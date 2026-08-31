#include "partner.hpp"
#include "internal_squid_server.hpp"
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>

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
constexpr uint64_t PARTNER_CPU_CLOCK_HZ = 4000000;
constexpr uint64_t PARTNER_SIO_CLOCK_HZ = 153600;
constexpr uint64_t PARTNER_CTC_XX1_HALF_PERIOD_TICKS = 1250;
constexpr uint8_t  PARTNER_SPURIOUS_VECTOR = 0x00;
constexpr uint8_t  PARTNER_FD0_TYPE_MASK = 0xC0;
constexpr uint8_t  PARTNER_FD1_TYPE_MASK = 0x30;
constexpr uint8_t  PARTNER_FD2_TYPE_MASK = 0x0C;
constexpr uint8_t  PARTNER_FD3_TYPE_MASK = 0x03;

static inline uint64_t i8272_bus_idle() {
    return I8272_CS | I8272_RD | I8272_WR | I8272_RESET;
}

static inline bool fdc_trace_enabled() {
    static const bool enabled = [] {
        const char *s = std::getenv("IDP_TRACE_FDC");
        return s && s[0] && s[0] != '0';
    }();
    return enabled;
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

static inline uint64_t mm58167a_bus_idle() {
    return MM58167A_CS | MM58167A_AS | MM58167A_DS |
           MM58167A_RW | MM58167A_RESET;
}

static inline uint8_t mm58167a_bus_read(mm58167a_t* rtc, uint8_t reg) {
    uint64_t pins = mm58167a_bus_idle();
    pins = (pins & ~0x1FULL) | (uint64_t)(reg & 0x1Fu);
    pins &= ~MM58167A_AS;
    pins = mm58167a_tick(rtc, pins);
    pins |= MM58167A_AS;
    pins &= ~(MM58167A_CS | MM58167A_DS);
    pins |= MM58167A_RW;
    pins = mm58167a_tick(rtc, pins);
    return MM58167A_GET_DATA(pins);
}

static inline void mm58167a_bus_write(mm58167a_t* rtc, uint8_t reg, uint8_t data) {
    uint64_t pins = mm58167a_bus_idle();
    pins = (pins & ~0x1FULL) | (uint64_t)(reg & 0x1Fu);
    pins &= ~MM58167A_AS;
    pins = mm58167a_tick(rtc, pins);
    pins |= MM58167A_AS;
    pins &= ~(MM58167A_CS | MM58167A_DS | MM58167A_RW);
    MM58167A_SET_DATA(pins, data);
    (void)mm58167a_tick(rtc, pins);
}

static inline uint64_t z80sio_port_pins(uint8_t port, bool is_read, uint8_t data = 0) {
    uint64_t sio_pins = Z80SIO_CE | Z80SIO_IORQ;
    if (is_read) sio_pins |= Z80SIO_RD;
    else sio_pins |= Z80SIO_WR;

    if (!is_read) {
        Z80SIO_SET_DATA(sio_pins, data);
    }

    switch (port & 0x03u) {
    case 0:
        // Channel A data
        break;
    case 1:
        // Channel A control/status
        sio_pins |= Z80SIO_CS_A;
        break;
    case 2:
        // Channel B data
        sio_pins |= Z80SIO_CS_B;
        break;
    default:
        // Channel B control/status aliases
        sio_pins |= Z80SIO_CS_A | Z80SIO_CS_B;
        break;
    }
    return sio_pins;
}

static inline bool partner_sio0_port(uint8_t port) {
    return port >= 0xD8 && port <= 0xDF;
}

static inline bool partner_sio1_port(uint8_t port) {
    return port >= 0xE0 && port <= 0xE7;
}

static inline bool partner_dma_port(uint8_t port) {
    return port >= 0xC0 && port <= 0xC7;
}

static inline bool partner_ctc_port(uint8_t port) {
    return port >= 0xC8 && port <= 0xCF;
}

static inline bool partner_pio_port(uint8_t port) {
    return port >= 0xD0 && port <= 0xD7;
}

static inline bool partner_fdc_vector_port(uint8_t port) {
    return port >= 0xE8 && port <= 0xEF;
}

static inline bool partner_fdc_port(uint8_t port) {
    return port >= 0xF0 && port <= 0xF7;
}

static inline bool partner_motor_port(uint8_t port) {
    return port >= 0x98 && port <= 0x9F;
}

static inline bool partner_sasi_port(uint8_t port) {
    return port >= 0x10 && port <= 0x1F;
}

static inline uint8_t partner_sasi_function(uint8_t port) {
    return port & 0x03u;
}

static inline uint8_t z80sio_cpu_read(z80sio_t* sio, uint8_t port) {
    uint64_t pins = z80sio_port_pins(port, true);
    pins |= sio->pins & (Z80SIO_DCDA | Z80SIO_CTSA |
                         Z80SIO_DCDB | Z80SIO_CTSB);
    pins = z80sio_tick(sio, pins);
    return Z80SIO_GET_DATA(pins);
}

static inline void z80sio_cpu_write(z80sio_t* sio, uint8_t port, uint8_t data) {
    uint64_t pins = z80sio_port_pins(port, false, data);
    pins |= sio->pins & (Z80SIO_DCDA | Z80SIO_CTSA |
                         Z80SIO_DCDB | Z80SIO_CTSB);
    (void)z80sio_tick(sio, pins);
}

static inline uint8_t z80pio_cpu_read(z80pio_t* pio, uint8_t port) {
    uint64_t pins = Z80PIO_CE | Z80PIO_IORQ | Z80PIO_RD |
                    Z80PIO_ASTB | Z80PIO_BSTB;
    if (port & 0x01) pins |= Z80PIO_CDSEL;
    if (port & 0x02) pins |= Z80PIO_BASEL;
    pins = z80pio_tick(pio, pins);
    return Z80PIO_GET_DATA(pins);
}

static inline void z80pio_cpu_write(z80pio_t* pio, uint8_t port, uint8_t data) {
    uint64_t pins = Z80PIO_CE | Z80PIO_IORQ | Z80PIO_ASTB | Z80PIO_BSTB;
    if (port & 0x01) pins |= Z80PIO_CDSEL;
    if (port & 0x02) pins |= Z80PIO_BASEL;
    Z80PIO_SET_DATA(pins, data);
    (void)z80pio_tick(pio, pins);
}

static inline uint8_t z80ctc_cpu_read(z80ctc_t* ctc, uint8_t port) {
    uint64_t pins = Z80CTC_CE | Z80CTC_IORQ | Z80CTC_RD;
    if (port & 0x01) pins |= Z80CTC_CS0;
    if (port & 0x02) pins |= Z80CTC_CS1;
    pins = z80ctc_tick(ctc, pins);
    return Z80CTC_GET_DATA(pins);
}

static inline void z80ctc_cpu_write(z80ctc_t* ctc, uint8_t port, uint8_t data) {
    uint64_t pins = Z80CTC_CE | Z80CTC_IORQ | Z80_WR;
    if (port & 0x01) pins |= Z80CTC_CS0;
    if (port & 0x02) pins |= Z80CTC_CS1;
    Z80CTC_SET_DATA(pins, data);
    (void)z80ctc_tick(ctc, pins);
}

static int clamp_tcp_port(int port)
{
    if (port < 1) return 1;
    if (port > 65535) return 65535;
    return port;
}

static bool set_nonblocking(idp_socket_t fd)
{
    return fd >= 0 && idp_socket_set_nonblocking(fd);
}

static idp_socket_t make_tcp_listener(int port)
{
    if (!idp_socket_initialize())
        return idp_invalid_socket;
    const idp_socket_t fd = (idp_socket_t)::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return idp_invalid_socket;

    idp_socket_set_reuse_address(fd);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)clamp_tcp_port(port));
    if (::bind(idp_native_socket(fd), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        idp_socket_close(fd);
        return idp_invalid_socket;
    }
    if (::listen(idp_native_socket(fd), 1) != 0) {
        idp_socket_close(fd);
        return idp_invalid_socket;
    }
    if (!set_nonblocking(fd)) {
        idp_socket_close(fd);
        return idp_invalid_socket;
    }
    return fd;
}

static idp_socket_t accept_nonblocking(idp_socket_t listen_fd)
{
    if (listen_fd < 0)
        return idp_invalid_socket;
    sockaddr_in client{};
    idp_socklen_t len = sizeof(client);
    const idp_socket_t fd = (idp_socket_t)::accept(
        idp_native_socket(listen_fd), reinterpret_cast<sockaddr*>(&client), &len);
    if (fd < 0)
        return idp_invalid_socket;
    if (!set_nonblocking(fd)) {
        idp_socket_close(fd);
        return idp_invalid_socket;
    }
    return fd;
}

