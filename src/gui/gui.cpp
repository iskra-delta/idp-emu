#include "gui.hpp"
#include "panel_display.hpp"
#include "panel_disasm.hpp"
#include "panel_regs.hpp"
#include "panel_fdc.hpp"
#include "panel_sio.hpp"
#include "panel_pio.hpp"
#include "panel_dma.hpp"
#include "panel_rtc.hpp"
#include "panel_xebec.hpp"
#include "../partner.hpp"

#include <imgui.h>
#include <imgui_internal.h>
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
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImGui::StyleColorsDark();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGuiStyle &style = ImGui::GetStyle();
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_);
    ImGui_ImplOpenGL3_Init("#version 330");

    // Enable SDL text input events for terminal key injection.
    SDL_StartTextInput();

    display_.init();

    return true;
}

void gui::shutdown()
{
    display_.shutdown();
    SDL_StopTextInput();

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

        if (event.type == SDL_KEYDOWN)
        {
            // Always allow debugger hotkeys even when ImGui has keyboard navigation focus.
            switch (event.key.keysym.sym)
            {
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
            default:
                break;
            }

            // Route terminal keys unless user is actively editing an ImGui text field.
            if (!ImGui::GetIO().WantTextInput)
            {
                switch (event.key.keysym.sym)
                {
                case SDLK_ESCAPE:
                    return false;
                case SDLK_RETURN:
                    key_buf_.push_back(0x0D);
                    break;
                case SDLK_BACKSPACE:
                    key_buf_.push_back(0x08);
                    break;
                case SDLK_TAB:
                    key_buf_.push_back(0x09);
                    break;
                default:
                    break;
                }
            }
        }

        // Text input for printable characters (also while paused for debugger injection)
        if (event.type == SDL_TEXTINPUT)
        {
            if (ImGui::GetIO().WantTextInput)
                continue;

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
            ImGui::MenuItem("Xebec S1410", nullptr, &show_xebec_);
            ImGui::MenuItem("MM58167 RTC", nullptr, &show_rtc_);
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Dockspace for dockable panels.
    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_None);
    if (!startup_layout_applied_)
    {
        startup_layout_applied_ = true;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

        ImGuiID dock_main = dockspace_id;
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.32f, nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Partner Display", dock_main);
        ImGui::DockBuilderDockWindow("Disassembly", dock_right);
        ImGui::DockBuilderFinish(dockspace_id);
    }

    // Panels
    panels::render_display(display_);

    if (show_registers_)
        panels::render_registers(emu);

    if (show_sio_)
        panels::render_sio(emu, key_buf_);

    if (show_pio_)
        panels::render_pio(emu);

    if (show_dma_)
        panels::render_dma(emu);

    if (show_rtc_)
        panels::render_rtc(emu);

    if (show_xebec_)
        panels::render_xebec(emu);

    if (show_disasm_)
        panels::render_disasm(emu, paused, action);

    if (show_fdc_)
        panels::render_fdc(emu);
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
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        SDL_Window *backup_window = SDL_GL_GetCurrentWindow();
        SDL_GLContext backup_context = SDL_GL_GetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        SDL_GL_MakeCurrent(backup_window, backup_context);
    }
    SDL_GL_SwapWindow(window_);
}
