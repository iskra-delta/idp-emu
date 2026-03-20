#pragma once
#include "display.hpp"
#include "../debugger.hpp"
#include <SDL.h>
#include <string>
#include <vector>
#include <cstdint>

class partner;

class gui
{
public:
    bool init(const std::string &title, int width, int height);
    void shutdown();

    bool process_events(bool &paused, dbg_action &action);

    void begin_frame();
    void render_panels(partner &emu, bool &paused, dbg_action &action);
    void end_frame();

    display &get_display() { return display_; }

    std::vector<uint8_t> drain_keys();

private:
    SDL_Window *window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    display display_;

    bool show_registers_ = false;
    bool show_disasm_ = true;
    bool show_fdc_ = false;
    bool show_sio_ = false;
    bool show_pio_ = false;
    bool show_dma_ = false;
    bool show_xebec_ = false;
    bool show_rtc_ = false;
    bool startup_layout_applied_ = false;

    std::vector<uint8_t> key_buf_;
};
