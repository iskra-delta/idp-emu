#include "partner_crt.hpp"
#include <iomanip>
#include <iostream>
#include <string>

namespace {
bool is_prompt_wait(uint16_t pc) {
    return (pc == 0x009F) || (pc == 0x00A1) || (pc == 0x00A3);
}

bool is_interesting_pc(uint16_t pc) {
    switch (pc) {
        case 0x0003:
        case 0x000E:
        case 0x0011:
        case 0x0013:
        case 0x0018:
        case 0x001D:
        case 0x009F:
        case 0x00A5:
        case 0x00A7:
        case 0x0177:
        case 0x017A:
        case 0x020F:
        case 0x0137:
        case 0x0155:
        case 0x0174:
        case 0x021E:
        case 0x022D:
        case 0x022E:
        case 0x0230:
        case 0x0233:
        case 0x0239:
        case 0x024C:
        case 0x0254:
        case 0x0259:
        case 0x0266:
        case 0x0272:
        case 0x027E:
        case 0x0281:
        case 0x0292:
        case 0x029B:
        case 0x02B6:
        case 0x02CC:
        case 0x03A5:
        case 0x03BC:
        case 0x03C2:
        case 0x03D3:
        case 0x03DA:
        case 0x03DE:
        case 0x03DF:
        case 0x03E0:
        case 0x03E3:
        case 0x03E8:
        case 0x03EB:
        case 0x03F2:
        case 0x03F5:
        case 0x03F8:
        case 0x03FB:
        case 0x03FD:
        case 0x0400:
        case 0x0402:
        case 0x0405:
        case 0x0408:
        case 0x040B:
        case 0x0604:
        case 0x0610:
            return true;
        default:
            return false;
    }
}

void dump_state(const partner_crt& idp, const char* tag) {
    const auto& fdc = idp.get_fdc();
    const auto& dma = idp.get_dma();
    const auto& ctc = idp.get_ctc();
    const auto& pio = idp.get_pio();
    const auto& cpu = idp.get_cpu();
    const auto& sio = idp.get_sio();
    const uint64_t pins = idp.get_pins();
    const uint8_t drv = idp.peek_mem(0xFFD0);
    const uint8_t trk = idp.peek_mem(0xFFD1);
    const uint16_t dma_dst = (uint16_t)idp.peek_mem(0xFFD2) | ((uint16_t)idp.peek_mem(0xFFD3) << 8);
    const uint8_t sec = idp.peek_mem(0xFFD4);
    const uint8_t retry = idp.peek_mem(0xFFD5);
    const uint8_t side = idp.peek_mem(0xFFD7);
    const uint8_t remain = idp.peek_mem(0xFFD8);
    const uint16_t ret0 = (uint16_t)idp.peek_mem(cpu.sp) | ((uint16_t)idp.peek_mem((uint16_t)(cpu.sp + 1)) << 8);
    std::cout
        << tag
        << " tick=" << idp.get_tick_count()
        << " pc=" << std::hex << std::setw(4) << std::setfill('0') << idp.get_current_pc()
        << " i=" << std::hex << std::setw(2) << (int)cpu.i
        << " im=" << std::dec << (int)cpu.im
        << " iff=" << (cpu.iff1 ? 1 : 0) << (cpu.iff2 ? 1 : 0)
        << " step=" << std::dec << cpu.step
        << " sp=" << std::hex << std::setw(4) << cpu.sp
        << " wz=" << std::hex << std::setw(4) << cpu.wz
        << " dl=" << std::hex << std::setw(2) << (int)cpu.dlatch
        << " phase=" << std::dec << (int)fdc.phase
        << " cmd=" << std::hex << std::setw(2) << (int)fdc.cmd_code
        << " msr=" << std::hex << std::setw(2) << (int)fdc.msr
        << " irq=" << fdc.irq_request
        << " sense=" << fdc.int_pending
        << " fint=" << std::hex << std::setw(2) << (int)idp.get_fdc_int_state()
        << " delay=" << std::dec << fdc.irq_delay
        << " res=" << (int)fdc.result_idx << "/" << (int)fdc.result_len
        << " data=" << (int)fdc.data_idx << "/" << (int)fdc.data_len
        << " motor=" << std::hex << std::setw(2) << (int)idp.get_fdc_motor()
        << " dma=" << std::dec << (int)dma.state
        << " den=" << dma.enabled
        << " dbusy=" << ((dma.status & Z80DMA_STATUS_BUSY) != 0)
        << " a=" << std::hex << std::setw(4) << dma.port_a.address
        << " b=" << std::hex << std::setw(4) << dma.port_b.address
        << " la=" << std::dec << dma.port_a.block_length
        << " lb=" << std::dec << dma.port_b.block_length
        << " fread=" << idp.get_dma_fdc_reads()
        << " fwrite=" << idp.get_dma_mem_writes()
        << " rx=" << std::hex << std::setw(2) << (int)sio.chn[0].rx_data
        << "/" << std::dec << sio.chn[0].rx_ready
        << " fvars="
        << " d" << std::hex << std::setw(2) << (int)drv
        << " t" << std::hex << std::setw(2) << (int)trk
        << " h" << std::hex << std::setw(2) << (int)side
        << " s" << std::hex << std::setw(2) << (int)sec
        << " r" << std::hex << std::setw(2) << (int)retry
        << " n" << std::hex << std::setw(2) << (int)remain
        << " dst=" << std::hex << std::setw(4) << dma_dst
        << " rom=" << (idp.is_rom_enabled() ? 1 : 0)
        << " bank=" << std::dec << (int)idp.get_ram_bank()
        << " ret=" << std::hex << std::setw(4) << ret0
        << " din=" << idp.get_dma_ready_input()
        << " rdy=" << ((pins & Z80DMA_RDY) != 0)
        << " breq=" << ((pins & Z80DMA_BUSREQ) != 0)
        << " back=" << ((pins & Z80DMA_BUSACK) != 0)
        << " int=" << ((pins & Z80_INT) != 0)
        << " iorq=" << ((pins & Z80_IORQ) != 0)
        << " m1=" << ((pins & Z80_M1) != 0)
        << " db=" << std::hex << std::setw(2) << (int)Z80_GET_DATA(pins)
        << " ctc-int="
        << std::hex
        << (int)ctc.chn[0].int_state
        << (int)ctc.chn[1].int_state
        << (int)ctc.chn[2].int_state
        << (int)ctc.chn[3].int_state
        << " ctc-vec="
        << std::setw(2) << (int)ctc.chn[0].int_vector
        << "/" << std::setw(2) << (int)ctc.chn[1].int_vector
        << "/" << std::setw(2) << (int)ctc.chn[2].int_vector
        << "/" << std::setw(2) << (int)ctc.chn[3].int_vector
        << " sio-int="
        << (int)sio.chn[0].int_state
        << (int)sio.chn[1].int_state
        << " sio-vec="
        << std::setw(2) << (int)sio.chn[0].int_vector
        << "/" << std::setw(2) << (int)sio.chn[1].int_vector
        << " pio-int="
        << (int)pio.port[0].int_state
        << (int)pio.port[1].int_state
        << " dma-int="
        << (int)dma.int_state
        << " dvec=" << std::setw(2) << (int)dma.int_vector
        << "\n";
}

void dump_terminal(const partner_crt& idp, const char* tag) {
    std::cout << tag << "\n" << idp.dump_terminal_text() << "\n";
}

void dump_raw_serial(const partner_crt& idp, const char* tag) {
    std::cout << tag << "\n" << idp.dump_raw_serial_text() << "\n";
}

void dump_bytes(const partner_crt& idp, uint16_t base, int count, const char* label) {
    std::cout << label << " @" << std::hex << std::setw(4) << std::setfill('0') << base << ":";
    for (int i = 0; i < count; i++) {
        std::cout << " " << std::hex << std::setw(2)
                  << (int)idp.peek_mem((uint16_t)(base + i));
    }
    std::cout << "\n";
}
}