static void close_fd_if_open(idp_socket_t &fd)
{
    if (fd >= 0) {
        idp_socket_close(fd);
        fd = idp_invalid_socket;
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

partner::partner(const std::string &rtc_nvram_path) : rtc_nvram_path_(rtc_nvram_path)
{
    // Initialize all chips
    z80dma_init(&dma);
    z80ctc_init(&ctc);
    z80sio_init(&sio);
    z80sio_init(&sio2);
    z80pio_init(&pio);
    i8272_init(&fdc);
    s1410_init(&hdc);
    s1410_set_present(&hdc, false);
    idpartner_sasi_init(&sasi_, &hdc);
    mm58167a_init(&rtc);

    // Tests may advance the RTC from the emulated 4 MHz clock for reproducible
    // results.  Interactive emulation uses the host wall clock, like the
    // physical battery-backed RTC; protocol timeouts then remain real-time even
    // when a debug build cannot emulate four million ticks per second.
    if (const char *fixed_rtc = std::getenv("IDP_FIXED_RTC")) {
        rtc.det_base = (time_t)std::strtoull(fixed_rtc, nullptr, 10);
        rtc.det_hz = 4000000u;
        rtc.det_ticks = &tick_count;
    }
    mm58167a_sync_time(&rtc);

    // Connect sector-read callback (preserved across resets)
    fdc.read_sector = read_sector_cb;
    fdc.write_sector = write_sector_cb;
    fdc.user_data   = this;
    s1410_set_block_callbacks(
        &hdc, read_hdd_blocks_cb, write_hdd_blocks_cb, this);

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

void partner::set_emulated_rtc_base(time_t base)
{
    rtc.det_base = base;
    rtc.det_hz = PARTNER_CPU_CLOCK_HZ;
    rtc.det_ticks = &tick_count;
    rtc.last_sync_time = 0;
    rtc.last_sync_millisecond = UINT16_MAX;
    rtc_host_sync_divider_ = 0;
    mm58167a_sync_time(&rtc);
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
    sio_modem_dirty_mask_ |= (uint8_t)(1u << idx);
    request_sio_service();
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

    // One embedded Retro Vault service can be cabled to any free serial
    // port. Selecting it on a new port moves the virtual cable instead of
    // creating ambiguous duplicate servers.
    if (cfg.kind == sio_device_kind::internal_squid)
    {
        for (int other = 0; other < static_cast<int>(sio_device_cfg_.size()); ++other)
        {
            if (other == idx ||
                sio_device_cfg_[other].kind != sio_device_kind::internal_squid)
                continue;
            sio_device_cfg_[other].kind = sio_device_kind::none;
            sio_modem_dirty_mask_ |= (uint8_t)(1u << other);
            reset_sio_device_runtime(static_cast<sio_port_id>(other));
        }
    }

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
    sio_modem_dirty_mask_ |= (uint8_t)(1u << idx);
    request_sio_service();
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
    st.session_generation = rt.session_generation;

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
    case sio_device_kind::internal_squid:
        cts = internal_squid_ == nullptr ||
            internal_squid_->can_receive_serial_byte();
        dcd = true;
        connected = internal_squid_ != nullptr && internal_squid_->link_up();
        st.detail = internal_squid_ != nullptr
            ? internal_squid_->status_text()
            : "Internal Squid (waiting for serial client)";
        if (internal_squid_ != nullptr)
            st.pending_rx_bytes += internal_squid_->pending_serial_bytes();
        break;
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
    const int index = pio_port_index(port);
    if (pio_device_cfg_[index].kind == cfg.kind)
        return;
    pio_device_cfg_[index] = cfg;
    pio_device_runtime_[index].covox_level = 0.0f;

    // A routing change is an audio discontinuity. Discard stale samples for
    // this port so the host starts the newly attached DAC at quiet midscale.
    std::erase_if(covox_sample_events_, [port](const covox_sample_event &event) {
        return event.port == port;
    });
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

std::vector<partner::covox_sample_event> partner::drain_covox_sample_events()
{
    std::vector<covox_sample_event> events;
    events.reserve(covox_sample_events_.size());
    while (!covox_sample_events_.empty())
    {
        events.push_back(covox_sample_events_.front());
        covox_sample_events_.pop_front();
    }
    return events;
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
    const int index = sio_port_index(port);
    auto &rt = sio_device_runtime_[index];
    sio_modem_dirty_mask_ |= (uint8_t)(1u << index);
    request_sio_service();
    rt.rx_fifo.clear();
    rt.last_mouse_buttons = 0;
    rt.mouse_buttons_initialized = false;
    rt.mouse_accum_dx = 0;
    rt.mouse_accum_dy = 0;
    rt.tx_bytes = 0;
    rt.rx_bytes = 0;
    ++rt.session_generation;
    rt.next_internal_squid_poll_tick = 0;
    cleanup_tcp_bridge(rt.tcp);
    if (sio_device_cfg_[sio_port_index(port)].kind ==
        sio_device_kind::internal_squid && internal_squid_ != nullptr)
        internal_squid_->reset_link();
}

void partner::reset_sio_device_session(sio_port_id port)
{
    const int index = sio_port_index(port);
    const auto kind = sio_device_cfg_[index].kind;
    auto &runtime = sio_device_runtime_[index];

    if (kind == sio_device_kind::mouse_microsoft ||
        kind == sio_device_kind::mouse_mousesystems ||
        kind == sio_device_kind::mouse_logitech)
    {
        // A guest mouse driver resets its SIO channel before initialization.
        // Discard motion accumulated during CP/M boot and begin a new host
        // coordinate session for the driver's freshly centered cursor.
        runtime.rx_fifo.clear();
        runtime.last_mouse_buttons = 0;
        runtime.mouse_buttons_initialized = false;
        runtime.mouse_accum_dx = 0;
        runtime.mouse_accum_dy = 0;
        ++runtime.session_generation;
        sio_modem_dirty_mask_ |= (uint8_t)(1u << index);
        request_sio_service();
        return;
    }

    if (kind != sio_device_kind::internal_squid)
        return;

    // A guest Squid client begins a session by resetting its selected Z80 SIO
    // channel. Treat that as reconnecting the internal virtual cable: retry
    // bytes from the preceding session must not enter the new handshake.
    sio_modem_dirty_mask_ |= (uint8_t)(1u << index);
    request_sio_service();
    runtime.rx_fifo.clear();
    runtime.next_internal_squid_poll_tick = 0;
    if (internal_squid_ != nullptr)
        internal_squid_->reset_link();
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
    const idp_socket_count_t n = idp_socket_send(
        tcp.control_client_fd, line, std::strlen(line));
    if (n < 0 && !idp_socket_would_block())
        close_fd_if_open(tcp.control_client_fd);
}

void partner::tcp_bridge_parse_control_line(tcp_bridge_runtime &tcp, const std::string &line)
{
    auto send_reply = [&](const char *msg) {
        if (tcp.control_client_fd < 0)
            return;
        (void)idp_socket_send(tcp.control_client_fd, msg, std::strlen(msg));
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

    const auto disconnect_data_client = [&]() {
        close_fd_if_open(tcp.data_client_fd);
        tcp.data_rx_fifo.clear();
        tcp.data_tx_fifo.clear();
        rt.rx_fifo.clear();
    };

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

    bool discard_socket_rx = false;
    if (tcp.last_rts != ch.rts) {
        const bool was_rts = tcp.last_rts;
        tcp.last_rts = ch.rts;
        tcp_bridge_send_modem_state(tcp, "RTS", ch.rts);
        if (cfg.tcp_require_rts) {
            /* Neither bytes already staged for the UART nor bytes waiting in
               the host socket belong to a future receiver.  Clear both
               queue levels on close, and drain the host backlog once when a
               new receiver raises RTS so it starts at a clean boundary. */
            tcp.data_rx_fifo.clear();
            rt.rx_fifo.clear();
            discard_socket_rx = !was_rts && ch.rts;
        }
    }

    if (tcp.data_client_fd >= 0)
    {
        uint8_t buf[512];
        for (;;)
        {
            const idp_socket_count_t n = idp_socket_recv(
                tcp.data_client_fd, buf, sizeof(buf));
            if (n > 0) {
                // RTS gates the emulated receiver, not a future receiver.
                // Retaining bytes received while RTS is low releases stale
                // traffic into the next program that opens this port.
                if (!discard_socket_rx &&
                    (!cfg.tcp_require_rts || ch.rts)) {
                    for (idp_socket_count_t i = 0; i < n; i++) {
                        if (tcp.data_rx_fifo.size() >= MAX_TCP_RX_FIFO_BYTES)
                            tcp.data_rx_fifo.pop_front();
                        tcp.data_rx_fifo.push_back(buf[i]);
                    }
                }
                continue;
            }
            if (n == 0) {
                disconnect_data_client();
            } else if (!idp_socket_would_block()) {
                disconnect_data_client();
            }
            break;
        }
    }

    if (tcp.control_client_fd >= 0)
    {
        char buf[256];
        for (;;)
        {
            const idp_socket_count_t n = idp_socket_recv(
                tcp.control_client_fd, buf, sizeof(buf));
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
            } else if (!idp_socket_would_block()) {
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

        const idp_socket_count_t sent = idp_socket_send(
            tcp.data_client_fd, out_buf, out_n);
        if (sent > 0)
        {
            for (idp_socket_count_t i = 0; i < sent; i++)
                tcp.data_tx_fifo.pop_front();
            continue;
        }
        if (sent < 0 && idp_socket_would_block())
            break;
        disconnect_data_client();
        break;
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
        request_sio_service();
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

bool partner::queue_pending_streaming_mouse_packet(sio_port_id port, bool force)
{
    const int idx = sio_port_index(port);
    auto &rt = sio_device_runtime_[idx];
    const auto kind = sio_device_cfg_[idx].kind;
    if (kind != sio_device_kind::mouse_microsoft &&
        kind != sio_device_kind::mouse_mousesystems)
        return false;
    if (!force && rt.mouse_accum_dx == 0 && rt.mouse_accum_dy == 0)
        return false;

    const int step_x = clamp_delta(rt.mouse_accum_dx, (int32_t)-127,
                                   (int32_t)127);
    const int step_y = clamp_delta(rt.mouse_accum_dy, (int32_t)-127,
                                   (int32_t)127);
    rt.mouse_accum_dx -= step_x;
    rt.mouse_accum_dy -= step_y;
    queue_mouse_packet(port, step_x, step_y, rt.last_mouse_buttons);
    return true;
}

void partner::queue_logitech_c7_identification(sio_port_id port)
{
    /* The Partner LOGI/Genius driver consumes a fixed 60-byte response to
       the 'c' command before beginning five-byte polls. Real devices pad the
       human-readable identification record; reproduce that wire length. */
    static constexpr std::array<uint8_t, 60> k_logitech_id = [] {
        std::array<uint8_t, 60> response{};
        constexpr char text[] =
            "\r\nLOGIMOUSE C7 Firmware Revision 3.0\r\n";
        std::copy_n(text, sizeof(text) - 1, response.begin());
        return response;
    }();
    const int idx = sio_port_index(port);
    auto &rt = sio_device_runtime_[idx];
    const auto push_byte = [&](uint8_t b) {
        if (rt.rx_fifo.size() >= MAX_SIO_RX_FIFO_BYTES)
            rt.rx_fifo.pop_front();
        rt.rx_fifo.push_back(b);
        request_sio_service();
    };

    for (uint8_t byte : k_logitech_id)
        push_byte(byte);
}

void partner::queue_logitech_c7_poll_report(sio_port_id port)
{
    const int idx = sio_port_index(port);
    auto &rt = sio_device_runtime_[idx];
    const auto push_byte = [&](uint8_t b) {
        if (rt.rx_fifo.size() >= MAX_SIO_RX_FIFO_BYTES)
            rt.rx_fifo.pop_front();
        rt.rx_fifo.push_back(b);
        request_sio_service();
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

        /* SDL may deliver many motion events in one 60 Hz frame, while a
           2400-baud mouse can put only about four bytes on the wire in that
           time. Accumulate excess motion and keep at most one packet waiting
           behind the byte being shifted. The old byte-per-event queue grew
           for roughly ten seconds, then dropped individual bytes and left
           the guest packet decoder permanently out of phase. Button edges
           are forced into complete packets so clicks are not coalesced away. */
        /* Preserve the wire order around button edges. Any motion accumulated
           while the old button state was active must precede the edge packet;
           otherwise a quick drag is decoded as movement after button-up. Keep
           only one final old-state packet at an overloaded edge so latency
           remains bounded. */
        if (button_change && rt.mouse_buttons_initialized)
        {
            (void)queue_pending_streaming_mouse_packet(port, false);
            rt.mouse_accum_dx = 0;
            rt.mouse_accum_dy = 0;
        }

        rt.last_mouse_buttons = buttons;
        rt.mouse_buttons_initialized = true;

        const int64_t accumulated_x = (int64_t)rt.mouse_accum_dx + dx;
        /* Microsoft reports Y in screen-coordinate direction. Mouse Systems
           negates its wire Y in the SDK decoder, so compensate only for that
           protocol here. This keeps inject_serial_mouse_motion() coordinates
           identical for every emulated mouse. */
        const int wire_dy = kind == sio_device_kind::mouse_microsoft ? dy : -dy;
        const int64_t accumulated_y = (int64_t)rt.mouse_accum_dy + wire_dy;
        rt.mouse_accum_dx = (int32_t)std::clamp<int64_t>(
            accumulated_x, INT32_MIN, INT32_MAX);
        rt.mouse_accum_dy = (int32_t)std::clamp<int64_t>(
            accumulated_y, INT32_MIN, INT32_MAX);
        if (button_change || rt.rx_fifo.empty())
            (void)queue_pending_streaming_mouse_packet(port, button_change);
    }
}

void partner::deactivate_serial_mouse_input()
{
    const std::array<sio_port_id, 3> ports = {
        sio_port_id::sio1_b, sio_port_id::sio2_a, sio_port_id::sio2_b
    };
    for (sio_port_id port : ports)
    {
        const int idx = sio_port_index(port);
        const auto kind = sio_device_cfg_[idx].kind;
        if (kind != sio_device_kind::mouse_microsoft &&
            kind != sio_device_kind::mouse_mousesystems &&
            kind != sio_device_kind::mouse_logitech)
            continue;

        auto &rt = sio_device_runtime_[idx];
        rt.mouse_accum_dx = 0;
        rt.mouse_accum_dy = 0;
        const bool button_change =
            rt.mouse_buttons_initialized && rt.last_mouse_buttons != 0;
        rt.last_mouse_buttons = 0;
        rt.mouse_buttons_initialized = true;
        if (button_change && kind != sio_device_kind::mouse_logitech)
            (void)queue_pending_streaming_mouse_packet(port, true);
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

partner::debug_cpu_state partner::capture_debug_cpu_state() const
{
    debug_cpu_state state;
    state.af = cpu.af;
    state.bc = cpu.bc;
    state.de = cpu.de;
    state.hl = cpu.hl;
    state.af_alt = cpu.af2;
    state.bc_alt = cpu.bc2;
    state.de_alt = cpu.de2;
    state.hl_alt = cpu.hl2;
    state.ix = cpu.ix;
    state.iy = cpu.iy;
    state.sp = cpu.sp;
    state.pc = cpu.pc;
    state.i = cpu.i;
    state.r = cpu.r;
    state.im = cpu.im;
    state.iff1 = cpu.iff1;
    state.iff2 = cpu.iff2;
    state.halted = (pins & Z80_HALT) != 0;
    return state;
}

void partner::apply_debug_cpu_state(const debug_cpu_state &state)
{
    cpu.af = state.af;
    cpu.bc = state.bc;
    cpu.de = state.de;
    cpu.hl = state.hl;
    cpu.af2 = state.af_alt;
    cpu.bc2 = state.bc_alt;
    cpu.de2 = state.de_alt;
    cpu.hl2 = state.hl_alt;
    cpu.ix = state.ix;
    cpu.iy = state.iy;
    cpu.sp = state.sp;
    cpu.pc = state.pc;
    cpu.i = state.i;
    cpu.r = state.r;
    cpu.im = state.im;
    cpu.iff1 = state.iff1;
    cpu.iff2 = state.iff2;

    if (state.halted)
        pins |= Z80_HALT;
    else
        pins &= ~Z80_HALT;
}

std::vector<uint8_t> partner::read_debug_memory(uint32_t address, size_t length) const
{
    std::vector<uint8_t> out;
    out.reserve(length);
    for (size_t i = 0; i < length; ++i)
        out.push_back(peek_mem((uint16_t)(address + i)));
    return out;
}

void partner::write_debug_memory(uint32_t address, const std::vector<uint8_t> &data,
                                 bool allow_rom)
{
    for (size_t i = 0; i < data.size(); ++i) {
        const uint16_t addr = (uint16_t)(address + i);
        if (allow_rom && rom_enabled && addr < rom_capacity) {
            if (addr < rom_size) {
                rom[addr] = data[i];
                rom_loaded_ = true;
            } else {
                rom2[addr - rom_size] = data[i];
                rom2_loaded_ = true;
            }
        } else {
            write_mem(addr, data[i]);
        }
    }
}

void partner::clear_debug_memory()
{
    ram.fill(0);
    ram_bank2_.fill(0);
}

void partner::load_debug_rom(const std::vector<uint8_t> &data)
{
    if (data.size() != rom_size && data.size() != rom_capacity)
        throw std::runtime_error("Partner ROM image must be exactly 2048 or 4096 bytes");
    std::copy_n(data.begin(), rom_size, rom.begin());
    rom2.fill(0xFF);
    rom2_loaded_ = data.size() == rom_capacity;
    if (rom2_loaded_)
        std::copy(data.begin() + rom_size, data.end(), rom2.begin());
    rom_loaded_ = true;
}

void partner::debug_set_pc(uint16_t pc)
{
    // A debugger redirect represents the boundary immediately before the
    // selected instruction.  Build the Z80's overlapped M1/T1 fetch state
    // explicitly so the next four ticks execute a four-T-state NOP; leaving
    // z80_prefetch() at decoder step zero would expose its synthetic bootstrap
    // NOP as a one-tick instruction to steppers and cycle measurements.
    pins = z80_debug_prefetch(&cpu, pc, peek_mem(pc));
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
        // Keep exact emulated timing. The GUI resamples these zero-order-held
        // 8-bit DAC values to the host rate for live and recorded audio.
        static constexpr size_t max_pending_covox_samples = 262144;
        if (covox_sample_events_.size() == max_pending_covox_samples)
            covox_sample_events_.pop_front();
        covox_sample_events_.push_back({tick_count, port, data});
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

void partner::pulse_pio_output_ack(pio_port_id port)
{
    const int idx = pio_port_index(port);
    if (pio_device_cfg_[idx].kind == pio_device_kind::none)
        return;

    uint64_t bus = pio.pins;
    bus &= ~(Z80PIO_CE | Z80PIO_IORQ | Z80PIO_RD | Z80PIO_M1);
    if (port == pio_port_id::a) {
        bus |= Z80PIO_BSTB;
        bus &= ~Z80PIO_ASTB;        // active-low strobe assert
        bus = z80pio_tick(&pio, bus);
        bus |= Z80PIO_ASTB;         // deassert -> ready clears, interrupt requests
        bus = z80pio_tick(&pio, bus);
    } else {
        bus |= Z80PIO_ASTB;
        bus &= ~Z80PIO_BSTB;
        bus = z80pio_tick(&pio, bus);
        bus |= Z80PIO_BSTB;
        bus = z80pio_tick(&pio, bus);
    }
}

void partner::apply_sio_modem_inputs(uint64_t &bus_pins, sio_port_id port_a, sio_port_id port_b)
{
    auto eval_modem = [&](sio_port_id port) {
        const int idx = sio_port_index(port);
        const uint8_t dirty_bit = (uint8_t)(1u << idx);
        if ((sio_modem_dirty_mask_ & dirty_bit) == 0u) {
            const uint8_t cached = sio_modem_input_cache_[idx];
            return std::pair<bool, bool>{
                (cached & 0x01u) != 0u, (cached & 0x02u) != 0u
            };
        }
        if (sio_port_locked_[idx]) {
            // Internal hard-wired channels are always present/ready.
            sio_modem_input_cache_[idx] = 0x03u;
            sio_modem_dirty_mask_ &= (uint8_t)~dirty_bit;
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
        case sio_device_kind::internal_squid:
            // This is a direct virtual cable. It is present even before the
            // guest completes the Squid handshake. CTS is live backpressure
            // from the server's bounded input queue; the SIO's Auto Enables
            // mode prevents another guest byte from starting while it is low.
            cts = internal_squid_ == nullptr ||
                internal_squid_->can_receive_serial_byte();
            dcd = true;
            break;
        }
        sio_modem_input_cache_[idx] =
            (uint8_t)((cts ? 0x01u : 0u) | (dcd ? 0x02u : 0u));
        sio_modem_dirty_mask_ &= (uint8_t)~dirty_bit;
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

    const bool line_event = ch.line_tx_event_pending ||
                            ch.line_rx_event_pending;
    const bool mouse_device =
        cfg.kind == sio_device_kind::mouse_microsoft ||
        cfg.kind == sio_device_kind::mouse_mousesystems ||
        cfg.kind == sio_device_kind::mouse_logitech;
    /* A physical streaming mouse can overrun an unattended three-byte UART
       FIFO.  The virtual mouse instead keeps one complete wire stream pending
       and starts its next byte only after foreground mouse_poll() consumes the
       preceding byte.  This preserves packet boundaries without building an
       unbounded host-event queue while the guest is painting. */
    const bool mouse_byte_unread = mouse_device && ch.rx_fifo_count != 0;
    const bool can_start_rx = !rt.rx_fifo.empty() &&
                              !ch.line_rx_active &&
                              !mouse_byte_unread &&
                              z80sio_rx_enabled(chip, channel);
    const bool internal_poll_due =
        cfg.kind == sio_device_kind::internal_squid &&
        internal_squid_ != nullptr &&
        tick_count >= rt.next_internal_squid_poll_tick;
    /* Mouse and internal-cable adapters have nothing to do between complete
       serial characters (apart from the Squid server's scheduled poll).
       Avoid four million empty deque/event probes per guest second. TCP is
       excluded because accepting a new host connection is asynchronous. */
    if (cfg.kind != sio_device_kind::tcp_bridge && !line_event &&
        !can_start_rx && !internal_poll_due)
        return;

    if (cfg.kind == sio_device_kind::tcp_bridge) {
        const bool poll_due = tick_count >= rt.tcp.next_poll_tick;
        poll_tcp_bridge(port, ch);
        if (poll_due)
            sio_modem_dirty_mask_ |= (uint8_t)(1u << idx);
    }
    else if (cfg.kind == sio_device_kind::internal_squid &&
             internal_squid_ != nullptr &&
             tick_count >= rt.next_internal_squid_poll_tick)
    {
        internal_squid_->service(rt.rx_fifo, ch.rts);
        rt.next_internal_squid_poll_tick = tick_count + 2048;
        sio_modem_dirty_mask_ |= (uint8_t)(1u << idx);
    }

    uint8_t rx = 0;
    bool rx_accepted = false;
    if (z80sio_line_take_rx(chip, channel, &rx, &rx_accepted)) {
        if (rx_accepted)
            ++rt.rx_bytes;
        static const bool trace_sio_rx = [] {
            const char *s = std::getenv("IDP_TRACE_SIO");
            return s && s[0] && s[0] != '0';
        }();
        if (trace_sio_rx && port == sio_port_id::sio1_b)
            std::fprintf(stderr,
                "[sio] rx-line data=%02X accepted=%u enabled=%u overrun=%u\n",
                rx, rx_accepted ? 1u : 0u,
                z80sio_rx_enabled(chip, channel) ? 1u : 0u,
                ch.rx_overrun ? 1u : 0u);
    }

    uint8_t tx = 0;
    if (z80sio_line_take_tx(chip, channel, &tx))
    {
        static const bool trace_sio = [] {
            const char *s = std::getenv("IDP_TRACE_SIO");
            return s && s[0] && s[0] != '0';
        }();
        if (trace_sio && port == sio_port_id::sio1_b) {
            static uint8_t trace_frame[20];
            static size_t trace_frame_size = 0;
            if (tx == 0x7Eu)
                trace_frame_size = 0;
            if (trace_frame_size < sizeof(trace_frame))
                trace_frame[trace_frame_size++] = tx;
            if (trace_frame_size == sizeof(trace_frame)) {
                uint8_t hash = 0;
                for (size_t i = 1; i < 18; ++i)
                    hash ^= trace_frame[i];
                std::fprintf(stderr, "[sio] tx-frame %s",
                    trace_frame[18] == hash && trace_frame[19] == 0xD3u
                        ? "ok" : "INVALID");
                for (uint8_t value : trace_frame)
                    std::fprintf(stderr, " %02X", value);
                std::fprintf(stderr, "\n");
                trace_frame_size = 0;
            }
        }
        ++rt.tx_bytes;
        if (cfg.kind == sio_device_kind::tcp_bridge)
        {
            /* Bytes shifted while the cable is unplugged are lost on real
               hardware; never replay them into a later TCP connection. */
            if (rt.tcp.data_client_fd >= 0) {
                if (rt.tcp.data_tx_fifo.size() >= MAX_TCP_TX_FIFO_BYTES)
                    rt.tcp.data_tx_fifo.pop_front();
                rt.tcp.data_tx_fifo.push_back(tx);
            }
        }
        else if (cfg.kind == sio_device_kind::internal_squid)
        {
            if (internal_squid_ == nullptr)
                internal_squid_ = std::make_unique<internal_squid_server>();
            (void)internal_squid_->receive_serial_byte(tx);
            internal_squid_->service(rt.rx_fifo, ch.rts);
            rt.next_internal_squid_poll_tick = tick_count + 2048;
            sio_modem_dirty_mask_ |= (uint8_t)(1u << idx);
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

    if (rt.rx_fifo.empty() &&
        (cfg.kind == sio_device_kind::mouse_microsoft ||
         cfg.kind == sio_device_kind::mouse_mousesystems))
        (void)queue_pending_streaming_mouse_packet(port, false);

    if (rt.rx_fifo.empty())
        return;

    // An internal Squid cable observes the guest's RTS line. Bytes already
    // staged at the cable remain pending until the guest advertises room.
    if (cfg.kind == sio_device_kind::internal_squid && !ch.rts)
        return;
    // can_start_rx was calculated before a just-completed line character was
    // transferred into the SIO FIFO. Recheck the live FIFO here so that same
    // service pass cannot immediately launch a second polled-mouse byte.
    if (mouse_device && ch.rx_fifo_count != 0)
        return;

    const uint8_t data = rt.rx_fifo.front();
    if (!z80sio_line_receive(chip, channel, data, tick_count,
                             PARTNER_CPU_CLOCK_HZ, PARTNER_SIO_CLOCK_HZ))
        return;
    rt.rx_fifo.pop_front();
    if (rt.rx_fifo.empty() &&
        (cfg.kind == sio_device_kind::mouse_microsoft ||
         cfg.kind == sio_device_kind::mouse_mousesystems))
        (void)queue_pending_streaming_mouse_packet(port, false);
}

void partner::service_virtual_devices()
{
    if (!sio_service_requested_ && tick_count < sio_next_service_tick_)
        return;
    sio_service_requested_ = false;
    sio_next_service_tick_ = UINT64_MAX;

    struct line {
        sio_port_id port;
        z80sio_t *chip;
        int channel;
    };
    const line lines[] = {
        {sio_port_id::sio1_a, &sio, Z80SIO_CHANNEL_A},
        {sio_port_id::sio1_b, &sio, Z80SIO_CHANNEL_B},
        {sio_port_id::sio2_a, &sio2, Z80SIO_CHANNEL_A},
        {sio_port_id::sio2_b, &sio2, Z80SIO_CHANNEL_B},
    };
    for (const line &entry : lines) {
        if (z80sio_line_tx_busy(entry.chip, entry.channel) ||
            z80sio_line_rx_busy(entry.chip, entry.channel)) {
            z80sio_line_tick(entry.chip, entry.channel, tick_count,
                             PARTNER_CPU_CLOCK_HZ, PARTNER_SIO_CLOCK_HZ);
        }
        const int idx = sio_port_index(entry.port);
        if (sio_device_cfg_[idx].kind != sio_device_kind::none) {
            service_sio_device(entry.port, entry.chip, entry.channel);
        } else if (!sio_port_locked_[idx]) {
            uint8_t discarded = 0;
            (void)z80sio_line_take_tx(entry.chip, entry.channel, &discarded);
        }
    }

    /* Schedule the next character completion or host-adapter poll. New guest
       SIO writes and injected input set sio_service_requested_ explicitly, so
       quiescent channels cost one comparison per motherboard clock. */
    for (const line &entry : lines) {
        const z80sio_channel_t &channel = entry.chip->chn[entry.channel];
        if (channel.line_tx_active)
            sio_next_service_tick_ = std::min(
                sio_next_service_tick_, channel.line_tx_complete_tick);
        if (channel.line_rx_active)
            sio_next_service_tick_ = std::min(
                sio_next_service_tick_, channel.line_rx_complete_tick);
        if (channel.line_tx_event_pending || channel.line_rx_event_pending)
            sio_service_requested_ = true;

        const int idx = sio_port_index(entry.port);
        const auto kind = sio_device_cfg_[idx].kind;
        if (kind == sio_device_kind::internal_squid && internal_squid_ != nullptr)
            sio_next_service_tick_ = std::min(
                sio_next_service_tick_,
                sio_device_runtime_[idx].next_internal_squid_poll_tick);
        else if (kind == sio_device_kind::tcp_bridge)
            sio_next_service_tick_ = std::min(
                sio_next_service_tick_,
                sio_device_runtime_[idx].tcp.next_poll_tick);
    }
}

void partner::seed_cmos_nvram(const uint8_t *data, size_t len)
{
    if (!data || len == 0)
        return;
    const size_t n = std::min(len, (size_t)8);
    for (size_t i = 0; i < n; i++)
        rtc.regs[0x08 + i] = data[i];
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

    std::ifstream file;
    if (!rtc_nvram_path_.empty())
        file.open(rtc_nvram_path_, std::ios::binary);
    if (file)
    {
        uint8_t nvram[8]{};
        file.read(reinterpret_cast<char*>(nvram), sizeof(nvram));
        if (file.gcount() == (std::streamsize)sizeof(nvram))
        {
            for (size_t i = 0; i < sizeof(nvram); i++)
                rtc.regs[0x08 + i] = nvram[i];
            return;
        }
    }

    // No valid persisted CMOS yet: write the current seeded defaults once.
    apply_safe_defaults();
    save_rtc_nvram();
}

void partner::save_rtc_nvram() const
{
    if (rtc_nvram_path_.empty())
        return;
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
    if (!file)
        throw std::runtime_error("incomplete disk image: " + path);
    disk.path = path;

    // Auto-detect geometry from file size
    constexpr uint64_t FDD_77DS_SIZE = 77ULL * 2 * 18 * 256;   // 709,632 bytes
    constexpr uint64_t FDD_80DS_SIZE = 80ULL * 2 * 18 * 256;   // 737,280 bytes
    if ((size == FDD_77DS_SIZE) || (size == FDD_80DS_SIZE)) {
        disk.seclen = 256; disk.sectrk = 18; disk.heads = 2;
        std::cerr << "[info] disk " << drive << " (FDD): " << path << "\n";
    } else if ((size % (36ULL * 256ULL)) == 0) {
        disk.seclen = 256; disk.sectrk = 18; disk.heads = 2;
        std::cerr << "[info] disk " << drive << " (FDD, inferred double-sided): " << path << "\n";
    } else {
        disk.seclen = 256; disk.sectrk = 18; disk.heads = 1;
        std::cerr << "[warning] disk " << drive << " unknown size " << size
                  << ", assuming single-sided FDD geometry: " << path << "\n";
    }

    i8272_set_drive_ready(&fdc, drive, true);
    i8272_set_drive_media(&fdc, drive, disk.heads > 1, false, 1);
}

std::string partner::get_disk_path(int drive) const
{
    if (drive < 0 || drive >= I8272_MAX_DRIVES)
        return {};
    return disks_[drive].path;
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
    hdd_.path = path;

    s1410_set_present(&hdc, true);
    std::cerr << "[info] hard disk loaded: " << path << "\n";
}

bool partner::persist_disk_bytes(disk_image &disk, uint64_t offset,
                                 const uint8_t *src, size_t size)
{
    if ((src == nullptr) || (offset + size > disk.data.size()))
        return false;

    if (!disk.path.empty())
    {
        std::fstream file(disk.path, std::ios::binary | std::ios::in | std::ios::out);
        if (!file)
            return false;
        file.seekp((std::streamoff)offset, std::ios::beg);
        if (!file)
            return false;
        file.write(reinterpret_cast<const char *>(src), (std::streamsize)size);
        if (!file)
            return false;
        file.flush();
        if (!file)
            return false;
    }

    memcpy(disk.data.data() + offset, src, size);
    return true;
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

bool partner::write_sector_cb(int drive, int c, int h, int r, int n,
                               const uint8_t *buf, void *user)
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
    auto &disk = self->disks_[resolved_drive];
    if (disk.data.empty()) return false;

    uint16_t sector_size = (n == 0) ? 128 : (128 << n);
    if (sector_size > I8272_SECTOR_SIZE) sector_size = I8272_SECTOR_SIZE;

    if ((r <= 0) || ((uint32_t)r > disk.sectrk) || ((uint32_t)h >= disk.heads)) {
        return false;
    }

    uint32_t lba = (((uint32_t)c * disk.heads) + (uint32_t)h) * disk.sectrk
                 + (uint32_t)(r - 1);
    uint64_t offset = (uint64_t)lba * disk.seclen;

    return self->persist_disk_bytes(disk, offset, buf, sector_size);
}

bool partner::read_hdd_blocks_cb(uint32_t lba, uint32_t count, uint8_t *buf, void *user)
{
    auto *self = static_cast<partner *>(user);
    const auto &disk = self->hdd_;
    static const bool trace_hd = [] {
        const char *s = std::getenv("IDP_TRACE_HD");
        return s && s[0] && s[0] != '0';
    }();
    if (disk.data.empty()) {
        if (trace_hd)
            std::fprintf(stderr, "[hd] read_blocks lba=%u count=%u FAIL empty\n", lba, count);
        return false;
    }

    const uint64_t offset = (uint64_t)lba * disk.seclen;
    const uint64_t bytes = (uint64_t)count * disk.seclen;
    if (offset + bytes > disk.data.size()) {
        if (trace_hd)
            std::fprintf(stderr, "[hd] read_blocks lba=%u count=%u FAIL range\n", lba, count);
        return false;
    }

    memcpy(buf, disk.data.data() + offset, (size_t)bytes);
    if (trace_hd)
        std::fprintf(stderr, "[hd] read_blocks lba=%u count=%u OK bytes=%llu\n",
            lba, count, (unsigned long long)bytes);
    return true;
}

bool partner::write_hdd_blocks_cb(uint32_t lba, uint32_t count, const uint8_t *buf, void *user)
{
    auto *self = static_cast<partner *>(user);
    auto &disk = self->hdd_;
    if (disk.data.empty()) return false;

    const uint64_t offset = (uint64_t)lba * disk.seclen;
    const uint64_t bytes = (uint64_t)count * disk.seclen;
    if (offset + bytes > disk.data.size()) return false;

    return self->persist_disk_bytes(disk, offset, buf, (size_t)bytes);
}

void partner::load_rom(const std::string &path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("cannot open rom file: " + path);
    std::vector<uint8_t> image(
        (std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (image.size() != rom_size && image.size() != rom_capacity)
        throw std::runtime_error("Partner ROM image must be exactly 2048 or 4096 bytes: " + path);
    load_debug_rom(image);
    rom_path_ = path;

    std::cerr << "[info] rom loaded: " << path << "\n";
}

bool partner::uses_partos_cmos_layout() const
{
    std::string rom_name = std::filesystem::path(rom_path_).filename().string();
    std::transform(rom_name.begin(), rom_name.end(), rom_name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return rom_name == "partos.rom";
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
    // Derive the BIOS-visible NVRAM config from the mounted hardware: clear the
    // FD-type bits for empty drive slots and stamp a valid checksum nibble. Only
    // when a boot ROM is present -- a bare NVRAM round-trip (no ROM) must read
    // back exactly what was written, so leave the shadow bytes untouched then.
    if (rom_loaded_ && uses_partos_cmos_layout()) {
        rtc.regs[0x0F] &= 0xF0;
        if (disks_[0].data.empty())
            rtc.regs[0x09] &= (uint8_t)~PARTNER_FD0_TYPE_MASK;
        if (disks_[1].data.empty())
            rtc.regs[0x09] &= (uint8_t)~PARTNER_FD1_TYPE_MASK;
        if (disks_[2].data.empty())
            rtc.regs[0x09] &= (uint8_t)~PARTNER_FD2_TYPE_MASK;
        if (disks_[3].data.empty())
            rtc.regs[0x09] &= (uint8_t)~PARTNER_FD3_TYPE_MASK;
        stamp_rtc_nvram_checksum();
    }

    rom_enabled = true;
    ram_bank = 1;
    fdc_int_vector = 0;
    fdc_motor = 0;
    fdc_int_state = 0;
    fdc_irq_level_ = false;
    fdc_reset_irq_armed_ = false;
    prompt_fdc_cleanup_done_ = false;
    fdc_motor_running = false;
    dma_busreq_latched = false;
    dma_ready_input_ = false;
    dma_cpu_yield_active_ = false;
    dma_cpu_yield_left_boundary_ = false;
    dma_cpu_suspended_ = false;
    cpu_resume_pins_ = pins;
    dma_fdc_reads_ = 0;
    dma_mem_writes_ = 0;
    dma_port_writes_ = 0;
    last_dma_port_wr_tick_ = 0;
    fd_read_real_calls_ = 0;
    dma_enabled_ticks_ = 0;
    dma_commit_reads_ = 0;
    dma_commit_eligible_ = 0;
    sasi_data_reads_ = 0;
    sasi_data_writes_ = 0;
    sasi_data_phase_reads_ = 0;
    dma_bus_service_ = false;
    last_cpu_bus_pins_ = 0;
    io_read_latched_ = false;
    io_read_latched_addr_ = 0;
    io_read_latched_data_ = 0xFF;
    tick_count = 0;
    rtc_host_sync_divider_ = 0;
    mm58167a_sync_time(&rtc);
    dbg_im2_ack_vectors.fill(0);
    dbg_im2_ack_pcs.fill(0);
    dbg_im2_ack_count = 0;
    im2_ack_latched_ = false;
    im2_ack_latched_vector_ = -1;
    im2_ack_external_latched_ = false;
    external_im2_pending_vector_ = -1;
    external_im2_edge_armed_ = true;
    dbg_irref_values.fill(0);
    dbg_irref_pcs.fill(0);
    dbg_irref_sps.fill(0);
    dbg_irref_stack0.fill(0);
    dbg_irref_stack1.fill(0);
    dbg_irref_ticks.fill(0);
    dbg_irref_count = 0;
    restore_drive_ready_flags();

    for (sio_port_id port : { sio_port_id::sio1_a, sio_port_id::sio1_b, sio_port_id::sio2_a, sio_port_id::sio2_b })
        reset_sio_device_runtime(port);
    sio_modem_dirty_mask_ = 0x0Fu;
    sio_service_requested_ = true;
    sio_next_service_tick_ = 0;
    for (auto &rt : pio_device_runtime_) {
        rt.last_output = 0;
        rt.covox_level = 0.0f;
        rt.bytes_seen = 0;
    }
    covox_sample_events_.clear();
    virtual_printer_text_.clear();
}

void partner::stamp_rtc_nvram_checksum()
{
    rtc.regs[0x0F] &= 0xF0;
    uint8_t nibble_sum = 0;
    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t value = rtc.regs[0x08 + i];
        nibble_sum = (uint8_t)((nibble_sum + (value & 0x0F)) & 0x0F);
        nibble_sum = (uint8_t)(
            (nibble_sum + ((value >> 4) & 0x0F)) & 0x0F);
    }
    rtc.regs[0x0F] |= (uint8_t)((-nibble_sum) & 0x0F);
}

void partner::tick()
{
    tick_count++;
    const bool fdc_irq_before = fdc.irq_request;
    i8272_tick(&fdc);
    if (fdc_trace_enabled() && !fdc_irq_before && fdc.irq_request) {
        std::fprintf(stderr,
            "[fdc] tick=%llu irq vector=%02X phase=%u cmd=%02X data=%u/%u result=%u/%u\n",
            (unsigned long long)tick_count, fdc_int_vector, (unsigned)fdc.phase,
            fdc.cmd_code, fdc.data_idx, fdc.data_len,
            fdc.result_idx, fdc.result_len);
    }
    // Closing optional link JJ12 connects the MM58167's active-high interrupt,
    // through the board inverter, to the CPU's active-low NMI input. Standard
    // Partner configurations leave it open; their CP/M 0066h vector is not an
    // RTC handler. CHIPS pin masks represent assertion rather than electrical
    // polarity, so reflect a fitted link as Z80_NMI.
    bool rtc_sync_due = false;
    if (rtc.det_ticks != nullptr) {
        const uint64_t ticks_per_millisecond =
            std::max<uint64_t>(1u, rtc.det_hz / 1000u);
        rtc_sync_due = (tick_count % ticks_per_millisecond) == 0u;
    } else {
        ++rtc_host_sync_divider_;
        if (rtc_host_sync_divider_ >= 1024u) {
            rtc_host_sync_divider_ = 0;
            rtc_sync_due = true;
        }
    }
    const uint64_t rtc_pins = mm58167a_tick_idle(
        &rtc, mm58167a_bus_idle(), rtc_sync_due);
    if (rtc_nmi_enabled_ && (rtc_pins & MM58167A_INT))
        pins |= Z80_NMI;
    else
        pins &= ~Z80_NMI;
    const uint64_t prev_pins = pins;

    const bool cpu_ticked = !dma_owns_bus();
    if (cpu_ticked && dma_cpu_suspended_) {
        /* DMA bus cycles reuse the shared address/data/control pin mask. The
           real CPU retains its unfinished cycle internally while BUSACK is
           active, so restore the last CPU-side pins before resuming it. */
        const uint64_t asynchronous = pins & (Z80_INT | Z80_NMI);
        pins = (cpu_resume_pins_ & ~(Z80_INT | Z80_NMI)) | asynchronous;
        dma_cpu_suspended_ = false;
    } else if (!cpu_ticked) {
        dma_cpu_suspended_ = true;
    }
    if (cpu_ticked)
    {
        // IM2 ack data is sampled by the CPU during internal step 1657.
        // Present the highest-priority interrupt vector on the bus one tick
        // earlier so the sample sees the intended byte instead of stale bus
        // residue. This is required both for the external FDC vector latch
        // and for daisy-chain devices such as the SIO keyboard interrupt.
        if (cpu.step == 1657)
        {
            const int ack_vector = select_im2_ack_vector();

            static const bool trace_int = [] {
                const char *s = std::getenv("IDP_TRACE_INT");
                return s && s[0] && s[0] != '0';
            }();
            if (trace_int) {
                std::fprintf(stderr, "[int] ack vec=%02X pc=%04X\n",
                    ack_vector, cpu.pc);
            }
            dbg_im2_ack_vectors[dbg_im2_ack_count & 0x7u] = (uint8_t)ack_vector;
            dbg_im2_ack_pcs[dbg_im2_ack_count & 0x7u] = cpu.pc;
            dbg_im2_ack_count++;
            Z80_SET_DATA(pins, (uint8_t)ack_vector);
            pins |= Z80_IORQ;
        }

        // Tick the CPU
        pins = z80_tick(&cpu, pins);
        if (cpu.pc == 0xD4D1) {
            static const bool trace_hd = [] {
                const char *s = std::getenv("IDP_TRACE_HD");
                return s && s[0] && s[0] != '0';
            }();
            if (trace_hd)
                std::fprintf(stderr, "[hd] io_ptr_store de=%04X bc=%04X\n",
                    cpu.de, cpu.bc);
        }
        if (cpu.pc == 0xD1BA && cpu.bc == 0x0100)
            ++fd_read_real_calls_;

        // This z80 core enters the interrupt acknowledge sample microsteps
        // without always exposing a clean acknowledge bus cycle. Reconstruct
        // the complete cycle so service_cpu_bus() and the daisy-chain devices
        // latch the vector before they move REQUESTED to SERVICED. In
        // particular, a stale MREQ from the preceding opcode fetch must not
        // make service_cpu_bus() mistake the acknowledge for a memory read.
        if ((cpu.step == 1638) || (cpu.step == 1644) || (cpu.step == 1657)) {
            pins &= ~(Z80_MREQ | Z80_RD | Z80_WR);
            pins |= Z80_M1 | Z80_IORQ;
        }

        service_cpu_bus(pins);

        /* Z8410 byte mode releases BUSREQ after every byte. The CPU core has
           no BUSREQ/BUSACK pins of its own, so let it run through an actual
           instruction boundary before granting the DMA again. Merely waiting
           four host ticks can freeze the core in the middle of a bus cycle and
           later resume it on DMA-owned address/data pins. */
        if (dma_cpu_yield_active_) {
            if (!z80_opdone(&cpu)) {
                dma_cpu_yield_left_boundary_ = true;
            } else if (dma_cpu_yield_left_boundary_) {
                dma_cpu_yield_active_ = false;
            }
        }

        // The DMA register port is programmed by CPU I/O instructions, but the
        // DMA core itself is level-sensitive and would otherwise decode the
        // same byte across multiple T-states. Handle port 0xC0 explicitly on
        // a read/write edge and leave z80dma_tick() to bus-master transfers
        // and interrupt handling.
        const uint16_t cpu_addr = Z80_GET_ADDR(pins);
        const uint8_t cpu_port = cpu_addr & 0xFF;
        const uint16_t prev_cpu_addr = Z80_GET_ADDR(prev_pins);
        const uint8_t prev_cpu_port = prev_cpu_addr & 0xFF;
        const bool dma_wr_now = partner_dma_port(cpu_port) &&
            ((pins & (Z80_IORQ | Z80_WR)) == (Z80_IORQ | Z80_WR));
        const bool dma_wr_prev = partner_dma_port(prev_cpu_port) &&
            ((prev_pins & (Z80_IORQ | Z80_WR)) == (Z80_IORQ | Z80_WR));
        const bool dma_rd_now = partner_dma_port(cpu_port) &&
            ((pins & (Z80_IORQ | Z80_RD)) == (Z80_IORQ | Z80_RD));
        const bool dma_rd_prev = partner_dma_port(prev_cpu_port) &&
            ((prev_pins & (Z80_IORQ | Z80_RD)) == (Z80_IORQ | Z80_RD));

        if (dma_wr_now && !dma_wr_prev)
            service_cpu_dma_port_write(pins);
        if (dma_rd_now && !dma_rd_prev)
            Z80_SET_DATA(pins, z80dma_read(&dma));

        // Preserve CPU-owned bus state before CTC/DMA/peripheral ticks reuse
        // the shared pin mask. This is the state the CPU sees after BUSACK.
        cpu_resume_pins_ = pins;
    }

    // Tick peripheral chips in interrupt daisy chain order
    // Rebuild the shared INT line from the current peripheral state each tick
    // instead of letting an old external request linger in `pins`.
    pins &= ~Z80_INT;
    // Set IEIO to enable interrupt daisy chain
    pins |= Z80_IEIO;

    // Highest priority: CTC (ports 0xC8-0xCF). The MC14411's default F13
    // output is a 1600 Hz square wave at channel 0; ZC/TO0 cascades into
    // channel 1 for the floppy-motor timeout.
    pins &= ~(Z80CTC_CE | Z80CTC_CS0 | Z80CTC_CS1 |
              Z80CTC_CLKTRG0 | Z80CTC_CLKTRG1 | Z80CTC_CLKTRG2 | Z80CTC_CLKTRG3);
    if (((tick_count / PARTNER_CTC_XX1_HALF_PERIOD_TICKS) & 1u) != 0)
        pins |= Z80CTC_CLKTRG0;
    if (ctc.pins & Z80CTC_ZCTO0)
        pins |= Z80CTC_CLKTRG1;
    if (get_ctc3_trigger_edge())
        pins |= Z80CTC_CLKTRG3;
    pins = z80ctc_tick(&ctc, pins);
    pins &= ~(Z80CTC_CE | Z80CTC_CS0 | Z80CTC_CS1 |
              Z80CTC_CLKTRG0 | Z80CTC_CLKTRG1 | Z80CTC_CLKTRG2 | Z80CTC_CLKTRG3);
    if (pins & Z80CTC_ZCTO1)
    {
        fdc_motor_running = false;
        fdc_motor = 0;
        for (int drive = 0; drive < I8272_MAX_DRIVES; ++drive)
            i8272_set_drive_motor(&fdc, drive, false);
    }

    // Second priority: DMA (ports 0xC0-0xC7)
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
            // Read direction (device -> RAM): the source is an I/O port, so
            // wait until the source device actually has a byte to hand over.
            const uint8_t src_port = (uint8_t)(src.address & 0xFF);
            if (dma.state == Z80DMA_STATE_WRITE) {
                // Once a byte has been latched, keep RDY asserted long enough
                // for the matching write-back cycle to complete.
                ready = true;
            } else if (partner_fdc_port(src_port) && (src_port & 1u)) {
                ready = i8272_drq(&fdc);
            } else if (partner_sasi_port(src_port) && partner_sasi_function(src_port) == 1) {
                ready = idpartner_sasi_drq(&sasi_);
            }
        }
        else
        {
            // Write direction (RAM -> device): the source is memory (always
            // ready), so gate on whether the destination I/O device can accept
            // a byte yet. This makes DMA writes to the FDC/SASI work.
            const z80dma_port_t &dst = dma.direction_ab ? dma.port_b : dma.port_a;
            if (!dst.is_memory)
            {
                const uint8_t dst_port = (uint8_t)(dst.address & 0xFF);
                if (dma.state == Z80DMA_STATE_WRITE) {
                    ready = true;
                } else if (partner_fdc_port(dst_port) && (dst_port & 1u)) {
                    ready = i8272_drq(&fdc);
                } else if (partner_sasi_port(dst_port) && partner_sasi_function(dst_port) == 1) {
                    ready = idpartner_sasi_drq(&sasi_);
                }
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
    pins = z80dma_tick(&dma, pins);
    if (dma_state_before == Z80DMA_STATE_WRITE &&
        dma.mode == Z80DMA_MODE_BYTE && dma.byte_gap_ticks != 0) {
        dma_cpu_yield_active_ = true;
        dma_cpu_yield_left_boundary_ = false;
    }
    if (dma.enabled && (pins & Z80DMA_BUSACK) && (pins & Z80DMA_RD))
        service_dma_read_bus(pins);
    dma_busreq_latched = (pins & Z80DMA_BUSREQ) != 0;
    if ((dma_state_before == Z80DMA_STATE_WRITE || dma.state == Z80DMA_STATE_WRITE) &&
        (pins & Z80DMA_WR))
        service_dma_write_bus(pins);
    pins &= ~Z80DMA_CE;

    // Third priority: first SIO chip (ports 0xD8-0xDF). CPU reads and writes
    // are both decoded exactly once by service_cpu_bus() through io_read() or
    // io_write(). Keep CE low here: a second level-sensitive decode would
    // consume RX data twice and turn a selected RR1/RR2 read into RR0.
    apply_sio_modem_inputs(pins, sio_port_id::sio1_a, sio_port_id::sio1_b);
    pins = z80sio_tick_idle(&sio, pins);
    pins &= ~(Z80SIO_CE | Z80SIO_CS_A | Z80SIO_CS_B);

    // Fourth priority: second SIO chip (ports 0xE0-0xE7).
    apply_sio_modem_inputs(pins, sio_port_id::sio2_a, sio_port_id::sio2_b);
    pins = z80sio_tick_idle(&sio2, pins);
    pins &= ~(Z80SIO_CE | Z80SIO_CS_A | Z80SIO_CS_B);

    // Fifth priority: PIO (ports 0xD0-0xD7). As with the CTC and SIO, its
    // selected CPU access already ran through io_read()/io_write(); keep CE
    // low here so DMA addresses cannot become phantom PIO accesses.
    pins &= ~(Z80PIO_CE | Z80PIO_CDSEL | Z80PIO_BASEL);
    // Virtual attachments generate explicit active-low strobe pulses; both
    // handshake inputs otherwise remain at their inactive high level.
    pins |= Z80PIO_ASTB | Z80PIO_BSTB;
    // The CPU core exposes the leading M1 phase before RD/MREQ settles. The
    // standalone PIO model interprets M1 without RD/IORQ as its reset
    // sequence, which would otherwise reset both ports after every Z80
    // instruction. Machine reset already calls z80pio_reset() explicitly;
    // keep M1 only for a real IORQ interrupt-acknowledge cycle here.
    const bool masked_transient_pio_m1 =
        (pins & Z80PIO_M1) && !(pins & Z80PIO_IORQ) && !(pins & Z80PIO_RD);
    if (masked_transient_pio_m1)
        pins &= ~Z80PIO_M1;
    pins = z80pio_tick(&pio, pins);
    if (masked_transient_pio_m1)
        pins |= Z80PIO_M1;
    pins &= ~(Z80PIO_CE | Z80PIO_CDSEL | Z80PIO_BASEL);

    // Discrete FDC daisy logic follows the motherboard PIO. The expansion
    // connector, including the GDP card's local PIO, is last.
    service_fdc_daisy(pins, cpu_ticked);
    pins = clock_expansion_daisy_chain(pins);

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

    if (dma.enabled)
        dma_enabled_ticks_++;

    service_virtual_devices();

    /*
        The GDP board presents vertical blank as an external interrupt pulse.
        Latch each rising edge until the CPU acknowledges it, then require the
        source to go low before it can interrupt again. Driving INT directly
        from the 64-tick live level lets RETI re-enable interrupts while that
        level is still high, producing an interrupt storm from one vertical
        blank and occasionally waking an FDC EI/HALT wait with the wrong
        vector.
    */
    const int external_im2_vector = get_external_im2_vector();
    if (external_im2_vector < 0) {
        external_im2_edge_armed_ = true;
    } else {
        if (external_im2_edge_armed_ &&
            external_im2_pending_vector_ < 0) {
            external_im2_pending_vector_ = external_im2_vector;
        }
        external_im2_edge_armed_ = false;
    }
    if (external_im2_pending_vector_ >= 0)
        pins |= Z80_INT;

}

void partner::restore_drive_ready_flags()
{
    for (int i = 0; i < I8272_MAX_DRIVES; i++) {
        i8272_set_drive_ready(&fdc, i, !disks_[i].data.empty());
        i8272_set_drive_media(&fdc, i, disks_[i].heads > 1, false, 1);
    }
    if (disks_[1].data.empty() &&
        !disks_[0].data.empty() &&
        disks_[2].data.empty() &&
        disks_[3].data.empty())
    {
        i8272_set_drive_ready(&fdc, 1, true);
        i8272_set_drive_media(&fdc, 1, disks_[0].heads > 1, false, 1);
    }
}

uint64_t partner::clock_zilog_daisy_chain(uint64_t bus_pins)
{
    /*
        The chips, not the motherboard, arbitrate Zilog interrupts.  IEI starts
        high at the CTC and ripples through the physical priority order. A
        chip with a request consumes IEIO and, during M1|IORQ, drives its own
        vector onto D0..D7.  Keeping this as a pin transaction is important:
        deriving a vector by examining controller structs duplicates each
        chip's enable, cause and in-service rules in the board model.
    */
    bus_pins |= Z80_IEIO;
    bus_pins = z80ctc_daisychain(&ctc, bus_pins);
    bus_pins = z80dma_daisychain(&dma, bus_pins);
    bus_pins = z80sio_daisychain(&sio, bus_pins);
    bus_pins = z80sio_daisychain(&sio2, bus_pins);
    bus_pins = z80pio_daisychain(&pio, bus_pins);
    service_fdc_daisy(bus_pins, false);
    bus_pins = clock_expansion_daisy_chain(bus_pins);
    return bus_pins;
}

int partner::select_im2_ack_vector()
{
    if (im2_ack_latched_)
        return im2_ack_latched_vector_;

    /* An unclaimed acknowledge sees the board's spurious-vector bus value.
       Every real vector is driven by the claiming chip below. */
    const int fallback_vector = PARTNER_SPURIOUS_VECTOR;
    uint64_t ack_pins = 0;
    Z80_SET_DATA(ack_pins, (uint8_t)fallback_vector);
    ack_pins |= Z80_M1 | Z80_IORQ;
    ack_pins = clock_zilog_daisy_chain(ack_pins);

    const bool board_claimed = (ack_pins & Z80_IEIO) == 0;
    int ack_vector = board_claimed ? (int)Z80_GET_DATA(ack_pins) : -1;
    bool external_ack = false;

    if (!board_claimed && external_im2_pending_vector_ >= 0) {
        ack_vector = external_im2_pending_vector_;
        external_ack = true;
    }

    if (ack_vector < 0)
        ack_vector = fallback_vector;

    im2_ack_latched_ = true;
    im2_ack_latched_vector_ = ack_vector;
    im2_ack_external_latched_ = external_ack;
    return ack_vector;
}

void partner::service_cpu_dma_port_write(uint64_t bus_pins)
{
    if ((bus_pins & (Z80_IORQ | Z80_WR)) != (Z80_IORQ | Z80_WR))
        return;
    if (!partner_dma_port((uint8_t)Z80_GET_ADDR(bus_pins)))
        return;
    if (last_dma_port_wr_tick_ == tick_count)
        return;
    last_dma_port_wr_tick_ = tick_count;
    dma_port_writes_++;
    const uint8_t data = Z80_GET_DATA(bus_pins);
    z80dma_write(&dma, data);
    static const bool trace_dma = [] {
        const char *s = std::getenv("IDP_TRACE_DMA");
        return s && s[0] && s[0] != '0';
    }();
    if (trace_dma) {
        std::fprintf(stderr,
            "[dma] tick=%llu pc=%04X wr=%02X vec=%02X int_en=%d int_state=%02X enabled=%d "
            "compat=%u state=%u a=%04X/%04X b=%04X/%04X\n",
            (unsigned long long)tick_count,
            cpu.pc,
            data,
            dma.int_vector,
            dma.interrupt_enable ? 1 : 0,
            dma.int_state,
            dma.enabled ? 1 : 0,
            dma.compat_state,
            (unsigned)dma.state,
            dma.port_a.address,
            dma.port_a.block_length,
            dma.port_b.address,
            dma.port_b.block_length);
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
        const uint16_t prev_addr = Z80_GET_ADDR(last_cpu_bus_pins_);
        const bool same_port = ((prev_addr & 0xFF) == (addr & 0xFF));
        const uint8_t cur_port = (uint8_t)(addr & 0xFF);
        if (bus_pins & Z80_RD)
        {
            if (!io_rd_prev || !same_port || !io_read_latched_)
            {
                io_read_latched_data_ = io_read(addr & 0xFF);
                io_read_latched_addr_ = addr & 0xFF;
                io_read_latched_ = true;
            }
            Z80_SET_DATA(bus_pins, io_read_latched_data_);
        }
        else if (bus_pins & Z80_WR)
        {
            const bool io_wr_active =
                (bus_pins & (Z80_IORQ | Z80_WR)) == (Z80_IORQ | Z80_WR);
            const bool io_wr_rising =
                io_wr_active &&
                ((last_cpu_bus_pins_ & (Z80_IORQ | Z80_WR)) != (Z80_IORQ | Z80_WR));
            if (partner_dma_port(cur_port)) {
                if (io_wr_rising)
                    service_cpu_dma_port_write(bus_pins);
            } else {
                /* z80_tick() presents an I/O write for one service tick.  The
                   peripheral pass below no longer decodes CPU writes, so
                   dispatch it exactly here.  Comparing against the previous
                   serviced bus loses legitimate back-to-back OUT operations
                   (notably the SIO register/value initialization pairs). */
                io_write(addr & 0xFF, Z80_GET_DATA(bus_pins));
            }
        }

        if (!io_rd_now || !same_port)
            io_read_latched_ = false;
    }
    else if ((bus_pins & (Z80_IORQ | Z80_M1)) == (Z80_IORQ | Z80_M1))
    {
        const int ack_vector = select_im2_ack_vector();

        static int last_fdc_ack_vector = -1;
        static uint32_t fdc_ack_trace_count = 0;
        if (fdc_trace_enabled() && fdc.irq_request &&
            (fdc_ack_trace_count < 32u || ack_vector != last_fdc_ack_vector)) {
            std::fprintf(stderr,
                "[fdc] ack tick=%llu pc=%04X selected=%02X wanted=%02X "
                "dma=%02X ctc=%02X/%02X/%02X/%02X "
                "ctc2ctl=%02X const=%02X count=%02X "
                "sio=%02X/%02X sio2=%02X/%02X pio=%02X/%02X\n",
                (unsigned long long)tick_count, cpu.pc, ack_vector,
                fdc_int_vector, dma.int_state,
                ctc.chn[0].int_state, ctc.chn[1].int_state,
                ctc.chn[2].int_state, ctc.chn[3].int_state,
                ctc.chn[2].control, ctc.chn[2].constant,
                ctc.chn[2].down_counter,
                sio.chn[0].int_state, sio.chn[1].int_state,
                sio2.chn[0].int_state, sio2.chn[1].int_state,
                pio.port[0].int_state, pio.port[1].int_state);
            ++fdc_ack_trace_count;
            last_fdc_ack_vector = ack_vector;
        }

        dbg_im2_ack_vectors[dbg_im2_ack_count & 0x7u] = (uint8_t)ack_vector;
        dbg_im2_ack_pcs[dbg_im2_ack_count & 0x7u] = cpu.pc;
        dbg_im2_ack_count++;
        Z80_SET_DATA(bus_pins, (uint8_t)ack_vector);
        if (im2_ack_external_latched_ &&
            ack_vector == external_im2_pending_vector_) {
            external_im2_pending_vector_ = -1;
        }
    }
    if ((bus_pins & (Z80_IORQ | Z80_M1)) != (Z80_IORQ | Z80_M1)) {
        im2_ack_latched_ = false;
        im2_ack_latched_vector_ = -1;
        im2_ack_external_latched_ = false;
    }

    last_cpu_bus_pins_ = bus_pins;
}

uint8_t partner::sasi_data_read_for_bus()
{
    sasi_data_reads_++;
    const bool was_data = (hdc.phase == S1410_PHASE_READ_DATA);
    const uint8_t data = idpartner_sasi_data_r(&sasi_);
    if (was_data && hdc.data_idx > 0)
        sasi_data_phase_reads_++;
    return data;
}

void partner::service_dma_read_bus(uint64_t &bus_pins)
{
    if (!(bus_pins & Z80DMA_BUSACK) || !(bus_pins & Z80DMA_RD))
        return;

    const uint16_t src_addr = Z80DMA_GET_ADDR(bus_pins);
    uint8_t data = 0xFF;
    dma_bus_service_ = true;
    if (bus_pins & Z80DMA_MREQ)
        data = read_mem(src_addr);
    else if ((bus_pins & Z80DMA_IORQ) &&
             partner_sasi_port((uint8_t)src_addr) &&
             partner_sasi_function((uint8_t)src_addr) == 1)
        data = sasi_data_read_for_bus();
    else if (bus_pins & Z80DMA_IORQ) {
        if (partner_fdc_port((uint8_t)src_addr) && (src_addr & 1u) &&
            dma.bytes_remaining == 1)
            i8272_terminal_count(&fdc);
        data = io_read((uint8_t)src_addr);
        if (partner_fdc_port((uint8_t)src_addr) && (src_addr & 1u))
            dma_fdc_reads_++;
    }
    dma_bus_service_ = false;
    Z80DMA_SET_DATA(bus_pins, data);
}

void partner::service_dma_write_bus(uint64_t &bus_pins)
{
    if (!(bus_pins & Z80DMA_BUSACK) || !(bus_pins & Z80DMA_WR))
        return;

    const uint16_t addr = Z80DMA_GET_ADDR(bus_pins);
    const uint8_t data = Z80DMA_GET_DATA(bus_pins);
    static const bool trace_hd = [] {
        const char *s = std::getenv("IDP_TRACE_HD");
        return s && s[0] && s[0] != '0';
    }();
    if (trace_hd && (bus_pins & Z80DMA_IORQ))
        std::fprintf(stderr, "[hd] dma_wr port=%04X data=%02X dir_ab=%d\n",
            addr, data, dma.direction_ab ? 1 : 0);
    if (bus_pins & Z80DMA_MREQ) {
        write_mem(addr, data);
        dma_mem_writes_++;
    }
    else if (bus_pins & Z80DMA_IORQ) {
        if (partner_fdc_port((uint8_t)addr) && (addr & 1u) &&
            dma.bytes_remaining == 0)
            i8272_terminal_count(&fdc);
        io_write(addr & 0xFF, data);
    }
}

void partner::service_fdc_daisy(uint64_t &bus_pins, bool cpu_ticked)
{
    (void)cpu_ticked;
    const bool irq_level = (i8272_irq_pins(&fdc, 0) & I8272_IRQ) != 0;
    if (irq_level && !fdc_irq_level_)
        fdc_int_state |= FDC_INT_NEEDED;
    fdc_irq_level_ = irq_level;

    // A higher-priority device has blocked IEI, so the FDC latch neither
    // requests the CPU nor consumes a downstream RETI.
    if ((bus_pins & Z80_IEIO) == 0)
        return;

    if ((bus_pins & Z80_RETI) && (fdc_int_state & FDC_INT_SERVICED)) {
        fdc_int_state &= (uint8_t)~FDC_INT_SERVICED;
        bus_pins &= ~Z80_RETI;
    }

    if (fdc_int_state & FDC_INT_SERVICED) {
        bus_pins &= ~Z80_IEIO;
        return;
    }

    if (fdc_int_state & FDC_INT_NEEDED) {
        fdc_int_state = (uint8_t)(
            (fdc_int_state & ~FDC_INT_NEEDED) | FDC_INT_REQUESTED);
    }

    if (fdc_int_state & FDC_INT_REQUESTED) {
        bus_pins &= ~Z80_IEIO;
        if ((bus_pins & (Z80_M1 | Z80_IORQ)) == (Z80_M1 | Z80_IORQ)) {
            Z80_SET_DATA(bus_pins, fdc_int_vector);
            fdc_int_state = (uint8_t)(
                (fdc_int_state & ~FDC_INT_REQUESTED) | FDC_INT_SERVICED);
            bus_pins &= ~Z80_INT;
            (void)i8272_irq_pins(&fdc, I8272_IACK);
        } else {
            bus_pins |= Z80_INT;
        }
    }
}

bool partner::dma_transfer_pending() const
{
    return dma.enabled && (dma.bytes_remaining > 0 ||
        dma.port_a.block_length > 0 || dma.port_b.block_length > 0);
}

bool partner::dma_owns_bus() const
{
    if (!dma.enabled)
        return false;
    // In Z8410 byte mode BUSREQ is released after every byte so the CPU can
    // execute at least one complete machine cycle.  The DMA core counts that
    // four-clock minimum explicitly; board-level RDY glue must not reacquire
    // the bus during the gap.
    if (dma_cpu_yield_active_ || dma.byte_gap_ticks != 0)
        return false;
    if (dma_busreq_latched || (dma.state != Z80DMA_STATE_IDLE))
        return true;
    if (!dma_transfer_pending())
        return false;
    // Keep the CPU off the SASI/FDC data port while a programmed block is still
    // in flight and the source is ready. Otherwise a stray IN (0x11) between DMA
    // bus cycles consumes the byte without landing it in RAM.
    if (dma_ready_input_)
        return true;
    if (!dma.direction_ab) {
        const z80dma_port_t &src = dma.port_b;
        const uint8_t src_port = (uint8_t)(src.address & 0xFF);
        if (!src.is_memory && partner_sasi_port(src_port) &&
            partner_sasi_function(src_port) == 1)
            return idpartner_sasi_drq(&sasi_);
        if (!src.is_memory && partner_fdc_port(src_port) && (src_port & 1u))
            return i8272_drq(&fdc);
    }
    return false;
}

uint8_t partner::peek_ram(uint16_t addr) const
{
    if ((addr >= banked_base) && (addr < shared_base) && (ram_bank == 2))
        return ram_bank2_[addr - banked_base];
    return ram[addr];
}

uint8_t partner::read_mem(uint16_t addr)
{
    // E51 and E50 are separate 2 KiB sockets selected at 0000h and 0800h.
    // The remainder of the 8 KiB overlay window has no ROM output enabled.
    if (rom_enabled && addr < 0x2000)
        return read_boot_rom(addr);

    return peek_ram(addr);
}

uint8_t partner::read_boot_rom(uint16_t addr) const
{
    if (addr < rom_size)
        return rom[addr];
    if (addr < rom_capacity)
        return rom2_loaded_ ? rom2[addr - rom_size] : 0xFF;
    return 0xFF;
}

void partner::write_mem(uint16_t addr, uint8_t data)
{
    if (addr == 0xF9C1) {
        const uint32_t idx = dbg_irref_count & 0xFu;
        dbg_irref_values[idx] = data;
        dbg_irref_pcs[idx] = cpu.pc;
        dbg_irref_sps[idx] = cpu.sp;
        dbg_irref_stack0[idx] =
            (uint16_t)(peek_mem(cpu.sp) | (uint16_t(peek_mem((uint16_t)(cpu.sp + 1))) << 8));
        dbg_irref_stack1[idx] =
            (uint16_t)(peek_mem((uint16_t)(cpu.sp + 2)) |
                       (uint16_t(peek_mem((uint16_t)(cpu.sp + 3))) << 8));
        dbg_irref_ticks[idx] = tick_count;
        dbg_irref_count++;
    }
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

    if (partner_sasi_port((uint8_t)port) && partner_sasi_function((uint8_t)port) == 0) {
        const uint8_t st = idpartner_sasi_status_r(&sasi_);
        static const bool trace_hd = [] {
            const char *s = std::getenv("IDP_TRACE_HD");
            return s && s[0] && s[0] != '0';
        }();
        if (trace_hd && cpu.pc >= 0xC00B)
            std::fprintf(stderr, "[hd] status_r=%02X busy=%d pc=%04X\n",
                st, sasi_.target ? (int)sasi_.target->busy : -1, cpu.pc);
        return st;
    }
    if (partner_sasi_port((uint8_t)port) && partner_sasi_function((uint8_t)port) == 1) {
        return sasi_data_read_for_bus();
    }
    if (partner_sasi_port((uint8_t)port))
        return 0xFF;

    // MM58167 RTC: 0xA0-0xBF
    if (port >= 0xA0 && port <= 0xBF)
    {
        return mm58167a_bus_read(&rtc, (uint8_t)(port - 0xA0));
    }

    // Z80 PIO: 0xD0-0xD3
    if (partner_pio_port((uint8_t)port))
    {
        return z80pio_cpu_read(&pio, (uint8_t)port);
    }

    // Z80 CTC: 0xC8-0xCB
    if (partner_ctc_port((uint8_t)port))
    {
        return z80ctc_cpu_read(&ctc, (uint8_t)port);
    }

    // Z80 SIO chip 0: D8/D9 = channel A data/control, DA/DB = channel B data/control.
    if (partner_sio0_port((uint8_t)port))
    {
        const uint8_t data = z80sio_cpu_read(&sio, (uint8_t)port);
        if ((port & 0x01u) == 0)
            request_sio_service();
        static const bool trace_sio = [] {
            const char *s = std::getenv("IDP_TRACE_SIO");
            return s && s[0] && s[0] != '0';
        }();
        if (trace_sio && ((uint8_t)port == 0xDA))
            std::fprintf(stderr, "[sio] rd pc=%04X port=%02X data=%02X\n",
                cpu.pc, (unsigned int)((uint8_t)port), (unsigned int)data);
        return data;
    }
    // Z80 SIO chip 1: E0/E1 = channel A data/control, E2/E3 = channel B data/control.
    if (partner_sio1_port((uint8_t)port))
    {
        const uint8_t data = z80sio_cpu_read(&sio2, (uint8_t)port);
        if ((port & 0x01u) == 0)
            request_sio_service();
        return data;
    }

    // Z80 DMA: 0xC0 — kernel hd_dma_setup$ uses otir; route through the chip
    // model so every programmed byte lands (io_write is edge-gated per OUT).
    if (partner_dma_port((uint8_t)port))
        return z80dma_read(&dma);

    // Intel 8272 FDC Status: 0xF0
    if (partner_fdc_port((uint8_t)port) && (port & 0x01u) == 0)
    {
        return i8272_bus_read(&fdc, 0);
    }

    // Intel 8272 FDC Data: 0xF1
    if (partner_fdc_port((uint8_t)port) && (port & 0x01u) != 0)
    {
        const i8272_phase_t phase_before = fdc.phase;
        const uint8_t result_before = fdc.result_idx;
        const uint8_t data = i8272_bus_read(&fdc, 1);
        if (fdc_trace_enabled() &&
            (phase_before == I8272_PHASE_RESULT || phase_before != fdc.phase)) {
            std::fprintf(stderr,
                "[fdc] tick=%llu rd=%02X phase=%u->%u result=%u->%u/%u\n",
                (unsigned long long)tick_count, data, (unsigned)phase_before,
                (unsigned)fdc.phase, result_before, fdc.result_idx,
                fdc.result_len);
        }
        return data;
    }

    // FDC Motor Status: 0x98
    if (partner_motor_port((uint8_t)port))
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

    if (partner_sasi_port((uint8_t)port) && partner_sasi_function((uint8_t)port) == 0)
    {
        idpartner_sasi_ctrl_w(&sasi_, data);
        return;
    }
    if (partner_sasi_port((uint8_t)port) && partner_sasi_function((uint8_t)port) == 1)
    {
        sasi_data_writes_++;
        static const bool trace_hd = [] {
            const char *s = std::getenv("IDP_TRACE_HD");
            return s && s[0] && s[0] != '0';
        }();
        if (trace_hd)
            std::fprintf(stderr, "[hd] data_w=%02X phase=%d cfg=%u/%u pc=%04X\n",
                data, (int)hdc.phase, hdc.cfg_len, hdc.cfg_expected, cpu.pc);
        idpartner_sasi_data_w(&sasi_, data);
        return;
    }
    if (partner_sasi_port((uint8_t)port) && partner_sasi_function((uint8_t)port) == 2)
    {
        idpartner_sasi_reset_w(&sasi_, data);
        return;
    }

    // MM58167 RTC: 0xA0-0xBF
    if (port >= 0xA0 && port <= 0xBF)
    {
        mm58167a_bus_write(&rtc, (uint8_t)(port - 0xA0), data);
        if (port >= 0xA8 && port <= 0xAF)
            save_rtc_nvram();
        return;
    }

    // Z80 PIO: 0xD0-0xD3
    if (partner_pio_port((uint8_t)port))
    {
        const bool is_data_write = (port & 0x01) == 0;
        const pio_port_id pio_port = (port & 0x02) ? pio_port_id::b : pio_port_id::a;
        z80pio_cpu_write(&pio, (uint8_t)port, data);
        const uint64_t ready_pin = (pio_port == pio_port_id::a)
            ? Z80PIO_ARDY : Z80PIO_BRDY;
        if (is_data_write && (pio.pins & ready_pin))
        {
            const uint8_t pin_data = (pio_port == pio_port_id::a)
                ? Z80PIO_GET_PA(pio.pins) : Z80PIO_GET_PB(pio.pins);
            apply_pio_device_output(pio_port, pin_data);
            pulse_pio_output_ack(pio_port);
        }
        return;
    }

    // Z80 CTC: 0xC8-0xCB
    if (partner_ctc_port((uint8_t)port))
    {
        z80ctc_cpu_write(&ctc, (uint8_t)port, data);
        return;
    }

    if (partner_sio0_port((uint8_t)port))
    {
        const int channel = (port & 0x02) != 0
            ? Z80SIO_CHANNEL_B : Z80SIO_CHANNEL_A;
        const bool channel_reset = (port & 0x01) != 0 &&
            sio.chn[channel].reg_index == 0 && (data & 0x38u) == 0x18u;
        static const bool trace_sio = [] {
            const char *s = std::getenv("IDP_TRACE_SIO");
            return s && s[0] && s[0] != '0';
        }();
        if (trace_sio && ((uint8_t)port == 0xDA || (uint8_t)port == 0xDB))
            std::fprintf(stderr, "[sio] wr pc=%04X port=%02X data=%02X\n",
                cpu.pc,
                (unsigned int)((uint8_t)port), (unsigned int)data);
        z80sio_cpu_write(&sio, (uint8_t)port, data);
        request_sio_service();
        if (channel_reset)
            reset_sio_device_session(channel == Z80SIO_CHANNEL_B
                ? sio_port_id::sio1_b : sio_port_id::sio1_a);
        return;
    }
    if (partner_sio1_port((uint8_t)port))
    {
        const int channel = (port & 0x02) != 0
            ? Z80SIO_CHANNEL_B : Z80SIO_CHANNEL_A;
        const bool channel_reset = (port & 0x01) != 0 &&
            sio2.chn[channel].reg_index == 0 && (data & 0x38u) == 0x18u;
        z80sio_cpu_write(&sio2, (uint8_t)port, data);
        request_sio_service();
        if (channel_reset)
            reset_sio_device_session(channel == Z80SIO_CHANNEL_B
                ? sio_port_id::sio2_b : sio_port_id::sio2_a);
        return;
    }

    if (partner_dma_port((uint8_t)port)) {
        z80dma_write(&dma, data);
        return;
    }

    // Intel 8272 FDC Data: 0xF1
    if (partner_fdc_port((uint8_t)port) && (port & 0x01u) != 0)
    {
        const i8272_phase_t phase_before = fdc.phase;
        const uint8_t cmd_before = fdc.cmd_idx;
        const uint16_t data_before = fdc.data_idx;
        i8272_bus_write(&fdc, 1, data);
        if (fdc_trace_enabled() &&
            (phase_before != fdc.phase || data_before == 0u ||
             (fdc.data_len != 0u && fdc.data_idx == fdc.data_len))) {
            std::fprintf(stderr,
                "[fdc] tick=%llu wr=%02X phase=%u->%u cmd=%u->%u/%u code=%02X "
                "data=%u->%u/%u irq_delay=%u\n",
                (unsigned long long)tick_count, data, (unsigned)phase_before,
                (unsigned)fdc.phase, cmd_before, fdc.cmd_idx, fdc.cmd_len,
                fdc.cmd_code, data_before, fdc.data_idx, fdc.data_len,
                fdc.irq_delay);
        }
        return;
    }

    // FDC Motor Control: 0x98
    if (partner_motor_port((uint8_t)port))
    {
        // Board documentation describes OUT 98h as "turn motor on" and
        // IN 98h bit 0 as the motor-running status. Keep the motor on until
        // reset or the CTC-generated timeout pulse turns it off again.
        fdc_motor_running = true;
        fdc_motor = 0x01;
        for (int drive = 0; drive < I8272_MAX_DRIVES; ++drive)
            i8272_set_drive_motor(&fdc, drive, true);
        return;
    }

    // FDC Interrupt Vector: 0xE8
    if (partner_fdc_vector_port((uint8_t)port))
    {
        fdc_int_vector = data;
        if (!fdc_reset_irq_armed_ && cpu.i != 0xFE)
        {
            // Arm the power-on reset-complete interrupt when firmware
            // configures the FDC vector during fdc_init, instead of keeping a
            // stale interrupt pending from machine reset all the way to the
            // monitor prompt.
            i8272_schedule_reset_irq(&fdc, 64);
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
