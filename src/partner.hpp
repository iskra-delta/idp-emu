#pragma once
#include "z80.h"
#include "z80sio.h"
#include "z80pio.h"
#include "z80ctc.h"
#include "z80dma.h"
#include "i8272.h"
#include "s1410.h"
#include "idpartner_sasi.h"
#include "mm58167.h"
#include <array>
#include <string>
#include <cstdint>
#include <vector>
#include <deque>
#include <utility>

class partner
{
public:
    static constexpr size_t rom_size = 0x0800;
    static constexpr size_t ram_size = 0x10000;
    static constexpr uint16_t banked_base = 0x0000;
    static constexpr uint16_t shared_base = 0xC000;
    static constexpr size_t banked_size = shared_base - banked_base;

    enum class sio_port_id : uint8_t {
        sio1_a = 0,
        sio1_b = 1,
        sio2_a = 2,
        sio2_b = 3
    };

    enum class sio_device_kind : uint8_t {
        none = 0,
        mouse_microsoft,
        mouse_mousesystems,
        mouse_logitech,
        tcp_bridge
    };

    struct sio_device_config {
        sio_device_kind kind = sio_device_kind::none;
        int tcp_data_port = 6601;
        int tcp_control_port = 6602;
        bool tcp_require_rts = true;
        bool tcp_cts_follows_data_client = true;
    };

    struct sio_port_status {
        bool locked = false;
        bool connected = false;
        bool cts = false;
        bool dcd = false;
        bool rts = false;
        bool dtr = false;
        size_t pending_rx_bytes = 0;
        uint64_t tx_bytes = 0;
        uint64_t rx_bytes = 0;
        std::string detail;
    };

    enum class pio_port_id : uint8_t {
        a = 0,
        b = 1
    };

    enum class pio_device_kind : uint8_t {
        none = 0,
        covox,
        centronics_printer
    };

    struct pio_device_config {
        pio_device_kind kind = pio_device_kind::none;
    };

    struct pio_port_status {
        uint8_t last_output = 0;
        float covox_level = 0.0f;
        uint64_t bytes_seen = 0;
    };

    explicit partner(const std::string &rtc_nvram_path = "partner_cmos.bin");
    virtual ~partner();

    void load_rom(const std::string &path);
    void load_disk(int drive, const std::string &path);
    void load_hdd(const std::string &path);
    std::string get_disk_path(int drive) const;
    virtual void reset();
    virtual void tick();

    // State accessors for GUI panels (read-only)
    const z80_t& get_cpu() const { return cpu; }
    const z80dma_t& get_dma() const { return dma; }
    const z80ctc_t& get_ctc() const { return ctc; }
    const z80sio_t& get_sio() const { return sio; }
    const z80sio_t& get_sio2() const { return sio2; }
    const z80pio_t& get_pio() const { return pio; }
    const i8272_t& get_fdc() const { return fdc; }
    const s1410_t& get_hdc() const { return hdc; }
    const idpartner_sasi_t& get_sasi() const { return sasi_; }
    const mm58167a_t& get_rtc() const { return rtc; }
    uint8_t get_fdc_motor() const { return fdc_motor; }
    uint8_t get_fdc_int_vector() const { return fdc_int_vector; }
    uint8_t get_fdc_int_state() const { return fdc_int_state; }
    uint64_t get_pins() const { return pins; }
    uint32_t get_dma_fdc_reads() const { return dma_fdc_reads_; }
    uint32_t get_dma_mem_writes() const { return dma_mem_writes_; }
    bool get_dma_ready_input() const { return dma_ready_input_; }
    uint8_t peek_mem(uint16_t addr) const {
        if (rom_enabled && addr < 0x2000) return rom[addr & (rom_size - 1)];
        return peek_ram(addr);
    }
    uint64_t get_tick_count() const { return tick_count; }
    bool is_rom_enabled() const { return rom_enabled; }
    uint8_t get_ram_bank() const { return ram_bank; }
    // Instruction boundary test. Uses the CPU's own latched pin copy
    // (cpu.pins) rather than the shared bus mask: the peripheral daisy
    // chain rewrites the shared mask every tick (e.g. while a CTC interrupt
    // is pending) and can mask out the M1|RD fetch strobes.
    bool is_opdone() const {
        return ((cpu.pins & (Z80_M1|Z80_RD)) == (Z80_M1|Z80_RD)) && !cpu.prefix_active;
    }
    // During M1 fetch, cpu.pc has already been incremented past the opcode byte.
    // This returns the actual instruction address.
    uint16_t get_current_pc() const {
        return is_opdone() ? (uint16_t)(cpu.pc - 1) : cpu.pc;
    }

