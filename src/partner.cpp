#include "partner.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>

#include <fcntl.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

namespace {
constexpr uint64_t PARTNER_HDD_SIZE = 1224ULL * 32 * 256;
constexpr uint8_t FDC_INT_NEEDED = 1 << 0;
constexpr uint8_t FDC_INT_REQUESTED = 1 << 1;
constexpr uint8_t FDC_INT_SERVICED = 1 << 2;
constexpr size_t MAX_SIO_RX_FIFO_BYTES = 8192;
constexpr size_t MAX_TCP_RX_FIFO_BYTES = 32768;
constexpr size_t MAX_TCP_TX_FIFO_BYTES = 32768;
constexpr size_t MAX_PRINTER_TEXT_BYTES = 1 << 20;
constexpr uint64_t TCP_POLL_INTERVAL_ACTIVE_TICKS = 2048;
constexpr uint64_t TCP_POLL_INTERVAL_IDLE_TICKS = 8192;

static inline uint64_t i8272_bus_idle() {
    return I8272_CS | I8272_RD | I8272_WR | I8272_RESET;
}

static inline uint8_t i8272_bus_read(i8272_t* fdc, uint8_t reg) {
    uint64_t pins = i8272_bus_idle();
    pins |= (reg & 0x01);
    pins &= ~(I8272_CS | I8272_RD);
    pins = i8272_tick_pins(fdc, pins);
    return I8272_GET_DATA(pins);
}

static inline void i8272_bus_write(i8272_t* fdc, uint8_t reg, uint8_t data) {
    uint64_t pins = i8272_bus_idle();
    pins |= (reg & 0x01);
    I8272_SET_DATA(pins, data);
    pins &= ~(I8272_CS | I8272_WR);
    (void)i8272_tick_pins(fdc, pins);
}

static inline uint64_t s1410_bus_idle() {
    return S1410_CS | S1410_RD | S1410_WR | S1410_RESET;
}

static inline uint8_t s1410_bus_read(s1410_t* hdc, uint8_t reg) {
    uint64_t pins = s1410_bus_idle();
    pins |= (reg & 0x03);
    pins &= ~(S1410_CS | S1410_RD);
    pins = s1410_tick_pins(hdc, pins);
    return S1410_GET_DATA(pins);
}

static inline uint64_t z80sio_port_pins(uint8_t port, bool is_read, uint8_t data = 0) {
    uint64_t sio_pins = Z80SIO_CE | Z80SIO_IORQ;
    if (is_read) sio_pins |= Z80SIO_RD;
    else sio_pins |= Z80SIO_WR;

    if (!is_read) {
        Z80SIO_SET_DATA(sio_pins, data);
    }

    if ((port == 0xD8) || (port == 0xE0)) {
        // Channel A data
    } else if ((port == 0xD9) || (port == 0xE1)) {
        // Channel A control/status
        sio_pins |= Z80SIO_CS_A;
    } else if ((port == 0xDA) || (port == 0xE2)) {
        // Channel B data
        sio_pins |= Z80SIO_CS_B;
    } else {
        // Channel B control/status aliases
        sio_pins |= Z80SIO_CS_A | Z80SIO_CS_B;
    }
    return sio_pins;
}

static inline bool partner_sio0_port(uint8_t port) {
    return port >= 0xD8 && port <= 0xDB;
}

static inline bool partner_sio1_port(uint8_t port) {
    return port >= 0xE0 && port <= 0xE4;
}

static inline uint8_t z80sio_cpu_read(z80sio_t* sio, uint8_t port) {
    uint64_t pins = z80sio_port_pins(port, true);
    pins = z80sio_tick(sio, pins);
    return Z80SIO_GET_DATA(pins);
}

static inline void z80sio_cpu_write(z80sio_t* sio, uint8_t port, uint8_t data) {
    uint64_t pins = z80sio_port_pins(port, false, data);
    (void)z80sio_tick(sio, pins);
}

static inline uint8_t z80pio_cpu_read(z80pio_t* pio, uint8_t port) {
    uint64_t pins = Z80PIO_CE | Z80PIO_IORQ | Z80PIO_RD;
    if (port & 0x01) pins |= Z80PIO_CDSEL;
    if (port & 0x02) pins |= Z80PIO_BASEL;
    pins = z80pio_tick(pio, pins);
    return Z80PIO_GET_DATA(pins);
}

static inline void z80pio_cpu_write(z80pio_t* pio, uint8_t port, uint8_t data) {
    uint64_t pins = Z80PIO_CE | Z80PIO_IORQ;
    if (port & 0x01) pins |= Z80PIO_CDSEL;
    if (port & 0x02) pins |= Z80PIO_BASEL;
    Z80PIO_SET_DATA(pins, data);
    (void)z80pio_tick(pio, pins);
}

static int clamp_tcp_port(int port)
{
    if (port < 1) return 1;
    if (port > 65535) return 65535;
    return port;
}

static bool set_nonblocking(int fd)
{
    if (fd < 0)
        return false;
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
        return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

static int make_tcp_listener(int port)
{
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    int one = 1;
    (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)clamp_tcp_port(port));
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        return -1;
    }
    if (::listen(fd, 1) != 0) {
        ::close(fd);
        return -1;
    }
    if (!set_nonblocking(fd)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

static int accept_nonblocking(int listen_fd)
{
    if (listen_fd < 0)
        return -1;
    sockaddr_in client{};
    socklen_t len = sizeof(client);
    int fd = ::accept(listen_fd, reinterpret_cast<sockaddr*>(&client), &len);
    if (fd < 0)
        return -1;
    if (!set_nonblocking(fd)) {
        ::close(fd);
        return -1;
    }
    return fd;
}

static void close_fd_if_open(int &fd)
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

template <typename T>
static T clamp_delta(T v, T lo, T hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}
}

partner::partner()
{
    // Initialize all chips
    z80dma_init(&dma);
    z80ctc_init(&ctc);
    z80sio_init(&sio);
    z80sio_init(&sio2);
    z80pio_init(&pio);
    i8272_init(&fdc);
    s1410_init(&hdc);
    hdc.present = false;
    idpartner_sasi_init(&sasi_, &hdc);
    mm58167a_init(&rtc);

    // Connect sector-read callback (preserved across resets)
    fdc.read_sector = read_sector_cb;
    fdc.user_data   = this;
    hdc.read_blocks = read_hdd_blocks_cb;
    hdc.user_data   = this;

    sio_device_cfg_[sio_port_index(sio_port_id::sio1_a)].kind = sio_device_kind::none;
    sio_device_cfg_[sio_port_index(sio_port_id::sio1_b)].kind = sio_device_kind::none;
    sio_device_cfg_[sio_port_index(sio_port_id::sio2_a)].kind = sio_device_kind::none;
    sio_device_cfg_[sio_port_index(sio_port_id::sio2_b)].kind = sio_device_kind::none;
    set_sio_port_lock(sio_port_id::sio1_a, true, "Fixed internal channel");
    set_sio_port_lock(sio_port_id::sio1_b, false, "");
    set_sio_port_lock(sio_port_id::sio2_a, false, "");
    set_sio_port_lock(sio_port_id::sio2_b, false, "");

    reset();
}

partner::~partner()
{
    for (auto &runtime : sio_device_runtime_)
        cleanup_tcp_bridge(runtime.tcp);
}

std::pair<z80sio_t *, int> partner::resolve_sio_channel(sio_port_id port)
{
    switch (port)
    {
    case sio_port_id::sio1_a: return { &sio, Z80SIO_CHANNEL_A };
    case sio_port_id::sio1_b: return { &sio, Z80SIO_CHANNEL_B };
    case sio_port_id::sio2_a: return { &sio2, Z80SIO_CHANNEL_A };
    case sio_port_id::sio2_b: return { &sio2, Z80SIO_CHANNEL_B };
    }
    return { nullptr, Z80SIO_CHANNEL_A };
}

const std::pair<const z80sio_t *, int> partner::resolve_sio_channel_const(sio_port_id port) const
{
    switch (port)
    {
    case sio_port_id::sio1_a: return { &sio, Z80SIO_CHANNEL_A };
    case sio_port_id::sio1_b: return { &sio, Z80SIO_CHANNEL_B };
    case sio_port_id::sio2_a: return { &sio2, Z80SIO_CHANNEL_A };
    case sio_port_id::sio2_b: return { &sio2, Z80SIO_CHANNEL_B };
    }
    return { nullptr, Z80SIO_CHANNEL_A };
}

void partner::set_sio_port_lock(sio_port_id port, bool locked, const std::string &reason)
{
    const int idx = sio_port_index(port);
    sio_port_locked_[idx] = locked;
    sio_port_lock_reason_[idx] = reason;
}

partner::sio_device_config partner::get_sio_device_config(sio_port_id port) const
{
    return sio_device_cfg_[sio_port_index(port)];
}

bool partner::set_sio_device_config(sio_port_id port, const sio_device_config &cfg_in)
{
    const int idx = sio_port_index(port);
    if (sio_port_locked_[idx])
        return false;

    sio_device_config cfg = cfg_in;
    cfg.tcp_data_port = clamp_tcp_port(cfg.tcp_data_port);
    cfg.tcp_control_port = clamp_tcp_port(cfg.tcp_control_port);
    if (cfg.tcp_control_port == cfg.tcp_data_port)
        cfg.tcp_control_port = clamp_tcp_port(cfg.tcp_data_port + 1);

    const sio_device_config old_cfg = sio_device_cfg_[idx];
    sio_device_cfg_[idx] = cfg;
    const bool changed =
        old_cfg.kind != cfg.kind ||
        old_cfg.tcp_data_port != cfg.tcp_data_port ||
        old_cfg.tcp_control_port != cfg.tcp_control_port ||
        old_cfg.tcp_require_rts != cfg.tcp_require_rts ||
        old_cfg.tcp_cts_follows_data_client != cfg.tcp_cts_follows_data_client;
    if (changed)
        reset_sio_device_runtime(port);
    return true;
}

partner::sio_port_status partner::get_sio_port_status(sio_port_id port) const
{
    sio_port_status st{};
    const int idx = sio_port_index(port);
    const auto [chip, channel] = resolve_sio_channel_const(port);
    if (!chip)
        return st;

    const z80sio_channel_t &ch = chip->chn[channel];
    const auto &cfg = sio_device_cfg_[idx];
    const auto &rt = sio_device_runtime_[idx];

    st.locked = sio_port_locked_[idx];
    st.rts = ch.rts;
    st.dtr = ch.dtr;
    st.pending_rx_bytes = rt.rx_fifo.size();
    st.tx_bytes = rt.tx_bytes;
    st.rx_bytes = rt.rx_bytes;

    bool cts = false;
    bool dcd = false;
    bool connected = false;
    switch (cfg.kind)
    {
    case sio_device_kind::none:
        st.detail = "Not connected";
        break;
    case sio_device_kind::mouse_microsoft:
        cts = true;
        dcd = true;
        connected = true;
        st.detail = "Serial mouse (Microsoft)";
        break;
    case sio_device_kind::mouse_mousesystems:
        cts = true;
        dcd = true;
        connected = true;
        st.detail = "Serial mouse (Mouse Systems)";
        break;
    case sio_device_kind::mouse_logitech:
        cts = true;
        dcd = true;
        connected = true;
        st.detail = "Serial mouse (Logitech)";
        break;
    case sio_device_kind::tcp_bridge: {
        const bool data_connected = rt.tcp.data_client_fd >= 0;
        dcd = data_connected;
        if (rt.tcp.control_dcd_override_active)
            dcd = rt.tcp.control_dcd_override_value;
        cts = cfg.tcp_cts_follows_data_client ? data_connected : true;
        if (rt.tcp.control_cts_override_active)
            cts = rt.tcp.control_cts_override_value;
        connected = data_connected;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "TCP %d / ctl %d",
                      cfg.tcp_data_port, cfg.tcp_control_port);
        st.detail = buf;
        st.pending_rx_bytes += rt.tcp.data_rx_fifo.size();
        break;
    }
    }
    st.cts = cts;
    st.dcd = dcd;
    st.connected = connected;
    if (st.locked && !sio_port_lock_reason_[idx].empty())
        st.detail = sio_port_lock_reason_[idx];
    return st;
}

