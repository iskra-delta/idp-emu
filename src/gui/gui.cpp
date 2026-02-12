#include "gui.hpp"
#include "panel_display.hpp"
#include "panel_disasm.hpp"
#include "panel_regs.hpp"
#include "panel_fdc.hpp"
#include "panel_sio.hpp"
#include "panel_pio.hpp"
#include "panel_dma.hpp"
#include "panel_rtc.hpp"
#include "../partner.hpp"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL.h>
#include <SDL_opengl.h>

bool gui::init(const std::string &title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
        return false;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    window_ = SDL_CreateWindow(
        title.c_str(),
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        width, height,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!window_)
        return false;

    SDL_SetWindowMinimumSize(window_, 1024, 600);

    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_)
        return false;

    SDL_GL_MakeCurrent(window_, gl_context_);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    ImGui::StyleColorsDark();

    ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_);
    ImGui_ImplOpenGL3_Init("#version 330");

    display_.init();

    return true;
}

void gui::shutdown()
{
    display_.shutdown();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (gl_context_)
        SDL_GL_DeleteContext(gl_context_);
    if (window_)
        SDL_DestroyWindow(window_);
    SDL_Quit();
}

bool gui::process_events(bool &paused, dbg_action &action)
{
    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT)
            return false;

        if (event.type == SDL_KEYDOWN && !ImGui::GetIO().WantCaptureKeyboard)
        {
            switch (event.key.keysym.sym)
            {
            case SDLK_ESCAPE:
                return false;
            case SDLK_F5:
                paused = !paused;
                break;
            case SDLK_F11:
                if (paused)
                    action = dbg_action::STEP_INTO;
                break;
            case SDLK_F10:
                if (paused)
                    action = dbg_action::STEP_OVER;
                break;
            case SDLK_RETURN:
                if (!paused)
                    key_buf_.push_back(0x0D);
                break;
            case SDLK_BACKSPACE:
                if (!paused)
                    key_buf_.push_back(0x08);
                break;
            case SDLK_TAB:
                if (!paused)
                    key_buf_.push_back(0x09);
                break;
            }
        }

        // Text input for printable characters (when emulation is running)
        if (event.type == SDL_TEXTINPUT && !paused && !ImGui::GetIO().WantCaptureKeyboard)
        {
            for (const char *p = event.text.text; *p; p++)
            {
                uint8_t ch = (uint8_t)*p;
                if (ch >= 32 && ch < 127)
                    key_buf_.push_back(ch);
            }
        }
    }
    return true;
}

void gui::begin_frame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();
}

void gui::render_panels(partner &emu, bool &paused, dbg_action &action)
{
    display_.update();

    int win_w, win_h;
    SDL_GetWindowSize(window_, &win_w, &win_h);

    float menu_h = ImGui::GetFrameHeight();
    float panel_w = 620.0f;
    float main_w = (float)win_w - panel_w;
    float main_h = (float)win_h - menu_h;
    float reg_h = 280.0f;
    float sio_h = 240.0f;
    float pio_h = 220.0f;
    float dma_h = 220.0f;
    float rtc_h = 200.0f;
    float fdc_h = 200.0f;

    // Menu bar
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("Emulation"))
        {
            if (ImGui::MenuItem(paused ? "Run" : "Pause", "Space"))
                paused = !paused;
            if (ImGui::MenuItem("Reset"))
                emu.reset();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit", "Esc"))
            {
                SDL_Event quit_event;
                quit_event.type = SDL_QUIT;
                SDL_PushEvent(&quit_event);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Registers", nullptr, &show_registers_);
            ImGui::MenuItem("Disassembly", nullptr, &show_disasm_);
            ImGui::MenuItem("Floppy Disk Controller", nullptr, &show_fdc_);
            ImGui::MenuItem("Z80 SIO", nullptr, &show_sio_);
            ImGui::MenuItem("Z80 PIO", nullptr, &show_pio_);
            ImGui::MenuItem("Z80 DMA", nullptr, &show_dma_);
            ImGui::MenuItem("MM58167 RTC", nullptr, &show_rtc_);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Display panel (always visible, fills main area)
    ImGui::SetNextWindowPos({0, menu_h});
    ImGui::SetNextWindowSize({main_w, main_h});
    panels::render_display(display_);

    float right_y = menu_h;

    // Registers panel
    if (show_registers_)
    {
        ImGui::SetNextWindowPos({main_w, right_y});
        ImGui::SetNextWindowSize({panel_w, reg_h});
        panels::render_registers(emu);
        right_y += reg_h;
    }

    if (show_sio_)
    {
        ImGui::SetNextWindowPos({main_w, right_y});
        ImGui::SetNextWindowSize({panel_w, sio_h});
        panels::render_sio(emu);
        right_y += sio_h;
    }

    if (show_pio_)
    {
        ImGui::SetNextWindowPos({main_w, right_y});
        ImGui::SetNextWindowSize({panel_w, pio_h});
        panels::render_pio(emu);
        right_y += pio_h;
    }

    if (show_dma_)
    {
        ImGui::SetNextWindowPos({main_w, right_y});
        ImGui::SetNextWindowSize({panel_w, dma_h});
        panels::render_dma(emu);
        right_y += dma_h;
    }

    if (show_rtc_)
    {
        ImGui::SetNextWindowPos({main_w, right_y});
        ImGui::SetNextWindowSize({panel_w, rtc_h});
        panels::render_rtc(emu);
        right_y += rtc_h;
    }

    float disasm_h = main_h - (right_y - menu_h);
    if (show_fdc_)
    {
        disasm_h -= fdc_h;
    }

    // Disassembly panel
    if (show_disasm_)
    {
        if (disasm_h < 100.0f) disasm_h = 100.0f;
        ImGui::SetNextWindowPos({main_w, right_y});
        ImGui::SetNextWindowSize({panel_w, disasm_h});
        panels::render_disasm(emu, paused, action);
    }

    // FDC panel (always at bottom when visible)
    if (show_fdc_)
    {
        float fdc_y = menu_h + main_h - fdc_h;
        ImGui::SetNextWindowPos({main_w, fdc_y});
        ImGui::SetNextWindowSize({panel_w, fdc_h});
        panels::render_fdc(emu);
    }
}

std::vector<uint8_t> gui::drain_keys()
{
    auto keys = std::move(key_buf_);
    key_buf_.clear();
    return keys;
}

void gui::end_frame()
{
    ImGui::Render();

    int display_w, display_h;
    SDL_GL_GetDrawableSize(window_, &display_w, &display_h);
    glViewport(0, 0, display_w, display_h);
    glClearColor(0.06f, 0.06f, 0.06f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    SDL_GL_SwapWindow(window_);
}
