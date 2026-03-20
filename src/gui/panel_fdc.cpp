#include "panel_fdc.hpp"
#include "../partner.hpp"
#include <imgui.h>

namespace {

const char *phase_name(i8272_phase_t phase)
{
    switch (phase) {
    case I8272_PHASE_IDLE:    return "IDLE";
    case I8272_PHASE_COMMAND: return "COMMAND";
    case I8272_PHASE_EXECUTE: return "EXECUTE";
    case I8272_PHASE_RESULT:  return "RESULT";
    default:                  return "???";
    }
}

const char *cmd_name(uint8_t code)
{
    switch (code) {
    case I8272_CMD_SPECIFY:       return "SPECIFY";
    case I8272_CMD_SENSE_DRIVE:   return "SENSE DRV";
    case I8272_CMD_RECALIBRATE:   return "RECALIB";
    case I8272_CMD_SENSE_INT:     return "SENSE INT";
    case I8272_CMD_READ_DATA:     return "READ DATA";
    case I8272_CMD_WRITE_DATA:    return "WRITE DATA";
    case I8272_CMD_READ_ID:       return "READ ID";
    case I8272_CMD_FORMAT_TRACK:  return "FORMAT";
    case I8272_CMD_SEEK:          return "SEEK";
    default:                      return "---";
    }
}

} // anonymous namespace

void panels::render_fdc(partner &emu)
{
    ImGui::Begin("i8272 FDC", nullptr,
                 ImGuiWindowFlags_NoCollapse);

    const i8272_t &fdc = emu.get_fdc();

    // Phase and command
    ImGui::Text("Phase: %s", phase_name(fdc.phase));
    ImGui::Text("Cmd:   %s (%02X)", cmd_name(fdc.cmd_code), fdc.cmd_code);

    ImGui::Separator();

    // MSR bits
    uint8_t msr = fdc.msr;
    ImGui::Text("MSR: %02X  %c%c%c%c",
                msr,
                (msr & I8272_MSR_RQM) ? 'R' : '-',
                (msr & I8272_MSR_DIO) ? 'D' : '-',
                (msr & I8272_MSR_EXM) ? 'E' : '-',
                (msr & I8272_MSR_CB)  ? 'B' : '-');
    ImGui::Text("  RQM=%d DIO=%d EXM=%d CB=%d",
                (msr >> 7) & 1, (msr >> 6) & 1,
                (msr >> 5) & 1, (msr >> 4) & 1);

    ImGui::Separator();

    // Status registers
    ImGui::Text("ST0: %02X  ST1: %02X  ST2: %02X",
                fdc.st0, fdc.st1, fdc.st2);

    // Interrupt
    ImGui::Text("INT: sense=%s irq=%s delay=%u vec=%02X",
                fdc.int_pending ? "PEND" : "----",
                fdc.irq_request ? "REQ" : "---",
                fdc.irq_delay,
                emu.get_fdc_int_vector());

    ImGui::Separator();

    // Drive state
    ImGui::Text("Motor: %02X", emu.get_fdc_motor());
    for (int i = 0; i < 2; i++)
    {
        const i8272_drive_t &drv = fdc.drive[i];
        ImGui::Text("D%d: T=%02d H=%d %s%s",
                    i, drv.track, drv.head,
                    drv.motor ? "MOT " : "",
                    drv.ready ? "RDY" : "");
    }

    ImGui::End();
}