bool partner::is_sio_port_locked(sio_port_id port) const
{
    return sio_port_locked_[sio_port_index(port)];
}

std::string partner::get_sio_port_lock_reason(sio_port_id port) const
{
    return sio_port_lock_reason_[sio_port_index(port)];
}

partner::pio_device_config partner::get_pio_device_config(pio_port_id port) const
{
    return pio_device_cfg_[pio_port_index(port)];
}

void partner::set_pio_device_config(pio_port_id port, const pio_device_config &cfg)
{
    pio_device_cfg_[pio_port_index(port)] = cfg;
}

partner::pio_port_status partner::get_pio_port_status(pio_port_id port) const
{
    const auto &rt = pio_device_runtime_[pio_port_index(port)];
    pio_port_status st{};
    st.last_output = rt.last_output;
    st.covox_level = rt.covox_level;
    st.bytes_seen = rt.bytes_seen;
    return st;
}

void partner::cleanup_tcp_bridge(tcp_bridge_runtime &tcp)
{
    close_fd_if_open(tcp.data_client_fd);
    close_fd_if_open(tcp.control_client_fd);
    close_fd_if_open(tcp.listen_fd);
    close_fd_if_open(tcp.control_listen_fd);
    tcp.next_poll_tick = 0;
    tcp.control_cts_override_active = false;
    tcp.control_cts_override_value = false;
    tcp.control_dcd_override_active = false;
    tcp.control_dcd_override_value = false;
    tcp.last_rts = false;
    tcp.last_dtr = false;
    tcp.control_rx_buf.clear();
    tcp.data_rx_fifo.clear();
    tcp.data_tx_fifo.clear();
}

void partner::reset_sio_device_runtime(sio_port_id port)
{
    auto &rt = sio_device_runtime_[sio_port_index(port)];
    rt.rx_fifo.clear();
    rt.last_mouse_buttons = 0;
    rt.mouse_buttons_initialized = false;
    rt.mouse_accum_dx = 0;
    rt.mouse_accum_dy = 0;
    rt.tx_bytes = 0;
    rt.rx_bytes = 0;
    cleanup_tcp_bridge(rt.tcp);
}

bool partner::parse_bool_token(const std::string &token, bool &value)
{
    std::string up = token;
    for (char &ch : up)
        ch = (char)std::toupper((unsigned char)ch);
    if (up == "1" || up == "ON" || up == "TRUE" || up == "YES") {
        value = true;
        return true;
    }
    if (up == "0" || up == "OFF" || up == "FALSE" || up == "NO") {
        value = false;
        return true;
    }
    return false;
}

void partner::tcp_bridge_send_modem_state(tcp_bridge_runtime &tcp, const char *name, bool value)
{
    if (tcp.control_client_fd < 0)
        return;
    char line[64];
    std::snprintf(line, sizeof(line), "%s %d\n", name, value ? 1 : 0);
    const ssize_t n = ::send(tcp.control_client_fd, line, std::strlen(line), MSG_NOSIGNAL);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
        close_fd_if_open(tcp.control_client_fd);
}

void partner::tcp_bridge_parse_control_line(tcp_bridge_runtime &tcp, const std::string &line)
{
    auto send_reply = [&](const char *msg) {
        if (tcp.control_client_fd < 0)
            return;
        (void)::send(tcp.control_client_fd, msg, std::strlen(msg), MSG_NOSIGNAL);
    };

    std::string cmd;
    std::string arg;
    const size_t sp = line.find_first_of(" \t");
    if (sp == std::string::npos)
        cmd = line;
    else {
        cmd = line.substr(0, sp);
        size_t arg_start = line.find_first_not_of(" \t", sp);
        if (arg_start != std::string::npos)
            arg = line.substr(arg_start);
    }
    for (char &ch : cmd)
        ch = (char)std::toupper((unsigned char)ch);
    for (char &ch : arg)
        ch = (char)std::toupper((unsigned char)ch);

    if (cmd == "PING") {
        send_reply("PONG\n");
        return;
    }

    if (cmd == "CTS") {
        if (arg == "AUTO") {
            tcp.control_cts_override_active = false;
            send_reply("OK CTS AUTO\n");
            return;
        }
        bool val = false;
        if (parse_bool_token(arg, val)) {
            tcp.control_cts_override_active = true;
            tcp.control_cts_override_value = val;
            send_reply("OK CTS\n");
            return;
        }
        send_reply("ERR CTS <0|1|AUTO>\n");
        return;
    }

    if (cmd == "DCD") {
        if (arg == "AUTO") {
            tcp.control_dcd_override_active = false;
            send_reply("OK DCD AUTO\n");
            return;
        }
        bool val = false;
        if (parse_bool_token(arg, val)) {
            tcp.control_dcd_override_active = true;
            tcp.control_dcd_override_value = val;
            send_reply("OK DCD\n");
            return;
        }
        send_reply("ERR DCD <0|1|AUTO>\n");
        return;
    }

    send_reply("ERR UNKNOWN\n");
}

