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

class partner
{
public:
    static constexpr size_t rom_size = 0x0800;
    static constexpr size_t ram_size = 0x10000;
    static constexpr uint16_t banked_base = 0x0000;
    static constexpr uint16_t shared_base = 0xC000;
    static constexpr size_t banked_size = shared_base - banked_base;

    partner();
    virtual ~partner() = default;

    void load_rom(const std::string &path);
    void load_disk(int drive, const std::string &path);
    void load_hdd(const std::string &path);
    void set_force_floppy_boot(bool enabled) { force_floppy_boot_ = enabled; }
    virtual void reset();
    virtual void tick();

    // State accessors for GUI panels (read-only)
    const z80_t& get_cpu() const { return cpu; }
    const z80dma_t& get_dma() const { return dma; }
    const z80ctc_t& get_ctc() const { return ctc; }
    const z80sio_t& get_sio() const { return sio; }
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
    bool is_opdone() const {
        return ((pins & (Z80_M1|Z80_RD)) == (Z80_M1|Z80_RD)) && !cpu.prefix_active;
    }
    // During M1 fetch, cpu.pc has already been incremented past the opcode byte.
    // This returns the actual instruction address.
    uint16_t get_current_pc() const {
        return is_opdone() ? (uint16_t)(cpu.pc - 1) : cpu.pc;
    }

protected:
    // All Zilog chips in Partner system
    z80_t cpu{};
    z80dma_t dma{};    // Highest priority (if present)
    z80ctc_t ctc{};    // Second priority
    z80sio_t sio{};    // Third priority
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
    bool force_floppy_boot_ = false;
    bool auto_floppy_key_sent_ = false;

    virtual uint8_t read_mem(uint16_t addr);
    virtual void write_mem(uint16_t addr, uint8_t data);
    virtual uint8_t io_read(uint16_t port);
    virtual void io_write(uint16_t port, uint8_t data);

    void restore_drive_ready_flags();
    void service_cpu_bus(uint64_t &pins);
    void service_dma_read_bus(uint64_t &pins);
    void service_dma_write_bus(uint64_t &pins);
    void service_fdc_daisy(uint64_t &pins, bool cpu_ticked);
    bool dma_owns_bus() const;
    uint8_t peek_ram(uint16_t addr) const;

private:
    struct disk_image {
        std::vector<uint8_t> data;
        uint32_t seclen = 256;
        uint32_t sectrk = 18;
        uint32_t heads = 1;
    };
    std::array<disk_image, I8272_MAX_DRIVES> disks_;
    disk_image hdd_;

    static bool read_sector_cb(int drive, int c, int h, int r, int n,
                               uint8_t *buf, void *user);
    static bool read_hdd_blocks_cb(uint32_t lba, uint32_t count, uint8_t *buf, void *user);
};