    sio_device_config get_sio_device_config(sio_port_id port) const;
    bool set_sio_device_config(sio_port_id port, const sio_device_config &cfg);
    sio_port_status get_sio_port_status(sio_port_id port) const;
    bool is_sio_port_locked(sio_port_id port) const;
    std::string get_sio_port_lock_reason(sio_port_id port) const;

    pio_device_config get_pio_device_config(pio_port_id port) const;
    void set_pio_device_config(pio_port_id port, const pio_device_config &cfg);
    pio_port_status get_pio_port_status(pio_port_id port) const;
    const std::string &get_virtual_printer_text() const { return virtual_printer_text_; }
    void clear_virtual_printer_text() { virtual_printer_text_.clear(); }

    void inject_serial_mouse_motion(int dx, int dy, bool left_pressed, bool right_pressed, bool middle_pressed);
    bool has_serial_mouse_attached() const;
    bool has_logitech_mouse_attached() const;

    struct debug_cpu_state {
        uint16_t af = 0;
        uint16_t bc = 0;
        uint16_t de = 0;
        uint16_t hl = 0;
        uint16_t ix = 0;
        uint16_t iy = 0;
        uint16_t sp = 0;
        uint16_t pc = 0;
        uint8_t i = 0;
        uint8_t r = 0;
        bool iff1 = false;
        bool iff2 = false;
        bool halted = false;
    };

    debug_cpu_state capture_debug_cpu_state() const;
    void apply_debug_cpu_state(const debug_cpu_state &state);
    // Redirect execution to a new PC. Unlike apply_debug_cpu_state, this
    // restarts the CPU's pipelined opcode fetch at the new address.
    void debug_set_pc(uint16_t pc);
    std::vector<uint8_t> read_debug_memory(uint32_t address, size_t length) const;
    void write_debug_memory(uint32_t address, const std::vector<uint8_t> &data);

protected:
    // All Zilog chips in Partner system
    z80_t cpu{};
    z80dma_t dma{};    // Highest priority (if present)
    z80ctc_t ctc{};    // Second priority
    z80sio_t sio{};    // Third priority
    z80sio_t sio2{};   // Fourth priority
    z80pio_t pio{};    // Lowest priority

    // Intel 8272 FDC (not on Zilog daisy chain)
    i8272_t fdc{};
    s1410_t hdc{};
    idpartner_sasi_t sasi_{};
    mm58167a_t rtc{};
    uint8_t fdc_int_vector = 0;  // Port 0xE8
    uint8_t fdc_motor = 0;       // Port 0x98
    uint8_t fdc_int_state = 0;
    bool fdc_reset_irq_armed_ = false;
    bool prompt_fdc_cleanup_done_ = false;
    bool fdc_motor_running = false;
    bool dma_busreq_latched = false;
    bool dma_ready_input_ = false;
    uint32_t dma_fdc_reads_ = 0;
    uint32_t dma_mem_writes_ = 0;
    uint64_t last_cpu_bus_pins_ = 0;
    bool io_read_latched_ = false;
    uint16_t io_read_latched_addr_ = 0;
    uint8_t io_read_latched_data_ = 0xFF;

    uint64_t pins = 0;
    uint64_t tick_count = 0;

    std::array<uint8_t, rom_size> rom{};
    std::array<uint8_t, ram_size> ram{};
    std::array<uint8_t, banked_size> ram_bank2_{};

    // Banking control
    bool rom_enabled = true;
    uint8_t ram_bank = 1;  // Bank 1 is default
    virtual uint8_t read_mem(uint16_t addr);
    virtual void write_mem(uint16_t addr, uint8_t data);
    virtual uint8_t io_read(uint16_t port);
    virtual void io_write(uint16_t port, uint8_t data);
    virtual int get_external_im2_vector() const { return -1; }
    // AVDC vertical-blank edge for CTC ch3 clock — overridden by partner_gdp.
    virtual bool get_avdc_vb_edge() const { return false; }