bool partner::ensure_tcp_bridge_listeners(sio_port_id port)
{
    const int idx = sio_port_index(port);
    const auto &cfg = sio_device_cfg_[idx];
    auto &tcp = sio_device_runtime_[idx].tcp;
    if (cfg.kind != sio_device_kind::tcp_bridge)
        return false;

    if (tcp.listen_fd < 0)
        tcp.listen_fd = make_tcp_listener(cfg.tcp_data_port);
    if (tcp.control_listen_fd < 0)
        tcp.control_listen_fd = make_tcp_listener(cfg.tcp_control_port);
    return tcp.listen_fd >= 0 && tcp.control_listen_fd >= 0;
}

void partner::poll_tcp_bridge(sio_port_id port, z80sio_channel_t &ch)
{
    const int idx = sio_port_index(port);
    auto &rt = sio_device_runtime_[idx];
    auto &tcp = rt.tcp;
    const auto &cfg = sio_device_cfg_[idx];
    if (!ensure_tcp_bridge_listeners(port))
        return;

    const bool active_before_poll =
        (tcp.data_client_fd >= 0) || (tcp.control_client_fd >= 0) || !tcp.data_tx_fifo.empty();
    const uint64_t interval =
        active_before_poll ? TCP_POLL_INTERVAL_ACTIVE_TICKS : TCP_POLL_INTERVAL_IDLE_TICKS;
    if (tick_count < tcp.next_poll_tick)
        return;
    tcp.next_poll_tick = tick_count + interval;

    if (tcp.data_client_fd < 0)
        tcp.data_client_fd = accept_nonblocking(tcp.listen_fd);
    if (tcp.control_client_fd < 0)
        tcp.control_client_fd = accept_nonblocking(tcp.control_listen_fd);

    if (tcp.data_client_fd >= 0)
    {
        uint8_t buf[512];
        for (;;)
        {
            const ssize_t n = ::recv(tcp.data_client_fd, buf, sizeof(buf), 0);
            if (n > 0) {
                for (ssize_t i = 0; i < n; i++) {
                    if (tcp.data_rx_fifo.size() >= MAX_TCP_RX_FIFO_BYTES)
                        tcp.data_rx_fifo.pop_front();
                    tcp.data_rx_fifo.push_back(buf[i]);
                }
                continue;
            }
            if (n == 0) {
                close_fd_if_open(tcp.data_client_fd);
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close_fd_if_open(tcp.data_client_fd);
            }
            break;
        }
    }

    if (tcp.control_client_fd >= 0)
    {
        char buf[256];
        for (;;)
        {
            const ssize_t n = ::recv(tcp.control_client_fd, buf, sizeof(buf), 0);
            if (n > 0) {
                tcp.control_rx_buf.append(buf, (size_t)n);
                for (;;) {
                    size_t nl = tcp.control_rx_buf.find('\n');
                    if (nl == std::string::npos)
                        break;
                    std::string line = tcp.control_rx_buf.substr(0, nl);
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    tcp.control_rx_buf.erase(0, nl + 1);
                    tcp_bridge_parse_control_line(tcp, line);
                }
                continue;
            }
            if (n == 0) {
                close_fd_if_open(tcp.control_client_fd);
                tcp.control_rx_buf.clear();
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                close_fd_if_open(tcp.control_client_fd);
                tcp.control_rx_buf.clear();
            }
            break;
        }
    }

    while (tcp.data_client_fd >= 0 && !tcp.data_tx_fifo.empty())
    {
        uint8_t out_buf[1024];
        const size_t out_n = std::min(tcp.data_tx_fifo.size(), sizeof(out_buf));
        for (size_t i = 0; i < out_n; i++)
            out_buf[i] = tcp.data_tx_fifo[i];

        const ssize_t sent = ::send(tcp.data_client_fd, out_buf, out_n, MSG_NOSIGNAL);
        if (sent > 0)
        {
            for (ssize_t i = 0; i < sent; i++)
                tcp.data_tx_fifo.pop_front();
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        close_fd_if_open(tcp.data_client_fd);
        break;
    }

    if (tcp.last_rts != ch.rts) {
        tcp.last_rts = ch.rts;
        tcp_bridge_send_modem_state(tcp, "RTS", ch.rts);
    }
    if (tcp.last_dtr != ch.dtr) {
        tcp.last_dtr = ch.dtr;
        tcp_bridge_send_modem_state(tcp, "DTR", ch.dtr);
    }

    const bool allow_remote_tx = !cfg.tcp_require_rts || ch.rts;
    while (allow_remote_tx && !tcp.data_rx_fifo.empty())
    {
        if (rt.rx_fifo.size() >= MAX_SIO_RX_FIFO_BYTES)
            rt.rx_fifo.pop_front();
        rt.rx_fifo.push_back(tcp.data_rx_fifo.front());
        tcp.data_rx_fifo.pop_front();
    }
}

void partner::queue_mouse_packet(sio_port_id port, int dx, int dy, uint8_t buttons)
{
    const int idx = sio_port_index(port);
    auto &rt = sio_device_runtime_[idx];
    const auto push_byte = [&](uint8_t b) {
        if (rt.rx_fifo.size() >= MAX_SIO_RX_FIFO_BYTES)
            rt.rx_fifo.pop_front();
        rt.rx_fifo.push_back(b);
    };

    const auto kind = sio_device_cfg_[idx].kind;
    if (kind == sio_device_kind::mouse_microsoft || kind == sio_device_kind::mouse_logitech)
    {
        const int8_t sx = (int8_t)clamp_delta(dx, -127, 127);
        const int8_t sy = (int8_t)clamp_delta(dy, -127, 127);
        const uint8_t xb = (uint8_t)sx;
        const uint8_t yb = (uint8_t)sy;
        // Microsoft/Logitech protocol is 7N1. The sync bit is bit6 (0x40),
        // not bit7, so keep packet bytes in the 0x00..0x7F range.
        uint8_t b1 = 0x40;
        if (buttons & 0x01) b1 |= 0x20; // left
        if (buttons & 0x02) b1 |= 0x10; // right
        b1 |= (uint8_t)((yb >> 4) & 0x0C);
        b1 |= (uint8_t)((xb >> 6) & 0x03);
        const uint8_t b2 = (uint8_t)(xb & 0x3F);
        const uint8_t b3 = (uint8_t)(yb & 0x3F);
        push_byte(b1);
        push_byte(b2);
        push_byte(b3);
        if (kind == sio_device_kind::mouse_logitech && (buttons & 0x04))
            push_byte(0x20);
        return;
    }

    if (kind == sio_device_kind::mouse_mousesystems)
    {
        const int clamped_dx = clamp_delta(dx, -127, 127);
        const int clamped_dy = clamp_delta(dy, -127, 127);
        // Mouse Systems is an 8N1 protocol. Use full 8-bit signed deltas.
        const int dxa = clamped_dx / 2;
        const int dxb = clamped_dx - dxa;
        const int dya = clamped_dy / 2;
        const int dyb = clamped_dy - dya;

        uint8_t b1 = 0x80;
        if ((buttons & 0x01) == 0) b1 |= 0x04; // left up
        if ((buttons & 0x04) == 0) b1 |= 0x02; // middle up
        if ((buttons & 0x02) == 0) b1 |= 0x01; // right up
        push_byte(b1);
        push_byte((uint8_t)(int8_t)dxa);
        push_byte((uint8_t)(int8_t)dya);
        push_byte((uint8_t)(int8_t)dxb);
        push_byte((uint8_t)(int8_t)dyb);
    }
}

void partner::queue_logitech_c7_identification(sio_port_id port)
{
    static constexpr const char *k_logitech_id =
        "\r\nLOGIMOUSE C7 Firmware Revision 3.0\r\n";
    const int idx = sio_port_index(port);
    auto &rt = sio_device_runtime_[idx];
    const auto push_byte = [&](uint8_t b) {
        if (rt.rx_fifo.size() >= MAX_SIO_RX_FIFO_BYTES)
            rt.rx_fifo.pop_front();
        rt.rx_fifo.push_back(b);
    };

    for (const char *p = k_logitech_id; *p; ++p)
        push_byte((uint8_t)*p);
    push_byte(0x00);
}

void partner::queue_logitech_c7_poll_report(sio_port_id port)
{
    const int idx = sio_port_index(port);
    auto &rt = sio_device_runtime_[idx];
    const auto push_byte = [&](uint8_t b) {
        if (rt.rx_fifo.size() >= MAX_SIO_RX_FIFO_BYTES)
            rt.rx_fifo.pop_front();
        rt.rx_fifo.push_back(b);
    };
    const auto with_even_parity = [](uint8_t data7) -> uint8_t {
        uint8_t b = data7 & 0x7F;
        uint8_t x = b;
        x ^= (uint8_t)(x >> 4);
        x ^= (uint8_t)(x >> 2);
        x ^= (uint8_t)(x >> 1);
        const bool odd = (x & 1u) != 0;
        if (odd)
            b |= 0x80;
        return b;
    };

    const int16_t dx = (int16_t)clamp_delta(rt.mouse_accum_dx, -2048, 2047);
    const int16_t dy = (int16_t)clamp_delta(rt.mouse_accum_dy, -2048, 2047);
    rt.mouse_accum_dx = 0;
    rt.mouse_accum_dy = 0;

    const bool left = (rt.last_mouse_buttons & 0x01) != 0;
    const bool right = (rt.last_mouse_buttons & 0x02) != 0;
    const bool middle = (rt.last_mouse_buttons & 0x04) != 0;

    const uint8_t data1 =
        0x40 |
        (left ? 0x10 : 0x00) |
        (middle ? 0x08 : 0x00) |
        (right ? 0x04 : 0x00);
    const uint16_t dx12 = (uint16_t)(dx & 0x0FFF);
    const uint16_t dy12 = (uint16_t)(dy & 0x0FFF);

    push_byte(with_even_parity(data1));
    push_byte(with_even_parity((uint8_t)(dx12 & 0x3F)));
    push_byte(with_even_parity((uint8_t)((dx12 >> 6) & 0x3F)));
    push_byte(with_even_parity((uint8_t)(dy12 & 0x3F)));
    push_byte(with_even_parity((uint8_t)((dy12 >> 6) & 0x3F)));
}

void partner::inject_serial_mouse_motion(int dx, int dy, bool left_pressed, bool right_pressed, bool middle_pressed)
{
    uint8_t buttons = 0;
    if (left_pressed) buttons |= 0x01;
    if (right_pressed) buttons |= 0x02;
    if (middle_pressed) buttons |= 0x04;

    const std::array<sio_port_id, 3> ports = {
        sio_port_id::sio1_b, sio_port_id::sio2_a, sio_port_id::sio2_b
    };
    for (sio_port_id port : ports)
    {
        const int idx = sio_port_index(port);
        const auto kind = sio_device_cfg_[idx].kind;
        const bool is_mouse =
            (kind == sio_device_kind::mouse_microsoft) ||
            (kind == sio_device_kind::mouse_mousesystems) ||
            (kind == sio_device_kind::mouse_logitech);
        if (!is_mouse)
            continue;

        auto &rt = sio_device_runtime_[idx];
        const bool button_change = !rt.mouse_buttons_initialized || (rt.last_mouse_buttons != buttons);
        if (kind == sio_device_kind::mouse_logitech)
        {
            // Logitech C7 is prompt/poll oriented: accumulate movement and
            // publish only when host sends 'P'.
            int32_t adx = rt.mouse_accum_dx + dx;
            int32_t ady = rt.mouse_accum_dy - dy;
            rt.mouse_accum_dx = clamp_delta(adx, (int32_t)-2048, (int32_t)2047);
            rt.mouse_accum_dy = clamp_delta(ady, (int32_t)-2048, (int32_t)2047);
            rt.last_mouse_buttons = buttons;
            rt.mouse_buttons_initialized = true;
            continue;
        }

        int rem_x = dx;
        int rem_y = -dy;
        bool sent = false;
        while (rem_x != 0 || rem_y != 0)
        {
            const int step_x = clamp_delta(rem_x, -127, 127);
            const int step_y = clamp_delta(rem_y, -127, 127);
            queue_mouse_packet(port, step_x, step_y, buttons);
            rem_x -= step_x;
            rem_y -= step_y;
            sent = true;
        }
        if (!sent && button_change) {
            queue_mouse_packet(port, 0, 0, buttons);
        }
        rt.last_mouse_buttons = buttons;
        rt.mouse_buttons_initialized = true;
    }
}

bool partner::has_logitech_mouse_attached() const
{
    const std::array<sio_port_id, 3> ports = {
        sio_port_id::sio1_b, sio_port_id::sio2_a, sio_port_id::sio2_b
    };
    for (sio_port_id port : ports)
    {
        const auto &cfg = sio_device_cfg_[sio_port_index(port)];
        if (cfg.kind == sio_device_kind::mouse_logitech)
            return true;
    }
    return false;
}

bool partner::has_serial_mouse_attached() const
{
    const std::array<sio_port_id, 3> ports = {
        sio_port_id::sio1_b, sio_port_id::sio2_a, sio_port_id::sio2_b
    };
    for (sio_port_id port : ports)
    {
        const auto kind = sio_device_cfg_[sio_port_index(port)].kind;
        if (kind == sio_device_kind::mouse_microsoft ||
            kind == sio_device_kind::mouse_mousesystems ||
            kind == sio_device_kind::mouse_logitech)
            return true;
    }
    return false;
}

void partner::apply_pio_device_output(pio_port_id port, uint8_t data)
{
    const int idx = pio_port_index(port);
    const auto kind = pio_device_cfg_[idx].kind;
    if (kind == pio_device_kind::none)
        return;

    auto &rt = pio_device_runtime_[idx];
    rt.last_output = data;
    rt.bytes_seen++;
    if (kind == pio_device_kind::covox) {
        rt.covox_level = (float)data / 255.0f;
        return;
    }
    if (kind == pio_device_kind::centronics_printer)
    {
        if (data == '\r') {
            virtual_printer_text_.push_back('\n');
        } else if (data == '\n') {
            if (virtual_printer_text_.empty() || virtual_printer_text_.back() != '\n')
                virtual_printer_text_.push_back('\n');
        } else if (data == '\t' || ((data >= 0x20) && (data < 0x7F))) {
            virtual_printer_text_.push_back((char)data);
        }
        if (virtual_printer_text_.size() > MAX_PRINTER_TEXT_BYTES)
            virtual_printer_text_.erase(0, virtual_printer_text_.size() - (MAX_PRINTER_TEXT_BYTES / 2));
    }
}

void partner::apply_sio_modem_inputs(uint64_t &bus_pins, sio_port_id port_a, sio_port_id port_b)
{
    auto eval_modem = [&](sio_port_id port) {
        const int idx = sio_port_index(port);
        if (sio_port_locked_[idx]) {
            // Internal hard-wired channels are always present/ready.
            return std::pair<bool, bool>{true, true};
        }
        bool cts = false;
        bool dcd = false;
        const auto &cfg = sio_device_cfg_[idx];
        const auto &rt = sio_device_runtime_[idx];
        switch (cfg.kind)
        {
        case sio_device_kind::none:
            break;
        case sio_device_kind::mouse_microsoft:
        case sio_device_kind::mouse_mousesystems:
        case sio_device_kind::mouse_logitech:
            cts = true;
            dcd = true;
            break;
        case sio_device_kind::tcp_bridge: {
            const bool data_connected = rt.tcp.data_client_fd >= 0;
            dcd = data_connected;
            if (rt.tcp.control_dcd_override_active)
                dcd = rt.tcp.control_dcd_override_value;
            cts = cfg.tcp_cts_follows_data_client ? data_connected : true;
            if (rt.tcp.control_cts_override_active)
                cts = rt.tcp.control_cts_override_value;
            break;
        }
        }
        return std::pair<bool, bool>{cts, dcd};
    };

    const auto [cts_a, dcd_a] = eval_modem(port_a);
    const auto [cts_b, dcd_b] = eval_modem(port_b);

    bus_pins &= ~(Z80SIO_CTSA | Z80SIO_DCDA | Z80SIO_CTSB | Z80SIO_DCDB);
    if (cts_a) bus_pins |= Z80SIO_CTSA;
    if (dcd_a) bus_pins |= Z80SIO_DCDA;
    if (cts_b) bus_pins |= Z80SIO_CTSB;
    if (dcd_b) bus_pins |= Z80SIO_DCDB;
}

void partner::service_sio_device(sio_port_id port, z80sio_t *chip, int channel)
{
    if (!chip)
        return;
    const int idx = sio_port_index(port);
    auto &cfg = sio_device_cfg_[idx];
    if (cfg.kind == sio_device_kind::none)
        return;
    auto &rt = sio_device_runtime_[idx];
    z80sio_channel_t &ch = chip->chn[channel];

    if (cfg.kind == sio_device_kind::tcp_bridge)
        poll_tcp_bridge(port, ch);

    if (!ch.tx_ready)
    {
        const uint8_t tx = z80sio_tx_data(chip, channel);
        rt.tx_bytes++;
        if (cfg.kind == sio_device_kind::tcp_bridge)
        {
            if (rt.tcp.data_tx_fifo.size() >= MAX_TCP_TX_FIFO_BYTES)
                rt.tcp.data_tx_fifo.pop_front();
            rt.tcp.data_tx_fifo.push_back(tx);
        }
        else if (cfg.kind == sio_device_kind::mouse_logitech)
        {
            // Logitech C7 prompt-mode commands:
            //   'c' => identification string
            //   'P' => poll report
            //   'D' => prompt mode (accepted, no-op)
            if (tx == 'c' || tx == 'C')
                queue_logitech_c7_identification(port);
            else if (tx == 'P' || tx == 'p')
                queue_logitech_c7_poll_report(port);
            else if (tx == 'D' || tx == 'd')
            {
                // Prompt mode command accepted (no-op in current model).
            }
        }
    }

    if (ch.rx_ready || rt.rx_fifo.empty())
        return;

    const uint8_t data = rt.rx_fifo.front();
    const bool was_ready = ch.rx_ready;
    z80sio_rx_data(chip, channel, data);
    if (!ch.rx_ready && !was_ready)
    {
        ch.rx_data = data;
        ch.rx_ready = true;
        ch.int_state |= Z80SIO_INT_NEEDED;
    }
    if (ch.rx_ready)
    {
        rt.rx_fifo.pop_front();
        rt.rx_bytes++;
    }
}

void partner::service_virtual_devices()
{
    const bool any_active =
        (sio_device_cfg_[sio_port_index(sio_port_id::sio1_b)].kind != sio_device_kind::none) ||
        (sio_device_cfg_[sio_port_index(sio_port_id::sio2_a)].kind != sio_device_kind::none) ||
        (sio_device_cfg_[sio_port_index(sio_port_id::sio2_b)].kind != sio_device_kind::none);
    if (!any_active)
        return;

    service_sio_device(sio_port_id::sio1_b, &sio, Z80SIO_CHANNEL_B);
    service_sio_device(sio_port_id::sio2_a, &sio2, Z80SIO_CHANNEL_A);
    service_sio_device(sio_port_id::sio2_b, &sio2, Z80SIO_CHANNEL_B);
}

void partner::load_rtc_nvram()
{
    static constexpr uint8_t k_rtc_nvram_defaults[8] = {
        0xF0, // 0xA8
        0x98, // 0xA9
        0xFF, // 0xAA
        0x01, // 0xAB
        0x85, // 0xAC
        0x07, // 0xAD
        0x00, // 0xAE
        0x57  // 0xAF
    };

    auto apply_safe_defaults = [&]() {
        for (size_t i = 0; i < sizeof(k_rtc_nvram_defaults); i++)
            rtc.regs[0x08 + i] = k_rtc_nvram_defaults[i];
    };

    std::ifstream file(rtc_nvram_path_, std::ios::binary);
    if (file)
    {
        uint8_t nvram[8]{};
        file.read(reinterpret_cast<char*>(nvram), sizeof(nvram));
        if (file.gcount() == (std::streamsize)sizeof(nvram))
        {
            for (size_t i = 0; i < sizeof(nvram); i++)
                rtc.regs[0x08 + i] = nvram[i];

            bool matches_reference = true;
            for (size_t i = 0; i < sizeof(k_rtc_nvram_defaults); i++) {
                if (rtc.regs[0x08 + i] != k_rtc_nvram_defaults[i]) {
                    matches_reference = false;
                    break;
                }
            }
            if (matches_reference) {
                return;
            }
            apply_safe_defaults();
            save_rtc_nvram();
            return;
        }
    }

    // No valid persisted CMOS yet: write the current seeded defaults once.
    apply_safe_defaults();
    save_rtc_nvram();
}

void partner::save_rtc_nvram() const
{
    std::ofstream file(rtc_nvram_path_, std::ios::binary | std::ios::trunc);
    if (!file)
        return;
    file.write(reinterpret_cast<const char*>(&rtc.regs[0x08]), 8);
}

void partner::load_disk(int drive, const std::string &path)
{
    if (drive < 0 || drive >= I8272_MAX_DRIVES)
        throw std::runtime_error("invalid drive number: " + std::to_string(drive));

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot open disk image: " + path);

    auto size = (uint64_t)file.tellg();
    file.seekg(0);

    auto &disk = disks_[drive];
    disk.data.resize(size);
    file.read(reinterpret_cast<char *>(disk.data.data()), (std::streamsize)size);

    // Auto-detect geometry from file size
    constexpr uint64_t FDD_77DS_SIZE = 77ULL * 2 * 18 * 256;   // 709,632 bytes
    constexpr uint64_t FDD_80DS_SIZE = 80ULL * 2 * 18 * 256;   // 737,280 bytes
    if ((size == FDD_77DS_SIZE) || (size == FDD_80DS_SIZE)) {
        disk.seclen = 256; disk.sectrk = 18; disk.heads = 2;
        std::cout << "[info] disk " << drive << " (FDD): " << path << "\n";
    } else if ((size % (36ULL * 256ULL)) == 0) {
        disk.seclen = 256; disk.sectrk = 18; disk.heads = 2;
        std::cout << "[info] disk " << drive << " (FDD, inferred double-sided): " << path << "\n";
    } else {
        disk.seclen = 256; disk.sectrk = 18; disk.heads = 1;
        std::cout << "[warning] disk " << drive << " unknown size " << size
                  << ", assuming single-sided FDD geometry: " << path << "\n";
    }

    fdc.drive[drive].ready = true;
}

void partner::load_hdd(const std::string &path)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("cannot open hard disk image: " + path);

    auto size = (uint64_t)file.tellg();
    file.seekg(0);
    if (size != PARTNER_HDD_SIZE)
        throw std::runtime_error("unexpected hard disk image size: " + std::to_string(size));

    hdd_.data.resize(size);
    hdd_.seclen = 256;
    hdd_.sectrk = 32;
    hdd_.heads = 1;
    file.read(reinterpret_cast<char *>(hdd_.data.data()), (std::streamsize)size);
    if (!file)
        throw std::runtime_error("incomplete hard disk image: " + path);

    hdc.present = true;
    std::cout << "[info] hard disk loaded: " << path << "\n";
}

