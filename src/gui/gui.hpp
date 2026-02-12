#pragma once
#include "display.hpp"
#include <SDL.h>
#include <string>

class partner;

class gui
{
public:
    bool init(const std::string &title, int width, int height);
    void shutdown();

    bool process_events(bool &paused);

    void begin_frame();
    void render_panels(partner &emu, bool &paused);
    void end_frame();

    display &get_display() { return display_; }

private:
    SDL_Window *window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    display display_;

    bool show_registers_ = true;
    bool show_disasm_ = true;
};
