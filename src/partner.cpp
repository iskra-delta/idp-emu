#include "partner.hpp"
#include <fstream>
#include <iostream>
#include <cstring>

namespace {
constexpr uint64_t PARTNER_HDD_SIZE = 1224ULL * 32 * 256;
constexpr uint8_t FDC_INT_NEEDED = 1 << 0;
constexpr uint8_t FDC_INT_REQUESTED = 1 << 1;
constexpr uint8_t FDC_INT_SERVICED = 1 << 2;
}

partner::partner()
{
    // Initialize all chips
    z80dma_init(&dma);
    z80ctc_init(&ctc);
    z80sio_init(&sio);
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

    reset();
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
    z80pio_reset(&pio);
    i8272_reset(&fdc);
    s1410_reset(&hdc);
    idpartner_sasi_reset(&sasi_);
    mm58167a_reset(&rtc);

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

    // Third priority: SIO (ports 0xD8-0xDB for Ch1, 0xE0-0xE4 for Ch2)
    // Drive SIO port-select pins from low address bits:
    // bit0: data/control, bit1: channel A/B.
    if ((pins & Z80_IORQ) && !(pins & Z80_M1) &&
        ((port >= 0xD8 && port <= 0xDB) || (port >= 0xE0 && port <= 0xE4)))
    {
        pins |= Z80SIO_CE;
        if (port & 0x01) pins |= Z80SIO_CS_A;  // control register select
        if (port & 0x02) pins |= Z80SIO_CS_B;  // channel B select
    }
    pins = z80sio_tick(&sio, pins);
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
    else if ((bus_pins & (Z80_IORQ | Z80_M1)) == (Z80_IORQ | Z80_M1))
    {
        // External 8272 interrupt acknowledge for IM2.
        // This must drive the vector byte on the exact acknowledge cycle;
        // trying to inject it later after the other chip ticks leaves the
        // stale opcode/data bus value in place.
        if (fdc.irq_request)
        {
            Z80_SET_DATA(bus_pins, fdc_int_vector);
            fdc.irq_request = false;
            fdc_int_state = 0;
        }
    }
    else if ((bus_pins & Z80_IORQ) && !(bus_pins & Z80_M1))
    {
        const bool io_rd_now = (bus_pins & (Z80_IORQ | Z80_RD)) == (Z80_IORQ | Z80_RD);
        const bool io_rd_prev = (last_cpu_bus_pins_ & (Z80_IORQ | Z80_RD)) == (Z80_IORQ | Z80_RD);
        const bool io_wr_prev = (last_cpu_bus_pins_ & (Z80_IORQ | Z80_WR)) == (Z80_IORQ | Z80_WR);
        const uint16_t prev_addr = Z80_GET_ADDR(last_cpu_bus_pins_);
        const bool same_port = ((prev_addr & 0xFF) == (addr & 0xFF));

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
            if (!io_wr_prev || !same_port)
                io_write(addr & 0xFF, Z80_GET_DATA(bus_pins));
        }

        if (!io_rd_now || !same_port)
            io_read_latched_ = false;
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
    // The Partner's 8272 is not a Zilog-family daisy-chain device. Its
    // interrupt is an external IM2 request whose low vector byte comes from
    // the board latch at port E8h, so present it as a plain level-sensitive
    // INT source until the CPU performs the acknowledge cycle.
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
        return s1410_read_error(&hdc);

    // MM58167 RTC: 0xA0-0xBF
    if ((port >= 0xA0 && port <= 0xB6) || port == 0xBF)
    {
        return rtc.regs[port - 0xA0];
    }

    // Z80 PIO: 0xD0-0xD3
    if (port >= 0xD0 && port <= 0xD3)
    {
        // PIO handles its own I/O in tick function via CE pin
        return 0xFF;
    }

    // Z80 SIO Channel 1: 0xD8-0xDB
    // ROM keyboard polling relies on immediate status/data visibility on D9/D8.
    // Expose channel-A reads directly here so injected RX bytes are observable
    // even when the SIO core isn't selected in the same CPU microstep.
    if (port == 0xD9)
    {
        uint8_t rr0 = 0;
        const auto &cha = sio.chn[0];
        if (cha.rx_ready) rr0 |= (1 << 0);   // RX char available
        if (cha.int_state) rr0 |= (1 << 1);  // interrupt pending
        if (cha.tx_ready) rr0 |= (1 << 2);   // TX buffer empty
        if (cha.dcd) rr0 |= (1 << 3);
        if (cha.break_abort) rr0 |= (1 << 4);
        if (cha.cts) rr0 |= (1 << 5);
        if (cha.tx_underrun) rr0 |= (1 << 6);
        return rr0;
    }
    if (port == 0xD8)
    {
        auto &cha = sio.chn[0];
        const uint8_t data = cha.rx_data;
        cha.rx_ready = false;
        cha.int_state &= ~Z80SIO_INT_NEEDED;
        return data;
    }

    // Z80 SIO Channel 1/2 remaining ports
    if ((port >= 0xD8 && port <= 0xDB) || (port >= 0xE0 && port <= 0xE4))
    {
        // Other control/status paths are still handled by the SIO core tick.
        return 0xFF;
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
        return i8272_read_status(&fdc);
    }

    // Intel 8272 FDC Data: 0xF1
    if (port == 0xF1)
    {
        return i8272_read_data(&fdc);
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
        rtc.regs[port - 0xA0] = data;
        // Writing reset-counter register refreshes visible time registers.
        if (port == 0xB2)
            mm58167a_sync_time(&rtc);
        return;
    }

    // Z80 PIO: 0xD0-0xD3
    if (port >= 0xD0 && port <= 0xD3)
    {
        // PIO handles its own I/O in tick function via CE pin
        return;
    }

    // Z80 SIO explicit Partner port mapping.
    // SIO core decode uses CS_A=control/data-select and CS_B=channel-select.
    // Partner wiring exposes multiple aliases, so drive SIO directly here.
    if ((port >= 0xD8 && port <= 0xDB) || (port >= 0xE0 && port <= 0xE4))
    {
        uint64_t sio_pins = 0;
        sio_pins |= Z80SIO_CE | Z80SIO_IORQ | Z80SIO_WR;
        Z80SIO_SET_DATA(sio_pins, data);

        if (port == 0xD8) {
            // Channel A data
        } else if ((port == 0xD9) || (port == 0xDA) || (port == 0xDB)) {
            // Channel A control/status aliases
            sio_pins |= Z80SIO_CS_A;
        } else if (port == 0xE0) {
            // Channel B data
            sio_pins |= Z80SIO_CS_B;
        } else {
            // Channel B control aliases (E1-E4)
            sio_pins |= Z80SIO_CS_A | Z80SIO_CS_B;
        }

        (void)z80sio_tick(&sio, sio_pins);
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
        i8272_write_data(&fdc, data);
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