bool partner::read_sector_cb(int drive, int c, int h, int r, int n,
                              uint8_t *buf, void *user)
{
    auto *self = static_cast<partner *>(user);
    if (drive < 0 || drive >= I8272_MAX_DRIVES) return false;
    int resolved_drive = drive;
    if ((drive == 1) &&
        self->disks_[1].data.empty() &&
        !self->disks_[0].data.empty() &&
        self->disks_[2].data.empty() &&
        self->disks_[3].data.empty())
    {
        resolved_drive = 0;
    }
    const auto &disk = self->disks_[resolved_drive];
    if (disk.data.empty()) return false;

    uint16_t sector_size = (n == 0) ? 128 : (128 << n);
    if (sector_size > I8272_SECTOR_SIZE) sector_size = I8272_SECTOR_SIZE;

    if ((r <= 0) || ((uint32_t)r > disk.sectrk) || ((uint32_t)h >= disk.heads)) {
        return false;
    }

    uint32_t lba = (((uint32_t)c * disk.heads) + (uint32_t)h) * disk.sectrk
                 + (uint32_t)(r - 1);
    uint64_t offset = (uint64_t)lba * disk.seclen;

    if (offset + sector_size > disk.data.size()) {
        return false;
    }

    memcpy(buf, disk.data.data() + offset, sector_size);
    return true;
}