    void restore_drive_ready_flags();
    void service_cpu_bus(uint64_t &pins);
    void service_dma_read_bus(uint64_t &pins);
    void service_dma_write_bus(uint64_t &pins);
    void service_fdc_daisy(uint64_t &pins, bool cpu_ticked);
    int get_pending_daisy_vector() const;
    bool dma_owns_bus() const;
    uint8_t peek_ram(uint16_t addr) const;
    void set_sio_port_lock(sio_port_id port, bool locked, const std::string &reason);

private:
    struct tcp_bridge_runtime {
        int listen_fd = -1;
        int control_listen_fd = -1;
        int data_client_fd = -1;
        int control_client_fd = -1;
        uint64_t next_poll_tick = 0;
        bool control_cts_override_active = false;
        bool control_cts_override_value = false;
        bool control_dcd_override_active = false;
        bool control_dcd_override_value = false;
        bool last_rts = false;
        bool last_dtr = false;
        std::string control_rx_buf;
        std::deque<uint8_t> data_rx_fifo;
        std::deque<uint8_t> data_tx_fifo;
    };

    struct sio_device_runtime {
        std::deque<uint8_t> rx_fifo;
        uint8_t last_mouse_buttons = 0;
        bool mouse_buttons_initialized = false;
        int32_t mouse_accum_dx = 0;
        int32_t mouse_accum_dy = 0;
        uint64_t tx_bytes = 0;
        uint64_t rx_bytes = 0;
        tcp_bridge_runtime tcp{};
    };

    struct pio_device_runtime {
        uint8_t last_output = 0;
        float covox_level = 0.0f;
        uint64_t bytes_seen = 0;
    };

    void load_rtc_nvram();
    void save_rtc_nvram() const;
    void service_virtual_devices();
    void service_sio_device(sio_port_id port, z80sio_t *chip, int channel);
    void apply_sio_modem_inputs(uint64_t &bus_pins, sio_port_id port_a, sio_port_id port_b);
    void apply_pio_device_output(pio_port_id port, uint8_t data);
    void queue_mouse_packet(sio_port_id port, int dx, int dy, uint8_t buttons);
    void queue_logitech_c7_poll_report(sio_port_id port);
    void queue_logitech_c7_identification(sio_port_id port);
    static int sio_port_index(sio_port_id port) { return (int)port; }
    static int pio_port_index(pio_port_id port) { return (int)port; }
    void reset_sio_device_runtime(sio_port_id port);
    void cleanup_tcp_bridge(tcp_bridge_runtime &tcp);
    bool ensure_tcp_bridge_listeners(sio_port_id port);
    void poll_tcp_bridge(sio_port_id port, z80sio_channel_t &ch);
    void tcp_bridge_send_modem_state(tcp_bridge_runtime &tcp, const char *name, bool value);
    void tcp_bridge_parse_control_line(tcp_bridge_runtime &tcp, const std::string &line);
    static bool parse_bool_token(const std::string &token, bool &value);
    std::pair<z80sio_t *, int> resolve_sio_channel(sio_port_id port);
    const std::pair<const z80sio_t *, int> resolve_sio_channel_const(sio_port_id port) const;

    struct disk_image {
        std::vector<uint8_t> data;
        uint32_t seclen = 256;
        uint32_t sectrk = 18;
        uint32_t heads = 1;
        std::string path;
    };
    std::array<disk_image, I8272_MAX_DRIVES> disks_;
    disk_image hdd_;
    std::string rtc_nvram_path_ = "partner_cmos.bin";
    std::array<sio_device_config, 4> sio_device_cfg_{};
    std::array<sio_device_runtime, 4> sio_device_runtime_{};
    std::array<bool, 4> sio_port_locked_{};
    std::array<std::string, 4> sio_port_lock_reason_{};
    std::array<pio_device_config, 2> pio_device_cfg_{};
    std::array<pio_device_runtime, 2> pio_device_runtime_{};
    std::string virtual_printer_text_;

    bool persist_disk_bytes(disk_image &disk, uint64_t offset,
                            const uint8_t *src, size_t size);
    static bool read_sector_cb(int drive, int c, int h, int r, int n,
                               uint8_t *buf, void *user);
    static bool write_sector_cb(int drive, int c, int h, int r, int n,
                                const uint8_t *buf, void *user);
    static bool read_hdd_blocks_cb(uint32_t lba, uint32_t count, uint8_t *buf, void *user);
    static bool write_hdd_blocks_cb(uint32_t lba, uint32_t count, const uint8_t *buf, void *user);
};
