#include "gui.hpp"
#include "panel_display.hpp"
#include "panel_disasm.hpp"
#include "panel_regs.hpp"
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

bool gui::process_events(bool &paused)
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
            case SDLK_SPACE:
                paused = !paused;
                break;
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

void gui::render_panels(partner &emu, bool &paused)
{
    display_.update();

    int win_w, win_h;
    SDL_GetWindowSize(window_, &win_w, &win_h);

    float menu_h = ImGui::GetFrameHeight();
    float panel_w = 350.0f;
    float main_w = (float)win_w - panel_w;
    float main_h = (float)win_h - menu_h;
    float reg_h = 280.0f;

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
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }

    // Display panel (always visible, fills main area)
    ImGui::SetNextWindowPos({0, menu_h});
    ImGui::SetNextWindowSize({main_w, main_h});
    panels::render_display(display_);

    // Registers panel
    if (show_registers_)
    {
        ImGui::SetNextWindowPos({main_w, menu_h});
        ImGui::SetNextWindowSize({panel_w, reg_h});
        panels::render_registers(emu);
    }

    // Disassembly panel
    if (show_disasm_)
    {
        float disasm_y = show_registers_ ? menu_h + reg_h : menu_h;
        float disasm_h = show_registers_ ? main_h - reg_h : main_h;
        ImGui::SetNextWindowPos({main_w, disasm_y});
        ImGui::SetNextWindowSize({panel_w, disasm_h});
        panels::render_disasm(emu);
    }
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