bool partner::read_hdd_blocks_cb(uint32_t lba, uint32_t count, uint8_t *buf, void *user)
{
    auto *self = static_cast<partner *>(user);
    const auto &disk = self->hdd_;
    if (disk.data.empty()) return false;

    const uint64_t offset = (uint64_t)lba * disk.seclen;
    const uint64_t bytes = (uint64_t)count * disk.seclen;
    if (offset + bytes > disk.data.size()) return false;

    memcpy(buf, disk.data.data() + offset, (size_t)bytes);
    return true;
}

void partner::load_rom(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open rom file: " + path);
    file.read(reinterpret_cast<char *>(rom.data()), rom_size);
    if (!file)
        throw std::runtime_error("incomplete rom file: " + path);

    std::cout << "[info] rom loaded: " << path << "\n";
}

void partner::reset()
{
    pins = z80_reset(&cpu);
    z80dma_reset(&dma);
    z80ctc_reset(&ctc);
    z80sio_reset(&sio);
    z80sio_reset(&sio2);
    z80pio_reset(&pio);
    i8272_reset(&fdc);
    s1410_reset(&hdc);
    idpartner_sasi_reset(&sasi_);
    mm58167a_reset(&rtc);
    load_rtc_nvram();

    rom_enabled = true;
    ram_bank = 1;
    fdc_int_vector = 0;
    fdc_motor = 0;
    fdc_int_state = 0;
    fdc_reset_irq_armed_ = false;
    prompt_fdc_cleanup_done_ = false;
    fdc_motor_running = false;
    dma_busreq_latched = false;
    dma_ready_input_ = false;
    last_cpu_bus_pins_ = 0;
    io_read_latched_ = false;
    io_read_latched_addr_ = 0;
    io_read_latched_data_ = 0xFF;
    tick_count = 0;
    auto_floppy_key_sent_ = false;
    restore_drive_ready_flags();

    for (sio_port_id port : { sio_port_id::sio1_a, sio_port_id::sio1_b, sio_port_id::sio2_a, sio_port_id::sio2_b })
        reset_sio_device_runtime(port);
    for (auto &rt : pio_device_runtime_) {
        rt.last_output = 0;
        rt.covox_level = 0.0f;
        rt.bytes_seen = 0;
    }
    virtual_printer_text_.clear();
}

