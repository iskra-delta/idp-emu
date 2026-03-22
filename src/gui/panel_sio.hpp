#pragma once
#include <cstdint>
#include <vector>

class partner;

namespace panels {
    void render_sio(partner &emu, std::vector<uint8_t> &inject_buf, bool *p_open);
}