int main(int argc, char** argv) {
    std::string rom_file = "roms/partner_crt.rom";
    std::string disk_file = "disks/boot.img";
    if (argc > 1) {
        disk_file = argv[1];
    }

    {
        z80dma_t dma{};
        z80dma_init(&dma);
        const uint8_t seq[] = {
            0x05, 0xCF, 0x79, 0x00, 0xE0, 0xFF, 0x00,
            0x14, 0x28, 0x95, 0xF1, 0x0C, 0xFF, 0x8A, 0xCF, 0x01, 0xCF, 0x87
        };
        for (uint8_t b : seq) {
            z80dma_write(&dma, b);
        }
        std::cout
            << "dma-seq"
            << " a=" << std::hex << std::setw(4) << std::setfill('0') << dma.port_a.address
            << " b=" << std::hex << std::setw(4) << dma.port_b.address
            << " la=" << std::dec << dma.port_a.block_length
            << " lb=" << std::dec << dma.port_b.block_length
            << " dir_ab=" << dma.direction_ab
            << " mode=" << (int)dma.mode
            << " en=" << dma.enabled
            << "\n";
    }

    partner_crt idp(terminal_profile::vt52);
    idp.set_force_floppy_boot(true);
    idp.load_rom(rom_file);
    idp.load_disk(0, disk_file);
    idp.reset();
    dump_state(idp, "after-reset");

    uint16_t last_pc = 0xFFFF;
    uint16_t prev_pc = 0xFFFF;
    uint8_t last_fint = 0xFF;
    uint8_t last_phase = 0xFF;
    bool last_irq = false;
    bool last_sense = false;
    int prompt_hits = 0;
    bool injected_f = false;
    bool boot_started = false;
    bool logged_far_pc = false;
    bool logged_idle_dma_window = false;
    bool logged_stale_result = false;
    bool logged_first_low_pc = false;
    bool logged_first_0038 = false;
    bool logged_sp_drop = false;
    bool saw_init_resume = false;
    bool saw_real_return_prompt = false;
    int post_init_trace = 0;
    int int_trace = 0;
    int post_inject_trace = 0;

    for (uint64_t i = 0; i < 30000000ULL; i++) {
        idp.tick();
        const uint16_t pc = idp.get_current_pc();
        const auto& fdc = idp.get_fdc();
        const auto& cpu = idp.get_cpu();
        const uint64_t pins = idp.get_pins();
        const uint8_t fint = idp.get_fdc_int_state();

        if ((cpu.step >= 1655) && (cpu.step <= 1673) && (int_trace < 64)) {
            dump_state(idp, "im2");
            int_trace++;
        }
        if (((pins & (Z80_IORQ | Z80_M1)) == (Z80_IORQ | Z80_M1)) && (int_trace < 64)) {
            dump_state(idp, "ack");
            int_trace++;
        }

        if (!logged_idle_dma_window &&
            boot_started &&
            (fdc.cmd_code == I8272_CMD_READ_DATA) &&
            (fdc.phase == I8272_PHASE_IDLE) &&
            (idp.get_dma().state != Z80DMA_STATE_IDLE) &&
            (fdc.data_idx == fdc.data_len) &&
            (fdc.data_len != 0)) {
            std::cout << "idle-dma-window-begin\n";
            for (int j = 0; j < 32; j++) {
                dump_state(idp, "win");
                idp.tick();
            }
            std::cout << "idle-dma-window-end\n";
            logged_idle_dma_window = true;
        }

        if ((fint != last_fint) || ((uint8_t)fdc.phase != last_phase) ||
            (fdc.irq_request != last_irq) || (fdc.int_pending != last_sense)) {
            dump_state(idp, "edge");
            last_fint = fint;
            last_phase = (uint8_t)fdc.phase;
            last_irq = fdc.irq_request;
            last_sense = fdc.int_pending;
        }

        const bool pc_changed = (pc != last_pc);
        if (post_init_trace > 0 && pc_changed) {
            dump_state(idp, "post-init");
            post_init_trace--;
        }
        if (post_inject_trace > 0 && pc_changed) {
            dump_state(idp, "post-inject");
            post_inject_trace--;
        }

        if (boot_started && !logged_sp_drop && (cpu.sp < 0x8000)) {
            logged_sp_drop = true;
            std::cout << "first-sp-drop prev=" << std::hex << prev_pc << "\n";
            dump_state(idp, "first-sp-drop");
            dump_bytes(idp, (uint16_t)(cpu.sp - 32), 96, "first-sp-drop-stack");
            dump_bytes(idp, pc, 64, "first-sp-drop-code");
        }

        if (pc_changed) {
            prev_pc = last_pc;
            last_pc = pc;
            if (idp.get_tick_count() >= 0x1754f00ULL && idp.get_tick_count() <= 0x1755300ULL) {
                dump_state(idp, "fault-window");
            }
            if (is_interesting_pc(pc)) {
                dump_state(idp, "pc");
            } else if (boot_started && !logged_far_pc && (pc >= 0x4000)) {
                dump_state(idp, "far-pc");
                dump_terminal(idp, "terminal");
                logged_far_pc = true;
            }
            if (boot_started && !logged_first_low_pc && (pc < 0x0100)) {
                logged_first_low_pc = true;
                dump_state(idp, "first-low-pc");
                dump_bytes(idp, 0x0000, 64, "first-low-mem0000");
                dump_bytes(idp, 0x1000, 64, "first-low-mem1000");
            }
            if (boot_started && !logged_first_0038 && (pc == 0x0038)) {
                logged_first_0038 = true;
                std::cout << "first-0038 prev=" << std::hex << prev_pc << "\n";
                dump_state(idp, "first-0038-state");
                dump_bytes(idp, (uint16_t)(cpu.sp - 32), 96, "first-0038-stack-window");
            }
        }

        if (pc == 0x022E) {
            saw_init_resume = true;
            if (post_init_trace == 0) {
                post_init_trace = 256;
            }
        }

        if (!injected_f && is_prompt_wait(pc)) {
            prompt_hits++;
            if (prompt_hits > 4) {
                injected_f = true;
                dump_state(idp, "await-auto-f");
                post_inject_trace = 96;
            }
        }

        if (injected_f && ((pc == 0x03F5) || (pc == 0x03A5) || (pc == 0x0292) || (pc == 0x029B))) {
            boot_started = true;
        }

        if (!boot_started && saw_init_resume && pc == 0x0003 && i > 20400000ULL) {
            dump_state(idp, "first-prompt-return");
            dump_terminal(idp, "terminal-before-first-prompt");
            dump_raw_serial(idp, "raw-before-first-prompt");
            return 3;
        }

        if (boot_started && !saw_real_return_prompt &&
            ((pc == 0x000E) || (pc == 0x009F)) && i > 1000000ULL) {
            saw_real_return_prompt = true;
            dump_state(idp, "real-return-prompt");
            dump_terminal(idp, "terminal-before-reprompt");
            dump_raw_serial(idp, "raw-before-reprompt");
            dump_bytes(idp, 0x0000, 32, "mem0000");
            dump_bytes(idp, 0x0030, 32, "mem0030");
            dump_bytes(idp, 0x0604, 32, "mem0604");
            dump_bytes(idp, 0x0610, 32, "mem0610");
            dump_bytes(idp, 0x083a, 32, "mem083a");
            dump_bytes(idp, 0x100c, 32, "mem100c");
            dump_bytes(idp, 0x4000, 32, "mem4000");
            dump_bytes(idp, 0xF600, 32, "memf600");
            dump_bytes(idp, cpu.sp, 32, "stack");
            return 4;
        }

        if (boot_started && is_prompt_wait(pc) && i > 1000000ULL) {
            dump_state(idp, "returned-prompt");
            dump_terminal(idp, "terminal-after-return");
            dump_raw_serial(idp, "raw-after-return");
            return 2;
        }

        if (!logged_stale_result &&
            boot_started &&
            (fdc.cmd_code == I8272_CMD_READ_DATA) &&
            (fdc.phase == I8272_PHASE_RESULT) &&
            !fdc.irq_request && !fdc.int_pending &&
            (fdc.result_len > 0) &&
            (fdc.result_idx == 0) &&
            (i > 1000000ULL)) {
            dump_state(idp, "stale-read-result");
            logged_stale_result = true;
        }
    }

    dump_state(idp, "timeout");
    dump_bytes(idp, 0x03b0, 128, "mem03b0");
    dump_bytes(idp, 0x0400, 128, "mem0400");
    dump_bytes(idp, 0x0480, 160, "mem0480");
    dump_bytes(idp, 0x0680, 96, "mem0680");
    dump_bytes(idp, 0x09a0, 96, "mem09a0");
    dump_bytes(idp, 0x1000, 64, "mem1000");
    dump_terminal(idp, "terminal");
    dump_raw_serial(idp, "raw-serial");
    return 1;
}