void partner::tick()
{
    tick_count++;
    mm58167a_sync_time(&rtc);
    i8272_tick(&fdc);
    const uint64_t prev_pins = pins;

    const bool cpu_ticked = !dma_owns_bus();
    if (cpu_ticked)
    {
        // IM2 ack data is sampled by the CPU during internal step 1657.
        // Present the external FDC vector on the bus one tick earlier so the
        // sample sees the intended byte instead of stale bus residue.
        if ((cpu.step == 1657) && fdc.irq_request)
        {
            const bool daisy_busy =
                (dma.int_state != 0) ||
                (ctc.chn[0].int_state != 0) || (ctc.chn[1].int_state != 0) ||
                (ctc.chn[2].int_state != 0) || (ctc.chn[3].int_state != 0) ||
                (sio.chn[0].int_state != 0) || (sio.chn[1].int_state != 0) ||
                (sio2.chn[0].int_state != 0) || (sio2.chn[1].int_state != 0) ||
                (pio.port[0].int_state != 0) || (pio.port[1].int_state != 0);
            if (!daisy_busy)
            {
                Z80_SET_DATA(pins, fdc_int_vector);
                pins |= Z80_IORQ;
            }
        }

        // Tick the CPU
        pins = z80_tick(&cpu, pins);

        // This z80 core enters the interrupt acknowledge sample microsteps
        // without leaving IORQ visible in the externally observed pin mask.
        // Reconstruct it here so the daisy-chain devices can see and service
        // the acknowledge cycle.
        if ((cpu.step == 1638) || (cpu.step == 1644) || (cpu.step == 1657))
            pins |= Z80_IORQ;

        service_cpu_bus(pins);

        // The DMA register port is programmed by CPU I/O instructions, but the
        // DMA core itself is level-sensitive and would otherwise decode the
        // same byte across multiple T-states. Handle port 0xC0 explicitly on
        // a read/write edge and leave z80dma_tick() to bus-master transfers
        // and interrupt handling.
        const uint16_t cpu_addr = Z80_GET_ADDR(pins);
        const uint8_t cpu_port = cpu_addr & 0xFF;
        const uint16_t prev_cpu_addr = Z80_GET_ADDR(prev_pins);
        const uint8_t prev_cpu_port = prev_cpu_addr & 0xFF;
        const bool dma_wr_now = (cpu_port == 0xC0) &&
            ((pins & (Z80_IORQ | Z80_M1 | Z80_WR)) == (Z80_IORQ | Z80_WR));
        const bool dma_wr_prev = (prev_cpu_port == 0xC0) &&
            ((prev_pins & (Z80_IORQ | Z80_M1 | Z80_WR)) == (Z80_IORQ | Z80_WR));
        const bool dma_rd_now = (cpu_port == 0xC0) &&
            ((pins & (Z80_IORQ | Z80_M1 | Z80_RD)) == (Z80_IORQ | Z80_RD));
        const bool dma_rd_prev = (prev_cpu_port == 0xC0) &&
            ((prev_pins & (Z80_IORQ | Z80_M1 | Z80_RD)) == (Z80_IORQ | Z80_RD));

        if (dma_wr_now && !dma_wr_prev)
        {
            z80dma_write(&dma, Z80_GET_DATA(pins));
        }
        else if (dma_rd_now && !dma_rd_prev)
            Z80_SET_DATA(pins, z80dma_read(&dma));
    }

    // Tick peripheral chips in interrupt daisy chain order
    // Rebuild the shared INT line from the current peripheral state each tick
    // instead of letting an old external request linger in `pins`.
    pins &= ~Z80_INT;
    // Set IEIO to enable interrupt daisy chain
    pins |= Z80_IEIO;

    // Highest priority: DMA (port 0xC0)
    const uint16_t addr = Z80_GET_ADDR(pins);
    const uint8_t port = addr & 0xFF;

    // Present a simple external-ready signal to the DMA so boot-time
    // transfers from the FDC data port don't start until sector data is
    // actually available.
    pins &= ~Z80DMA_RDY;
    dma_ready_input_ = false;
    if (dma.enabled)
    {
        const z80dma_port_t &src = dma.direction_ab ? dma.port_a : dma.port_b;
        bool ready = true;
        if (!src.is_memory)
        {
            const uint8_t src_port = (uint8_t)(src.address & 0xFF);
            if (dma.state == Z80DMA_STATE_WRITE) {
                // Once a byte has been latched, keep RDY asserted long enough
                // for the matching write-back cycle to complete.
                ready = true;
            } else if (src_port == 0xF1) {
                ready = (fdc.phase == I8272_PHASE_EXECUTE);
            } else if (src_port == 0x11) {
                ready = idpartner_sasi_drq(&sasi_);
            }
        }
        dma_ready_input_ = ready;
        if (ready)
            pins |= Z80DMA_RDY;
    }

    if (dma_owns_bus())
        pins |= Z80DMA_BUSACK;
    else
        pins &= ~Z80DMA_BUSACK;

    const z80dma_state_t dma_state_before = dma.state;
    const bool dma_transfer_active =
        (pins & Z80DMA_BUSACK) && (pins & Z80DMA_RDY) &&
        (dma_state_before == Z80DMA_STATE_READ || dma_state_before == Z80DMA_STATE_WRITE);
    if (dma_state_before == Z80DMA_STATE_READ && dma_transfer_active)
        service_dma_read_bus(pins);
    pins = z80dma_tick(&dma, pins);
    dma_busreq_latched = (pins & Z80DMA_BUSREQ) != 0;
    if ((dma_state_before == Z80DMA_STATE_WRITE || dma.state == Z80DMA_STATE_WRITE) &&
        (pins & Z80DMA_WR))
        service_dma_write_bus(pins);
    pins &= ~Z80DMA_CE;

    // Second priority: CTC (ports 0xC8-0xCB)
    if ((pins & Z80_IORQ) && !(pins & Z80_M1) && (port >= 0xC8 && port <= 0xCB))
    {
        pins |= Z80CTC_CE;
        if (port & 0x01) pins |= Z80CTC_CS0;
        if (port & 0x02) pins |= Z80CTC_CS1;
    }
    // The board docs describe a motor timeout signal generated from cascaded
    // counters; feed channel 0 timeout into channel 1 as a simple cascade.
    if (ctc.pins & Z80CTC_ZCTO0)
        pins |= Z80CTC_CLKTRG1;
    pins = z80ctc_tick(&ctc, pins);
    pins &= ~(Z80CTC_CE | Z80CTC_CS0 | Z80CTC_CS1 | Z80CTC_CLKTRG1);
    if (pins & Z80CTC_ZCTO1)
    {
        fdc_motor_running = false;
        fdc_motor = 0;
        for (auto &drive : fdc.drive)
            drive.motor = false;
    }

    // Third priority: first SIO chip (ports 0xD8-0xDB)
    if ((pins & Z80_IORQ) && !(pins & Z80_M1) && partner_sio0_port(port))
    {
        pins |= Z80SIO_CE;
        if (port & 0x01) pins |= Z80SIO_CS_A;  // control register select
        if (port & 0x02) pins |= Z80SIO_CS_B;  // channel B select
    }
    apply_sio_modem_inputs(pins, sio_port_id::sio1_a, sio_port_id::sio1_b);
    pins = z80sio_tick(&sio, pins);
    pins &= ~(Z80SIO_CE | Z80SIO_CS_A | Z80SIO_CS_B);

    // Fourth priority: second SIO chip (ports 0xE0-0xE4)
    if ((pins & Z80_IORQ) && !(pins & Z80_M1) && partner_sio1_port(port))
    {
        pins |= Z80SIO_CE;
        if (port & 0x01) pins |= Z80SIO_CS_A;  // control register select
        if (port & 0x02) pins |= Z80SIO_CS_B;  // channel B select
    }
    apply_sio_modem_inputs(pins, sio_port_id::sio2_a, sio_port_id::sio2_b);
    pins = z80sio_tick(&sio2, pins);
    pins &= ~(Z80SIO_CE | Z80SIO_CS_A | Z80SIO_CS_B);

    // Lowest priority: PIO (ports 0xD0-0xD3)
    if ((pins & Z80_IORQ) && !(pins & Z80_M1) && (port >= 0xD0 && port <= 0xD3))
    {
        pins |= Z80PIO_CE;
        if (port & 0x01) pins |= Z80PIO_CDSEL; // control
        if (port & 0x02) pins |= Z80PIO_BASEL; // port B
    }
    pins = z80pio_tick(&pio, pins);
    pins &= ~(Z80PIO_CE | Z80PIO_CDSEL | Z80PIO_BASEL);

    // DMA uses the same high-bit namespace as the Zilog family chips for its
    // local pins, so preserve the bus-request/acknowledge state explicitly.
    if (dma_busreq_latched)
        pins |= Z80DMA_BUSREQ;
    else
        pins &= ~Z80DMA_BUSREQ;
    if (dma_owns_bus())
        pins |= Z80DMA_BUSACK;
    else
        pins &= ~Z80DMA_BUSACK;

    service_virtual_devices();

    // FDC interrupt participates in the Z80 daisy chain after the Zilog devices.
    service_fdc_daisy(pins, cpu_ticked);

}

