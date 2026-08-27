#include "partner_gdp.hpp"
#include "gui/display.hpp"
#include "z80dasm.h"
#include <iostream>
#include <string>
#include <cstdlib>
#include <fstream>

namespace {
struct dasm_context {
    partner_gdp* emu;
    uint16_t fetch_pc;
    std::string text;
};

uint8_t dasm_input(void* user_data) {
    auto* ctx = static_cast<dasm_context*>(user_data);
    return ctx->emu->peek_mem(ctx->fetch_pc++);
}

void dasm_output(char c, void* user_data) {
    auto* ctx = static_cast<dasm_context*>(user_data);
    ctx->text.push_back(c);
}
}

int main() {
    partner_gdp emu;
    const char *hdd_path = std::getenv("IDP_HDD");
    const char *floppy_path = std::getenv("IDP_FLOPPY");
    const bool use_hdd = (hdd_path && hdd_path[0]);
    const bool use_floppy = (floppy_path && floppy_path[0]);
    emu.load_rom("roms/partner_gdp.rom");
    if (use_floppy) {
        emu.load_disk(0, floppy_path);
    } else if (!use_hdd) {
        emu.load_disk(0, "disks/fdd-partner-g.img");
    }
    if (use_hdd) {
        emu.load_hdd(hdd_path);
        std::cout << "[probe-gdp] using HDD: " << hdd_path << "\n";
    }
    emu.reset();

    uint64_t next_key_tick = 0;
    uint64_t script_start_tick = 45000000ULL;
    bool saw_boot_banner = false;
    bool saw_cpm = false;
    bool saw_prompt = false;
    bool auto_cmd_started = false;
    const char *script = std::getenv("IDP_SCRIPT");
    if (!script) script = "";
    int script_pos = 0;
    const char *auto_cmd = std::getenv("IDP_AUTO_CMD");
    if (!auto_cmd) {
        auto_cmd = "";
    }
    int auto_cmd_pos = 0;
    uint64_t next_auto_key_tick = 0;
    uint64_t max_ticks = 120000000ULL;
    if (const char* mt = std::getenv("IDP_MAX_TICKS")) {
        const uint64_t parsed = std::strtoull(mt, nullptr, 10);
        if (parsed > 0) max_ticks = parsed;
    }
    if (const char* st = std::getenv("IDP_SCRIPT_START_TICK")) {
        const uint64_t parsed = std::strtoull(st, nullptr, 10);
        script_start_tick = parsed;
    }

    for (uint64_t i = 0; i < max_ticks; i++) {
        emu.tick();
        const uint16_t pc = emu.get_current_pc();

        if (!use_hdd &&
            (pc == 0x009F || pc == 0x00A1 || pc == 0x00A3) &&
            emu.get_tick_count() >= next_key_tick) {
            emu.key_input('f');
            next_key_tick = emu.get_tick_count() + 200000;
        }
        if ((emu.get_tick_count() > script_start_tick) && script[script_pos] != '\0' && (i % 200000ULL) == 0) {
            emu.key_input((uint8_t)script[script_pos++]);
        }
        if (saw_cpm && auto_cmd[auto_cmd_pos] != '\0' &&
            emu.get_tick_count() >= next_auto_key_tick) {
            auto_cmd_started = true;
            emu.key_input((uint8_t)auto_cmd[auto_cmd_pos++]);
            next_auto_key_tick = emu.get_tick_count() + 500000ULL;
        }

        if ((i % 2000000ULL) == 0) {
            std::cout << "[probe-gdp] tick=" << emu.get_tick_count()
                      << " pc=0x" << std::hex << pc << std::dec << "\n";
            const std::string raw = emu.dump_raw_serial_text();
            if (!saw_boot_banner && raw.find("Boot V") != std::string::npos) {
                saw_boot_banner = true;
                std::cout << "[probe-gdp] boot banner seen at tick " << emu.get_tick_count() << "\n";
            }
            if (!saw_cpm && (raw.find("CP/M") != std::string::npos)) {
                saw_cpm = true;
                std::cout << "[probe-gdp] CP/M text seen at tick " << emu.get_tick_count() << "\n";
                if (auto_cmd[0]) {
                    std::cout << "[probe-gdp] auto command armed: " << auto_cmd << "\n";
                    next_auto_key_tick = emu.get_tick_count() + 1000000ULL;
                }
            }
            if (!saw_prompt &&
                (raw.find("A>") != std::string::npos ||
                 raw.find("B>") != std::string::npos ||
                 raw.find("C>") != std::string::npos)) {
                saw_prompt = true;
                std::cout << "[probe-gdp] CP/M prompt seen at tick " << emu.get_tick_count() << "\n";
                break;
            }
        }
    }

    if (const char* dump = std::getenv("IDP_DUMP_PGM")) {
        display disp;
        emu.render_to(disp);
        std::ofstream out(dump, std::ios::binary);
        if (out) {
            out << "P5\n" << display::FB_W << " " << display::FB_H << "\n255\n";
            out.write((const char*)disp.data(), display::FB_W * display::FB_H);
            std::cout << "[probe-gdp] wrote " << dump << "\n";
        }
    }

    if (const char* dump_avdc = std::getenv("IDP_DUMP_AVDC")) {
        const auto& avdc = emu.get_avdc();
        std::ofstream out(dump_avdc, std::ios::binary);
        if (out) {
            out.write((const char*)avdc.vram, sizeof(avdc.vram));
            std::cout << "[probe-gdp] wrote " << dump_avdc << "\n";
        }
    }

    if (const char* dump_mem = std::getenv("IDP_DUMP_MEM")) {
        std::ofstream out(dump_mem, std::ios::binary);
        if (out) {
            for (int a = 0; a < 0x10000; a++) {
                const uint8_t b = emu.peek_mem((uint16_t)a);
                out.put((char)b);
            }
            std::cout << "[probe-gdp] wrote " << dump_mem << "\n";
        }
    }

    std::cout << "pc=" << std::hex << emu.get_current_pc()
              << " ticks=" << std::dec << emu.get_tick_count() << "\n";
    const auto &cpu = emu.get_cpu();
    std::cout << "cpu af=0x" << std::hex << cpu.af
              << " bc=0x" << cpu.bc
              << " de=0x" << cpu.de
              << " hl=0x" << cpu.hl
              << " sp=0x" << cpu.sp
              << " i=0x" << std::hex << (int)cpu.i
              << std::dec
              << " im=" << (int)cpu.im
              << " iff1=" << (cpu.iff1 ? 1 : 0)
              << " iff2=" << (cpu.iff2 ? 1 : 0)
              << "\n";
    const uint16_t cur_pc = emu.get_current_pc();
    std::cout << "dasm_pc\n";
    {
        uint16_t pc = cur_pc;
        for (int i = 0; i < 12; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_86d0\n";
    {
        uint16_t pc = 0x86D0;
        for (int i = 0; i < 24; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_8b60\n";
    {
        uint16_t pc = 0x8B60;
        for (int i = 0; i < 32; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_8b97\n";
    {
        uint16_t pc = 0x8B97;
        for (int i = 0; i < 20; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_8bf0\n";
    {
        uint16_t pc = 0x8BF0;
        for (int i = 0; i < 20; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_8700\n";
    {
        uint16_t pc = 0x8700;
        for (int i = 0; i < 28; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_ac40\n";
    {
        uint16_t pc = 0xAC40;
        for (int i = 0; i < 40; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_acd0\n";
    {
        uint16_t pc = 0xACD0;
        for (int i = 0; i < 28; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_9984\n";
    {
        uint16_t pc = 0x9984;
        for (int i = 0; i < 36; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_9a3f\n";
    {
        uint16_t pc = 0x9A3F;
        for (int i = 0; i < 36; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_9a85\n";
    {
        uint16_t pc = 0x9A85;
        for (int i = 0; i < 32; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_fab4\n";
    {
        uint16_t pc = 0xFAB4;
        for (int i = 0; i < 32; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "dasm_fea7\n";
    {
        uint16_t pc = 0xFEA7;
        for (int i = 0; i < 64; i++) {
            dasm_context ctx{&emu, pc, {}};
            const uint16_t next = z80dasm_op(pc, dasm_input, dasm_output, &ctx);
            std::cout << std::hex << pc << ": " << ctx.text << "\n";
            pc = next;
        }
    }
    std::cout << "mem_pc";
    for (int i = -16; i < 48; i++) {
        const uint16_t a = (uint16_t)(cur_pc + i);
        if (((i + 16) % 16) == 0) std::cout << "\n" << std::hex << a << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem(a);
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_b60";
    for (int i = 0; i < 0x40; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0x0B60 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0x0B60 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_ivt";
    for (int i = 0; i < 0xA0; i++) {
        const uint16_t a = (uint16_t)(((uint16_t)cpu.i << 8) + i);
        if ((i % 16) == 0) std::cout << "\n" << std::hex << a << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem(a);
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_acf0";
    for (int i = 0; i < 0x20; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0xACF0 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0xACF0 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_ff10";
    for (int i = 0; i < 0x30; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0xFF10 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0xFF10 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "ff19=0x" << std::hex << (int)emu.peek_mem(0xFF19)
              << " ff1a=0x" << (int)emu.peek_mem(0xFF1A)
              << " acf4=0x" << (int)emu.peek_mem(0xACF4)
              << " acf5=0x" << (int)emu.peek_mem(0xACF5)
              << std::dec << "\n";
    std::cout << "mem_f9a0";
    for (int i = 0; i < 0x40; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0xF9A0 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0xF9A0 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_f9c0";
    for (int i = 0; i < 0x40; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0xF9C0 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0xF9C0 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_aac0";
    for (int i = 0; i < 0x40; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0xAAC0 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0xAAC0 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_aa00";
    for (int i = 0; i < 0x60; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0xAA00 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0xAA00 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_86d0";
    for (int i = 0; i < 0xB0; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0x86D0 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0x86D0 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_8740";
    for (int i = 0; i < 0x80; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0x8740 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0x8740 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_8b60";
    for (int i = 0; i < 0xA0; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0x8B60 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0x8B60 + i));
    }
    std::cout << std::dec << "\n";
    std::cout << "mem_8c40";
    for (int i = 0; i < 0x60; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0x8C40 + i) << ":";
        std::cout << " " << std::hex << (int)emu.peek_mem((uint16_t)(0x8C40 + i));
    }
    std::cout << std::dec << "\n";
    const auto &avdc = emu.get_avdc();
    const auto &ef = emu.get_ef9367();
    const auto &hdc = emu.get_hdc();
    const auto &sasi = emu.get_sasi();
    const auto &dma = emu.get_dma();
    const auto &ctc = emu.get_ctc();
    const auto &pio = emu.get_pio();
    const auto &io_cnt = emu.get_io_counters();
    const auto &avdc_port_wr = emu.get_avdc_port_writes();
    const auto &avdc_cmd_wr = emu.get_avdc_cmd_writes();
    const uint64_t avdc_char_wr = emu.get_avdc_char_writes();
    const uint64_t avdc_char_nonspace_wr = emu.get_avdc_char_nonspace_writes();
    const auto &avdc_char_hist = emu.get_avdc_char_hist();
    std::cout << "avdc start1=0x" << std::hex << avdc.start1_addr
              << " start2=0x" << avdc.start2_addr
              << " cursor=0x" << avdc.cursor_addr
              << " ptr=0x" << avdc.display_ptr_addr
              << " status=0x" << (int)avdc.status
              << std::dec << "\n";
    int avdc_nonspace = 0;
    int avdc_first_nonspace = -1;
    int avdc_last_nonspace = -1;
    for (int i = 0; i < 16384; i++) {
        if (avdc.vram[i] > 0x20) {
            avdc_nonspace++;
            if (avdc_first_nonspace < 0) avdc_first_nonspace = i;
            avdc_last_nonspace = i;
        }
    }
    std::cout << "avdc nonspace=" << avdc_nonspace
              << " first=0x" << std::hex << (avdc_first_nonspace < 0 ? 0 : avdc_first_nonspace)
              << " last=0x" << (avdc_last_nonspace < 0 ? 0 : avdc_last_nonspace)
              << std::dec << "\n";
    std::cout << "avdc_nonspace_addrs:";
    int avdc_listed = 0;
    for (int i = 0; i < 16384 && avdc_listed < 64; i++) {
        if (avdc.vram[i] > 0x20) {
            std::cout << " 0x" << std::hex << i << "=0x" << (int)avdc.vram[i];
            avdc_listed++;
        }
    }
    std::cout << std::dec << "\n";
    std::cout << "avdc_line0:";
    for (int i = 0; i < 80; i++) {
        const uint8_t ch = avdc.vram[(avdc.start1_addr + i) & 0x3FFF];
        std::cout << (char)((ch >= 0x20 && ch < 0x7F) ? ch : '.');
    }
    std::cout << "\n";
    const bool avdc_row_mode = avdc.use_row_table || ((avdc.ir[2] & 0x80) != 0);
    std::cout << "avdc_row_mode=" << (avdc_row_mode ? 1 : 0)
              << " bool=" << (avdc.use_row_table ? 1 : 0)
              << " ir2=0x" << std::hex << (int)avdc.ir[2]
              << std::dec << "\n";
    if (avdc_row_mode) {
        std::cout << "avdc_rowtbl\n";
        const uint16_t rowtbl = (uint16_t)(avdc.start2_addr & 0x3FFFu);
        for (int row = 0; row < 8; row++) {
            const uint16_t p = (uint16_t)((rowtbl + (row * 2)) & 0x3FFFu);
            const uint16_t line = (uint16_t)((avdc.vram[p] | ((uint16_t)avdc.vram[(p + 1) & 0x3FFFu] << 8)) & 0x3FFFu);
            std::cout << "row " << row
                      << " ptr=0x" << std::hex << p
                      << " line=0x" << line
                      << std::dec << " text=\"";
            for (int col = 0; col < std::min(40, (int)avdc.chars_per_row); col++) {
                const uint8_t ch = avdc.vram[(line + col) & 0x3FFFu];
                std::cout << (char)((ch >= 0x20 && ch < 0x7F) ? ch : '.');
            }
            std::cout << "\"\n";
        }
    }
    const uint16_t cur_base = (uint16_t)(avdc.cursor_addr & 0x3FF0u);
    std::cout << "avdc_mem_cursor";
    for (int i = 0; i < 0x60; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << ((cur_base + i) & 0x3FFFu) << ":";
        std::cout << " " << std::hex << (int)avdc.vram[(cur_base + i) & 0x3FFFu];
    }
    std::cout << std::dec << "\n";
    const uint16_t ptr_base = (uint16_t)(avdc.display_ptr_addr & 0x3FF0u);
    std::cout << "avdc_mem_ptr";
    for (int i = 0; i < 0x60; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << ((ptr_base + i) & 0x3FFFu) << ":";
        std::cout << " " << std::hex << (int)avdc.vram[(ptr_base + i) & 0x3FFFu];
    }
    std::cout << std::dec << "\n";
    const uint16_t cur_swap_base = (uint16_t)((((avdc.cursor_addr & 0xFFu) << 8) | (avdc.cursor_addr >> 8)) & 0x3FF0u);
    std::cout << "avdc_mem_cursor_swap";
    for (int i = 0; i < 0x60; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << ((cur_swap_base + i) & 0x3FFFu) << ":";
        std::cout << " " << std::hex << (int)avdc.vram[(cur_swap_base + i) & 0x3FFFu];
    }
    std::cout << std::dec << "\n";
    const uint16_t ptr_swap_base = (uint16_t)((((avdc.display_ptr_addr & 0xFFu) << 8) | (avdc.display_ptr_addr >> 8)) & 0x3FF0u);
    std::cout << "avdc_mem_ptr_swap";
    for (int i = 0; i < 0x60; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << ((ptr_swap_base + i) & 0x3FFFu) << ":";
        std::cout << " " << std::hex << (int)avdc.vram[(ptr_swap_base + i) & 0x3FFFu];
    }
    std::cout << std::dec << "\n";
    std::cout << "avdc_mem_0ff0";
    for (int i = 0; i < 0x140; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0x0FF0 + i) << ":";
        std::cout << " " << std::hex << (int)avdc.vram[(0x0FF0 + i) & 0x3FFF];
    }
    std::cout << std::dec << "\n";
    std::cout << "avdc_mem_0e00";
    for (int i = 0; i < 0x220; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0x0E00 + i) << ":";
        std::cout << " " << std::hex << (int)avdc.vram[(0x0E00 + i) & 0x3FFF];
    }
    std::cout << std::dec << "\n";
    std::cout << "avdc_mem_01c0";
    for (int i = 0; i < 0x1A0; i++) {
        if ((i % 16) == 0) std::cout << "\n" << std::hex << (0x01C0 + i) << ":";
        std::cout << " " << std::hex << (int)avdc.vram[(0x01C0 + i) & 0x3FFF];
    }
    std::cout << std::dec << "\n";
    std::cout << "avdc_ir:";
    for (int i = 0; i < 10; i++) {
        std::cout << " " << std::hex << (int)avdc.ir[i];
    }
    std::cout << std::dec << "\n";
    std::cout << "io ef(r/w)=" << io_cnt.ef_rd << "/" << io_cnt.ef_wr
              << " avdc(r/w)=" << io_cnt.avdc_rd << "/" << io_cnt.avdc_wr
              << " pio(r/w)=" << io_cnt.pio_rd << "/" << io_cnt.pio_wr
              << "\n";
    std::cout << "gdp pioA=0x" << std::hex << (int)emu.get_gdp_pio_port_a()
              << " scroll=0x" << (int)emu.get_gdp_scroll()
              << std::dec << "\n";
    std::cout << "avdc_port_wr:";
    for (int i = 0; i < 16; i++) {
        if (avdc_port_wr[i]) {
            std::cout << " " << std::hex << i << ":" << std::dec << avdc_port_wr[i];
        }
    }
    std::cout << "\n";
    std::cout << "avdc_cmd_wr:";
    for (int i = 0; i < 256; i++) {
        if (avdc_cmd_wr[i]) {
            std::cout << " " << std::hex << i << ":" << std::dec << avdc_cmd_wr[i];
        }
    }
    std::cout << "\n";
    std::cout << "avdc_char_wr=" << avdc_char_wr
              << " nonspace=" << avdc_char_nonspace_wr << "\n";
    std::cout << "avdc_char_top:";
    for (int i = 0; i < 256; i++) {
        if (avdc_char_hist[i] >= 4) {
            std::cout << " " << std::hex << i << ":" << std::dec << avdc_char_hist[i];
        }
    }
    std::cout << "\n";
    std::cout << "hdc phase=" << (int)hdc.phase
              << " present=" << (hdc.present ? 1 : 0)
              << " busy=" << (hdc.busy ? 1 : 0)
              << " cfg=" << (int)hdc.cfg_kind << " len=" << (int)hdc.cfg_len << "/" << (int)hdc.cfg_expected
              << " data=" << hdc.data_idx << "/" << hdc.data_len
              << " resp=" << (int)hdc.response_idx << "/" << (int)hdc.response_len
              << " cc=" << (hdc.config_complete ? 1 : 0)
              << " rr=" << (hdc.response_ready ? 1 : 0)
              << " err=0x" << std::hex << (int)hdc.error
              << std::dec << "\n";
    std::cout << "hdc cfgbuf:"
              << " " << std::hex << (int)hdc.cfg_buf[0]
              << " " << (int)hdc.cfg_buf[1]
              << " " << (int)hdc.cfg_buf[2]
              << " " << (int)hdc.cfg_buf[3]
              << " " << (int)hdc.cfg_buf[4]
              << " " << (int)hdc.cfg_buf[5]
              << std::dec << "\n";
    std::cout << "sasi sess=" << (sasi.session_active ? 1 : 0)
              << " sel=" << (sasi.sel_latched ? 1 : 0)
              << " den=" << (sasi.data_enable ? 1 : 0)
              << " drq_en=" << (sasi.drq_enable ? 1 : 0)
              << " drq=" << (sasi.drq ? 1 : 0)
              << " ctrl=0x" << std::hex << (int)sasi.last_ctrl
              << std::dec << "\n";
    std::cout << "dma int_state=" << std::hex << (int)dma.int_state
              << " vec=" << (int)dma.int_vector
              << std::dec << "\n";
    std::cout << "ctc int_state="
              << std::hex
              << (int)ctc.chn[0].int_state << "/"
              << (int)ctc.chn[1].int_state << "/"
              << (int)ctc.chn[2].int_state << "/"
              << (int)ctc.chn[3].int_state
              << " vec="
              << (int)ctc.chn[0].int_vector << "/"
              << (int)ctc.chn[1].int_vector << "/"
              << (int)ctc.chn[2].int_vector << "/"
              << (int)ctc.chn[3].int_vector
              << std::dec << "\n";
    std::cout << "pio int_state="
              << std::hex
              << (int)pio.port[0].int_state << "/"
              << (int)pio.port[1].int_state
              << " vec="
              << (int)pio.port[0].int_vector << "/"
              << (int)pio.port[1].int_vector
              << std::dec << "\n";
    const auto &sio = emu.get_sio();
    const auto &sio2 = emu.get_sio2();
    std::cout << "sioA rx_ready=" << (sio.chn[Z80SIO_CHANNEL_A].rx_ready ? 1 : 0)
              << " tx_ready=" << (sio.chn[Z80SIO_CHANNEL_A].tx_ready ? 1 : 0)
              << " int_state=" << std::hex << (int)sio.chn[Z80SIO_CHANNEL_A].int_state
              << " vec=" << (int)sio.chn[Z80SIO_CHANNEL_A].int_vector
              << " wr0=" << std::hex << (int)sio.chn[Z80SIO_CHANNEL_A].wr[0]
              << " wr1=" << (int)sio.chn[Z80SIO_CHANNEL_A].wr[1]
              << " wr2=" << (int)sio.chn[Z80SIO_CHANNEL_A].wr[2]
              << " wr3=" << (int)sio.chn[Z80SIO_CHANNEL_A].wr[3]
              << " wr5=" << (int)sio.chn[Z80SIO_CHANNEL_A].wr[5]
              << std::dec << "\n";
    std::cout << "sioB rx_ready=" << (sio.chn[Z80SIO_CHANNEL_B].rx_ready ? 1 : 0)
              << " tx_ready=" << (sio.chn[Z80SIO_CHANNEL_B].tx_ready ? 1 : 0)
              << " int_state=" << std::hex << (int)sio.chn[Z80SIO_CHANNEL_B].int_state
              << " vec=" << (int)sio.chn[Z80SIO_CHANNEL_B].int_vector
              << " wr0=" << std::hex << (int)sio.chn[Z80SIO_CHANNEL_B].wr[0]
              << " wr1=" << (int)sio.chn[Z80SIO_CHANNEL_B].wr[1]
              << " wr2=" << (int)sio.chn[Z80SIO_CHANNEL_B].wr[2]
              << " wr3=" << (int)sio.chn[Z80SIO_CHANNEL_B].wr[3]
              << " wr5=" << (int)sio.chn[Z80SIO_CHANNEL_B].wr[5]
              << std::dec << "\n";
    std::cout << "sio2A rx_ready=" << (sio2.chn[Z80SIO_CHANNEL_A].rx_ready ? 1 : 0)
              << " tx_ready=" << (sio2.chn[Z80SIO_CHANNEL_A].tx_ready ? 1 : 0)
              << " int_state=" << std::hex << (int)sio2.chn[Z80SIO_CHANNEL_A].int_state
              << " vec=" << (int)sio2.chn[Z80SIO_CHANNEL_A].int_vector
              << " wr0=" << std::hex << (int)sio2.chn[Z80SIO_CHANNEL_A].wr[0]
              << " wr1=" << (int)sio2.chn[Z80SIO_CHANNEL_A].wr[1]
              << " wr2=" << (int)sio2.chn[Z80SIO_CHANNEL_A].wr[2]
              << " wr3=" << (int)sio2.chn[Z80SIO_CHANNEL_A].wr[3]
              << " wr5=" << (int)sio2.chn[Z80SIO_CHANNEL_A].wr[5]
              << std::dec << "\n";
    std::cout << "sio2B rx_ready=" << (sio2.chn[Z80SIO_CHANNEL_B].rx_ready ? 1 : 0)
              << " tx_ready=" << (sio2.chn[Z80SIO_CHANNEL_B].tx_ready ? 1 : 0)
              << " int_state=" << std::hex << (int)sio2.chn[Z80SIO_CHANNEL_B].int_state
              << " vec=" << (int)sio2.chn[Z80SIO_CHANNEL_B].int_vector
              << " wr0=" << std::hex << (int)sio2.chn[Z80SIO_CHANNEL_B].wr[0]
              << " wr1=" << (int)sio2.chn[Z80SIO_CHANNEL_B].wr[1]
              << " wr2=" << (int)sio2.chn[Z80SIO_CHANNEL_B].wr[2]
              << " wr3=" << (int)sio2.chn[Z80SIO_CHANNEL_B].wr[3]
              << " wr5=" << (int)sio2.chn[Z80SIO_CHANNEL_B].wr[5]
              << std::dec << "\n";
    std::cout << "ef cmd=0x" << std::hex << (int)ef.command
              << " cr1=0x" << (int)ef.cr1
              << " cr2=0x" << (int)ef.cr2
              << " chsz=0x" << (int)ef.ch_size
              << " dx=0x" << (int)ef.dx
              << " dy=0x" << (int)ef.dy
              << " x=0x" << ef.x
              << " y=0x" << ef.y
              << " status=0x" << (int)ef.status
              << std::dec << "\n";
    std::cout << "auto_cmd_started=" << (auto_cmd_started ? 1 : 0)
              << " sent=" << auto_cmd_pos << "\n";
    std::cout << "terminal\n" << emu.dump_terminal_text() << "\n";
    std::cout << "raw\n" << emu.dump_raw_serial_text() << "\n";
    return 0;
}
