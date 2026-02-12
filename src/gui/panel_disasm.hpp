#pragma once
#include "../debugger.hpp"

class partner;

namespace panels {
    void render_disasm(partner &emu, bool &paused, dbg_action &action);
}