void partner::restore_drive_ready_flags()
{
    for (int i = 0; i < I8272_MAX_DRIVES; i++)
        fdc.drive[i].ready = !disks_[i].data.empty();
    if (disks_[1].data.empty() &&
        !disks_[0].data.empty() &&
        disks_[2].data.empty() &&
        disks_[3].data.empty())
    {
        fdc.drive[1].ready = true;
    }
}

void partner::service_cpu_bus(uint64_t &bus_pins)
{
    const uint16_t addr = Z80_GET_ADDR(bus_pins);

    if (bus_pins & Z80_MREQ)
    {
        if (bus_pins & Z80_RD)
        {
            Z80_SET_DATA(bus_pins, read_mem(addr));
        }
        else if (bus_pins & Z80_WR)
        {
            write_mem(addr, Z80_GET_DATA(bus_pins));
        }
    }
    else if ((bus_pins & Z80_IORQ) && !(bus_pins & Z80_M1))
    {
        const bool io_rd_now = (bus_pins & (Z80_IORQ | Z80_RD)) == (Z80_IORQ | Z80_RD);
        const bool io_rd_prev = (last_cpu_bus_pins_ & (Z80_IORQ | Z80_RD)) == (Z80_IORQ | Z80_RD);
        const bool io_wr_prev = (last_cpu_bus_pins_ & (Z80_IORQ | Z80_WR)) == (Z80_IORQ | Z80_WR);
        const uint16_t prev_addr = Z80_GET_ADDR(last_cpu_bus_pins_);
        const bool same_port = ((prev_addr & 0xFF) == (addr & 0xFF));
        const uint8_t cur_port = (uint8_t)(addr & 0xFF);
        const bool always_fresh_read =
            (cur_port == 0x10) || (cur_port == 0x11) || (cur_port == 0x12) || // SASI status/data/reset
            (cur_port == 0xF0) || (cur_port == 0xF1);                          // i8272 status/data

        if (bus_pins & Z80_RD)
        {
            if (always_fresh_read || !io_rd_prev || !same_port || !io_read_latched_)
            {
                io_read_latched_data_ = io_read(addr & 0xFF);
                io_read_latched_addr_ = addr & 0xFF;
                io_read_latched_ = true;
            }
            Z80_SET_DATA(bus_pins, io_read_latched_data_);
        }
        else if (bus_pins & Z80_WR)
        {
            if (!io_wr_prev || !same_port)
                io_write(addr & 0xFF, Z80_GET_DATA(bus_pins));
        }

        if (!io_rd_now || !same_port)
            io_read_latched_ = false;
    }
    else if ((bus_pins & (Z80_IORQ | Z80_M1)) == (Z80_IORQ | Z80_M1))
    {
        // External 8272 IM2 acknowledge.
        // Keep this on the early CPU bus phase so the vector byte lands on the
        // same acknowledge cycle, but don't steal the cycle from daisy-chain
        // devices when they have a pending/requested/serviced interrupt.
        const bool daisy_busy =
            (dma.int_state != 0) ||
            (ctc.chn[0].int_state != 0) || (ctc.chn[1].int_state != 0) ||
            (ctc.chn[2].int_state != 0) || (ctc.chn[3].int_state != 0) ||
            (sio.chn[0].int_state != 0) || (sio.chn[1].int_state != 0) ||
            (sio2.chn[0].int_state != 0) || (sio2.chn[1].int_state != 0) ||
            (pio.port[0].int_state != 0) || (pio.port[1].int_state != 0);

        if (fdc.irq_request && !daisy_busy)
        {
            Z80_SET_DATA(bus_pins, fdc_int_vector);
            fdc.irq_request = false;
            fdc_int_state = 0;
        }
    }

    last_cpu_bus_pins_ = bus_pins;
}

void partner::service_dma_read_bus(uint64_t &bus_pins)
{
    if (!(bus_pins & Z80DMA_BUSACK))
        return;

    const z80dma_port_t &src = dma.direction_ab ? dma.port_a : dma.port_b;
    const uint8_t src_addr = (uint8_t)(src.address & 0xFF);
    const uint8_t data = src.is_memory ? read_mem(src.address)
                                       : io_read(src_addr);
    if (!src.is_memory && src_addr == 0xF1)
        dma_fdc_reads_++;
    Z80DMA_SET_DATA(bus_pins, data);
}

