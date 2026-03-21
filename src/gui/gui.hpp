#pragma once
#include "display.hpp"
#include "../debugger.hpp"
#include "../terminal/terminal_factory.hpp"
#include <SDL.h>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>

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
    void set_terminal_profile(terminal_profile profile) { terminal_profile_ = profile; }

    std::vector<uint8_t> drain_keys();

private:
    void blink_host_key(const char *host_key);
    bool is_host_key_blinking(const char *host_key);

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
    bool show_scn2674_ = false;
    bool show_ef9367_ = false;
    bool show_keyboard_ = false;
    bool startup_layout_applied_ = false;
    terminal_profile terminal_profile_ = terminal_profile::vt52;

    std::vector<uint8_t> key_buf_;
    std::unordered_map<std::string, uint32_t> key_blink_until_ms_{};
    static constexpr uint32_t key_blink_ms_ = 130;
};