void partner::service_dma_write_bus(uint64_t &bus_pins)
{
    if (!(bus_pins & Z80DMA_BUSACK) || !(bus_pins & Z80DMA_WR))
        return;

    const uint16_t addr = Z80DMA_GET_ADDR(bus_pins);
    const z80dma_port_t &dst = dma.direction_ab ? dma.port_b : dma.port_a;
    const uint8_t data = Z80DMA_GET_DATA(bus_pins);
    if (dst.is_memory) {
        write_mem(addr, data);
        dma_mem_writes_++;
    }
    else {
        io_write(addr & 0xFF, data);
    }
}

void partner::service_fdc_daisy(uint64_t &bus_pins, bool cpu_ticked)
{
    (void)cpu_ticked;
    // The Partner's 8272 is not a Zilog-family daisy-chain device. Its
    // interrupt is an external IM2 request whose low vector byte comes from
    // the board latch at port E8h, so expose it as a level-sensitive INT
    // source and let service_cpu_bus() provide the vector on acknowledge.
    fdc_int_state = 0;
    if (fdc.irq_request) {
        fdc_int_state = FDC_INT_REQUESTED;
        bus_pins |= Z80_INT;
    }
}

bool partner::dma_owns_bus() const
{
    if (!dma.enabled)
        return false;
    return dma_busreq_latched || (dma.state != Z80DMA_STATE_IDLE);
}

uint8_t partner::peek_ram(uint16_t addr) const
{
    if ((addr >= banked_base) && (addr < shared_base) && (ram_bank == 2))
        return ram_bank2_[addr - banked_base];
    return ram[addr];
}

uint8_t partner::read_mem(uint16_t addr)
{
    // Partner maps ROM across 0x0000-0x1FFF while enabled.
    // The dumped ROM is 2KB, mirrored across that 8KB range.
    if (rom_enabled && addr < 0x2000)
    {
        return rom[addr & (rom_size - 1)];
    }

    return peek_ram(addr);
}

void partner::write_mem(uint16_t addr, uint8_t data)
{
    // While ROM is enabled, low 8KB writes are not visible to RAM.
    if (rom_enabled && addr < 0x2000)
        return;

    if ((addr >= banked_base) && (addr < shared_base) && (ram_bank == 2))
    {
        ram_bank2_[addr - banked_base] = data;
        return;
    }

    ram[addr] = data;
}

uint8_t partner::io_read(uint16_t port)
{
    port &= 0xFF;

    if (port == 0x10)
        return idpartner_sasi_status_r(&sasi_);
    if (port == 0x11)
        return idpartner_sasi_data_r(&sasi_);
    if (port == 0x12)
        return s1410_bus_read(&hdc, 2);

    // MM58167 RTC: 0xA0-0xBF
    if ((port >= 0xA0 && port <= 0xB6) || port == 0xBF)
    {
        return rtc.regs[port - 0xA0];
    }

    // Z80 PIO: 0xD0-0xD3
    if (port >= 0xD0 && port <= 0xD3)
    {
        return z80pio_cpu_read(&pio, (uint8_t)port);
    }

    // Z80 SIO chip 0: D8/D9 = channel A data/control, DA/DB = channel B data/control.
    if (partner_sio0_port((uint8_t)port))
    {
        return z80sio_cpu_read(&sio, (uint8_t)port);
    }
    // Z80 SIO chip 1: E0/E1 = channel A data/control, E2/E3 = channel B data/control.
    if (partner_sio1_port((uint8_t)port))
    {
        return z80sio_cpu_read(&sio2, (uint8_t)port);
    }

    // Z80 DMA: 0xC0
    // Handled by the DMA chip tick via CE/IORQ wiring, not directly here.
    if (port == 0xC0)
    {
        return 0xFF;
    }

    // Intel 8272 FDC Status: 0xF0
    if (port == 0xF0)
    {
        return i8272_bus_read(&fdc, 0);
    }

    // Intel 8272 FDC Data: 0xF1
    if (port == 0xF1)
    {
        return i8272_bus_read(&fdc, 1);
    }

    // FDC Motor Status: 0x98
    if (port == 0x98)
    {
        return fdc_motor_running ? 0x01 : 0x00;
    }

    // Banking and control: 0x80-0x97
    // On Partner, both reads and writes of these ports update banking state.
    // 0x80-0x87 disables ROM overlay.
    if (port >= 0x80 && port <= 0x87)
    {
        rom_enabled = false;
        return 0xFF;
    }
    if (port >= 0x88 && port <= 0x8F)
    {
        ram_bank = 1;
        return 0xFF;
    }
    if (port >= 0x90 && port <= 0x97)
    {
        ram_bank = 2;
        return 0xFF;
    }

    return 0xFF;
}

void partner::io_write(uint16_t port, uint8_t data)
{
    port &= 0xFF;

    if (port == 0x10)
    {
        idpartner_sasi_ctrl_w(&sasi_, data);
        return;
    }
    if (port == 0x11)
    {
        idpartner_sasi_data_w(&sasi_, data);
        return;
    }
    if (port == 0x12)
    {
        idpartner_sasi_reset_w(&sasi_, data);
        return;
    }

    // MM58167 RTC: 0xA0-0xBF
    if ((port >= 0xA0 && port <= 0xB6) || port == 0xBF)
    {
        if (port == 0xAC)
            data = 0x51;
        rtc.regs[port - 0xA0] = data;
        if (port >= 0xA8 && port <= 0xAF)
            save_rtc_nvram();
        // Writing reset-counter register refreshes visible time registers.
        if (port == 0xB2)
            mm58167a_sync_time(&rtc);
        return;
    }

    // Z80 PIO: 0xD0-0xD3
    if (port >= 0xD0 && port <= 0xD3)
    {
        const bool is_data_write = (port & 0x01) == 0;
        const pio_port_id pio_port = (port & 0x02) ? pio_port_id::b : pio_port_id::a;
        z80pio_cpu_write(&pio, (uint8_t)port, data);
        if (is_data_write)
            apply_pio_device_output(pio_port, pio.port[(port & 0x02) ? Z80PIO_PORT_B : Z80PIO_PORT_A].output);
        return;
    }

    if (partner_sio0_port((uint8_t)port))
    {
        z80sio_cpu_write(&sio, (uint8_t)port, data);
        return;
    }
    if (partner_sio1_port((uint8_t)port))
    {
        z80sio_cpu_write(&sio2, (uint8_t)port, data);
        return;
    }

    // Z80 DMA: 0xC0
    // Handled by the DMA chip tick via CE/IORQ wiring, not directly here.
    if (port == 0xC0)
    {
        return;
    }

    // Intel 8272 FDC Data: 0xF1
    if (port == 0xF1)
    {
        i8272_bus_write(&fdc, 1, data);
        return;
    }

    // FDC Motor Control: 0x98
    if (port == 0x98)
    {
        // Board documentation describes OUT 98h as "turn motor on" and
        // IN 98h bit 0 as the motor-running status. Keep the motor on until
        // reset or the CTC-generated timeout pulse turns it off again.
        fdc_motor_running = true;
        fdc_motor = 0x01;
        for (auto &drive : fdc.drive)
            drive.motor = true;
        return;
    }

    // FDC Interrupt Vector: 0xE8
    if (port == 0xE8)
    {
        fdc_int_vector = data;
        if (!fdc_reset_irq_armed_)
        {
            // Arm the power-on reset-complete interrupt when firmware
            // configures the FDC vector during fdc_init, instead of keeping a
            // stale interrupt pending from machine reset all the way to the
            // monitor prompt.
            fdc.int_pending = false;
            fdc.irq_request = false;
            fdc.irq_delay = 64;
            fdc.irq_sets_sense = true;
            fdc.irq_enters_result = false;
            fdc_reset_irq_armed_ = true;
        }
        return;
    }

    // Memory banking and ROM control
    if (port >= 0x80 && port <= 0x87)
    {
        rom_enabled = false;
        return;
    }

    if (port >= 0x88 && port <= 0x8F)
    {
        ram_bank = 1;
        return;
    }

    if (port >= 0x90 && port <= 0x97)
    {
        ram_bank = 2;
        return;
    }
}
