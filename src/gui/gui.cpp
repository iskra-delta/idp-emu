#include "gui.hpp"
#include "panel_display.hpp"
#include "panel_disasm.hpp"
#include "panel_regs.hpp"
#include "panel_fdc.hpp"
#include "panel_sio.hpp"
#include "panel_pio.hpp"
#include "panel_dma.hpp"
#include "panel_rtc.hpp"
#include "panel_scn2674.hpp"
#include "panel_ef9367.hpp"
#include "panel_xebec.hpp"
#include "../partner.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <cctype>
#include <cstdio>
#include <iostream>
#include <string>

namespace {
static inline void push_byte(std::vector<uint8_t>& out, uint8_t b)
{
    out.push_back(b);
}

static inline void push_cstr(std::vector<uint8_t>& out, const char* s)
{
    while (*s) {
        out.push_back((uint8_t)*s++);
    }
}

// VT100 control-key translation (Ctrl+A..Z etc.).
static bool map_vt100_ctrl_key(SDL_Keycode key, SDL_Keymod mods, std::vector<uint8_t>& out)
{
    if ((mods & KMOD_CTRL) == 0) {
        return false;
    }
    if (key >= SDLK_a && key <= SDLK_z) {
        push_byte(out, (uint8_t)(1 + (key - SDLK_a))); // Ctrl+A..Ctrl+Z => 0x01..0x1A
        return true;
    }
    switch (key) {
    case SDLK_SPACE:
    case SDLK_2:
        push_byte(out, 0x00); // NUL
        return true;
    case SDLK_LEFTBRACKET:
        push_byte(out, 0x1B); // ESC
        return true;
    case SDLK_BACKSLASH:
        push_byte(out, 0x1C); // FS
        return true;
    case SDLK_RIGHTBRACKET:
        push_byte(out, 0x1D); // GS
        return true;
    case SDLK_6:
        push_byte(out, 0x1E); // RS (Ctrl+^)
        return true;
    case SDLK_MINUS:
        push_byte(out, 0x1F); // US (Ctrl+_)
        return true;
    default:
        return false;
    }
}

// Minimal DEC VT100-style keyboard translation for non-text keys.
// Printable text still arrives via SDL_TEXTINPUT.
static bool map_vt100_key(SDL_Keycode key, std::vector<uint8_t>& out, bool& local_only)
{
    local_only = false;
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
        push_byte(out, 0x0D);
        return true;
    case SDLK_BACKSPACE:
        // VT100 typically transmits DEL for Backspace.
        push_byte(out, 0x7F);
        return true;
    case SDLK_TAB:
        push_byte(out, 0x09);
        return true;
    case SDLK_ESCAPE:
        push_byte(out, 0x1B);
        return true;
    case SDLK_UP:
        push_cstr(out, "\x1B[A");
        return true;
    case SDLK_DOWN:
        push_cstr(out, "\x1B[B");
        return true;
    case SDLK_RIGHT:
        push_cstr(out, "\x1B[C");
        return true;
    case SDLK_LEFT:
        push_cstr(out, "\x1B[D");
        return true;
    case SDLK_HOME:
        push_cstr(out, "\x1B[H");
        return true;
    case SDLK_END:
        push_cstr(out, "\x1B[F");
        return true;
    case SDLK_INSERT:
        push_cstr(out, "\x1B[2~");
        return true;
    case SDLK_DELETE:
        push_cstr(out, "\x1B[3~");
        return true;
    case SDLK_PAGEUP:
        push_cstr(out, "\x1B[5~");
        return true;
    case SDLK_PAGEDOWN:
        push_cstr(out, "\x1B[6~");
        return true;
    case SDLK_F1:
        push_cstr(out, "\x1BOP"); // PF1
        return true;
    case SDLK_F2:
        push_cstr(out, "\x1BOQ"); // PF2
        return true;
    case SDLK_F3:
        push_cstr(out, "\x1BOR"); // PF3
        return true;
    case SDLK_F4:
        push_cstr(out, "\x1BOS"); // PF4
        return true;
    case SDLK_PAUSE:
    case SDLK_F12:
        // DEC SET-UP is local on real VT100.
        local_only = true;
        return true;
    default:
        return false;
    }
}

static const char *host_label_for_special_key(SDL_Keycode key)
{
    switch (key) {
    case SDLK_ESCAPE: return "Esc";
    case SDLK_TAB: return "Tab";
    case SDLK_RETURN: return "Enter";
    case SDLK_KP_ENTER: return "KP Enter";
    case SDLK_BACKSPACE: return "Backspace";
    case SDLK_SPACE: return "Space";
    case SDLK_UP: return "Up";
    case SDLK_DOWN: return "Down";
    case SDLK_LEFT: return "Left";
    case SDLK_RIGHT: return "Right";
    case SDLK_DELETE: return "Delete";
    case SDLK_PAUSE:
    case SDLK_F12: return "Pause/F12";
    case SDLK_F1: return "F1";
    case SDLK_F2: return "F2";
    case SDLK_F3: return "F3";
    case SDLK_F4: return "F4";
    case SDLK_LCTRL:
    case SDLK_RCTRL: return "Ctrl";
    case SDLK_CAPSLOCK: return "CapsLock";
    case SDLK_LSHIFT: return "LShift";
    case SDLK_RSHIFT: return "RShift";
    case SDLK_SCROLLLOCK: return "ScrollLock";
    case SDLK_KP_DIVIDE: return "KP/";
    case SDLK_KP_MULTIPLY: return "KP*";
    case SDLK_KP_MINUS: return "KP-";
    case SDLK_KP_PERIOD: return "KP.";
    case SDLK_KP_0: return "KP0";
    case SDLK_KP_1: return "KP1";
    case SDLK_KP_2: return "KP2";
    case SDLK_KP_3: return "KP3";
    case SDLK_KP_4: return "KP4";
    case SDLK_KP_5: return "KP5";
    case SDLK_KP_6: return "KP6";
    case SDLK_KP_7: return "KP7";
    case SDLK_KP_8: return "KP8";
    case SDLK_KP_9: return "KP9";
    default: return nullptr;
    }
}
}

void gui::blink_host_key(const char *host_key)
{
    if (!host_key || !host_key[0]) {
        return;
    }
    key_blink_until_ms_[host_key] = SDL_GetTicks() + key_blink_ms_;
}

bool gui::is_host_key_blinking(const char *host_key)
{
    if (!host_key || !host_key[0]) {
        return false;
    }
    const auto it = key_blink_until_ms_.find(host_key);
    if (it == key_blink_until_ms_.end()) {
        return false;
    }
    const uint32_t now = SDL_GetTicks();
    if (now > it->second) {
        key_blink_until_ms_.erase(it);
        return false;
    }
    return true;
}

bool gui::init(const std::string &title, int width, int height)
{
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0)
    {
        std::cerr << "[error] SDL_Init failed: " << SDL_GetError() << "\n";
        return false;
    }

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
    {
        std::cerr << "[error] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_SetWindowMinimumSize(window_, 1024, 600);

    gl_context_ = SDL_GL_CreateContext(window_);
    if (!gl_context_)
    {
        std::cerr << "[error] SDL_GL_CreateContext failed: " << SDL_GetError() << "\n";
        return false;
    }

    if (SDL_GL_MakeCurrent(window_, gl_context_) != 0)
    {
        std::cerr << "[error] SDL_GL_MakeCurrent failed: " << SDL_GetError() << "\n";
        return false;
    }
    if (SDL_GL_SetSwapInterval(1) != 0)
    {
        std::cerr << "[warning] SDL_GL_SetSwapInterval failed: " << SDL_GetError() << "\n";
    }

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

    if (!ImGui_ImplSDL2_InitForOpenGL(window_, gl_context_))
    {
        std::cerr << "[error] ImGui_ImplSDL2_InitForOpenGL failed\n";
        return false;
    }
    if (!ImGui_ImplOpenGL3_Init("#version 330"))
    {
        std::cerr << "[error] ImGui_ImplOpenGL3_Init failed\n";
        return false;
    }

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
            const SDL_Keycode sym = event.key.keysym.sym;
            const SDL_Keymod mods = (SDL_Keymod)event.key.keysym.mod;

            if (sym >= SDLK_a && sym <= SDLK_z) {
                char host[2] = { (char)std::toupper((int)('a' + (sym - SDLK_a))), '\0' };
                blink_host_key(host);
            } else if (sym >= SDLK_0 && sym <= SDLK_9) {
                char host[2] = { (char)('0' + (sym - SDLK_0)), '\0' };
                blink_host_key(host);
            } else {
                switch (sym) {
                case SDLK_MINUS: blink_host_key("-"); break;
                case SDLK_EQUALS: blink_host_key("="); break;
                case SDLK_LEFTBRACKET: blink_host_key("["); break;
                case SDLK_RIGHTBRACKET: blink_host_key("]"); break;
                case SDLK_SEMICOLON: blink_host_key(";"); break;
                case SDLK_QUOTE: blink_host_key("'"); break;
                case SDLK_COMMA: blink_host_key(","); break;
                case SDLK_PERIOD: blink_host_key("."); break;
                case SDLK_SLASH: blink_host_key("/"); break;
                case SDLK_BACKSLASH: blink_host_key("\\"); break;
                case SDLK_BACKQUOTE: blink_host_key("`"); break;
                default: {
                    const char *special = host_label_for_special_key(sym);
                    if (special) {
                        blink_host_key(special);
                    }
                    break;
                }
                }
            }

            // Keep a guaranteed quit chord even when ESC is used as terminal input (VT100 mode).
            if ((mods & KMOD_CTRL) && (sym == SDLK_q))
                return false;

            // Always allow debugger hotkeys even when ImGui has keyboard navigation focus.
            switch (sym)
            {
            case SDLK_F5:
                action = dbg_action::SWITCH_TO_GDP;
                break;
            case SDLK_SPACE:
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
                // VT100 profile maps non-printable keys to DEC-compatible sequences.
                if (terminal_profile_ == terminal_profile::vt100_ansi)
                {
                    if (map_vt100_ctrl_key(sym, mods, key_buf_))
                        continue;

                    bool local_only = false;
                    if (map_vt100_key(sym, key_buf_, local_only))
                    {
                        if (local_only)
                        {
                            // Local-only VT100 key (SET-UP): no transmit to host.
                        }
                        continue;
                    }
                }

                switch (sym)
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
                if (ch >= 32 && ch < 127) {
                    key_buf_.push_back(ch);
                    if (ch >= 'a' && ch <= 'z') {
                        char host[2] = { (char)std::toupper((int)ch), '\0' };
                        blink_host_key(host);
                    } else if (ch >= 'A' && ch <= 'Z') {
                        char host[2] = { (char)ch, '\0' };
                        blink_host_key(host);
                    } else if (ch >= '0' && ch <= '9') {
                        char host[2] = { (char)ch, '\0' };
                        blink_host_key(host);
                    } else {
                        switch (ch) {
                        case ' ': blink_host_key("Space"); break;
                        case '-': blink_host_key("-"); break;
                        case '=': blink_host_key("="); break;
                        case '[': blink_host_key("["); break;
                        case ']': blink_host_key("]"); break;
                        case ';': blink_host_key(";"); break;
                        case '\'': blink_host_key("'"); break;
                        case ',': blink_host_key(","); break;
                        case '.': blink_host_key("."); break;
                        case '/': blink_host_key("/"); break;
                        case '\\': blink_host_key("\\"); break;
                        case '`': blink_host_key("`"); break;
                        default: break;
                        }
                    }
                }
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
            {
                emu.reset();
                paused = true;
                display_.clear_all();
                display_.update();
            }
            ImGui::Separator();
            const char *quit_shortcut = (terminal_profile_ == terminal_profile::vt100_ansi) ? "Ctrl+Q" : "Esc";
            if (ImGui::MenuItem("Quit", quit_shortcut))
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
            ImGui::MenuItem("SCN2674 AVDC", nullptr, &show_scn2674_);
            ImGui::MenuItem("EF9367 GDP", nullptr, &show_ef9367_);
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Tools"))
        {
            ImGui::MenuItem("Virtual Keyboard", nullptr, &show_keyboard_);
            if (ImGui::BeginMenu("Monitor Type"))
            {
                const auto mode = display_.get_phosphor_type();
                if (ImGui::MenuItem("Green CRT", nullptr, mode == display::phosphor_type::green))
                    display_.set_phosphor_type(display::phosphor_type::green);
                if (ImGui::MenuItem("Orange CRT", nullptr, mode == display::phosphor_type::orange))
                    display_.set_phosphor_type(display::phosphor_type::orange);
                if (ImGui::MenuItem("LCD (Game Boy)", nullptr, mode == display::phosphor_type::lcd))
                    display_.set_phosphor_type(display::phosphor_type::lcd);
                ImGui::EndMenu();
            }
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
        ImGuiID dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.25f, nullptr, &dock_main);

        ImGui::DockBuilderDockWindow("Partner Display", dock_main);
        ImGui::DockBuilderDockWindow("Disassembly", dock_right);
        ImGui::DockBuilderDockWindow("SCN2674 AVDC", dock_right);
        ImGui::DockBuilderDockWindow("EF9367 GDP", dock_right);
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

    if (show_scn2674_)
        panels::render_scn2674(emu);

    if (show_ef9367_)
        panels::render_ef9367(emu);

    if (show_xebec_)
        panels::render_xebec(emu);

    if (show_disasm_)
        panels::render_disasm(emu, paused, action);

    if (show_fdc_)
        panels::render_fdc(emu);

    if (show_keyboard_)
    {
        // Keep this panel floating by default (not initially docked) because it benefits from horizontal space.
        ImGui::SetNextWindowDockID(0, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(ImVec2(1560.0f, 530.0f), ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(ImVec2(140.0f, 120.0f), ImGuiCond_Appearing);
        ImGui::Begin("Virtual Keyboard", &show_keyboard_);
        ImGui::TextUnformatted("DEC VT100-style mapping (click to inject):");
        ImGui::Separator();
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.96f, 0.96f, 0.96f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.82f, 0.82f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.2f);

        const float key_scale = 1.25f;
        const float key_h = 42.0f * key_scale;
        static char last_click_info[128] = "";
        auto clicked_info = [&](const char* dec_key, const char* host_key, const char* tx_desc) {
            std::snprintf(last_click_info, sizeof(last_click_info), "%s <- %s (%s)", dec_key, host_key, tx_desc);
        };
        auto push_blink_style = [&](const char* host_key) {
            if (!is_host_key_blinking(host_key)) {
                return false;
            }
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.99f, 0.90f, 0.52f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.00f, 0.94f, 0.62f, 1.00f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.94f, 0.84f, 0.42f, 1.00f));
            return true;
        };
        auto key_button = [&](const char* dec_key, const char* host_key, const char* tx_bytes, float w, float h = -1.0f) {
            if (h <= 0.0f) h = key_h;
            const bool blinked = push_blink_style(host_key);
            std::string label = std::string(dec_key) + "\n[" + host_key + "]";
            if (ImGui::Button(label.c_str(), ImVec2(w, h))) {
                blink_host_key(host_key);
                if (tx_bytes && tx_bytes[0]) {
                    push_cstr(key_buf_, tx_bytes);
                    clicked_info(dec_key, host_key, tx_bytes);
                } else {
                    clicked_info(dec_key, host_key, "local-only");
                }
            }
            if (blinked) {
                ImGui::PopStyleColor(3);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::BeginTooltip();
                ImGui::Text("DEC key: %s", dec_key);
                ImGui::Text("Host key: %s", host_key);
                ImGui::Text("Transmit: %s", (tx_bytes && tx_bytes[0]) ? tx_bytes : "(local only)");
                ImGui::EndTooltip();
            }
        };
        auto char_button = [&](const char* dec_key, const char* host_key, uint8_t ch, float w = 46.0f, float h = -1.0f) {
            if (h <= 0.0f) h = key_h;
            const bool blinked = push_blink_style(host_key);
            std::string label = std::string(dec_key) + "\n[" + host_key + "]";
            if (ImGui::Button(label.c_str(), ImVec2(w, h))) {
                blink_host_key(host_key);
                push_byte(key_buf_, ch);
                char tx[16];
                std::snprintf(tx, sizeof(tx), "0x%02X", (unsigned)ch);
                clicked_info(dec_key, host_key, tx);
            }
            if (blinked) {
                ImGui::PopStyleColor(3);
            }
        };
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
        const float sx = 4.0f * key_scale;
        const float k = 42.0f * key_scale;
        const float h = key_h;
        const float mod = 60.0f * key_scale;
        const float shift = 70.0f * key_scale;
        const float wide = 74.0f * key_scale;
        const float space_w = 468.0f * key_scale;
        const float ret_w = 88.0f * key_scale;
        const float kp_zero_w = 90.0f * key_scale;
        const float inter_group_gap_1 = 14.0f * key_scale;
        const float inter_group_gap_2 = 18.0f * key_scale;

        // Center the full keyboard block in the available content width.
        const auto row_w = [&](std::initializer_list<float> widths) -> float {
            float sum = 0.0f;
            int count = 0;
            for (float w : widths) {
                sum += w;
                count++;
            }
            return sum + (count > 1 ? (float)(count - 1) * sx : 0.0f);
        };
        const float left_group_w = std::max(
            std::max(
                std::max(
                    std::max(
                        row_w({wide}),
                        row_w({mod, k, k, k, k, k, k, k, k, k, k, k, k})),
                    row_w({mod, k, k, k, k, k, k, k, k, k, k, k, k})),
                row_w({mod, mod, k, k, k, k, k, k, k, k, k, k, k})),
            std::max(
                row_w({mod, shift, k, k, k, k, k, k, k, k, k, k, shift}),
                row_w({space_w})));
        const float nav_group_w = std::max(
            std::max(row_w({k, k, k, k}), row_w({k, k, k, wide, mod})),
            std::max(row_w({wide, k}), row_w({ret_w, mod})));
        const float keypad_group_w = std::max(
            std::max(row_w({k, k, k, k}), row_w({k, k, k, k})),
            std::max(row_w({k, k, k, k}), row_w({kp_zero_w, k, k})));
        const float keyboard_total_w = left_group_w + inter_group_gap_1 + nav_group_w + inter_group_gap_2 + keypad_group_w;
        const float avail_w = ImGui::GetContentRegionAvail().x;
        if (avail_w > keyboard_total_w) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - keyboard_total_w) * 0.5f);
        }

        // Left: main typing block
        ImGui::BeginGroup();
        key_button("SET-UP", "Pause/F12", "", wide, h);

        char_button("ESC", "Esc", 0x1B, mod); ImGui::SameLine();
        char_button("1", "1", '1', k); ImGui::SameLine();
        char_button("2", "2", '2', k); ImGui::SameLine();
        char_button("3", "3", '3', k); ImGui::SameLine();
        char_button("4", "4", '4', k); ImGui::SameLine();
        char_button("5", "5", '5', k); ImGui::SameLine();
        char_button("6", "6", '6', k); ImGui::SameLine();
        char_button("7", "7", '7', k); ImGui::SameLine();
        char_button("8", "8", '8', k); ImGui::SameLine();
        char_button("9", "9", '9', k); ImGui::SameLine();
        char_button("0", "0", '0', k); ImGui::SameLine();
        char_button("-", "-", '-', k); ImGui::SameLine();
        char_button("=", "=", '=', k);

        key_button("TAB", "Tab", "\x09", mod, h); ImGui::SameLine();
        char_button("Q", "Q", 'q', k); ImGui::SameLine();
        char_button("W", "W", 'w', k); ImGui::SameLine();
        char_button("E", "E", 'e', k); ImGui::SameLine();
        char_button("R", "R", 'r', k); ImGui::SameLine();
        char_button("T", "T", 't', k); ImGui::SameLine();
        char_button("Y", "Y", 'y', k); ImGui::SameLine();
        char_button("U", "U", 'u', k); ImGui::SameLine();
        char_button("I", "I", 'i', k); ImGui::SameLine();
        char_button("O", "O", 'o', k); ImGui::SameLine();
        char_button("P", "P", 'p', k); ImGui::SameLine();
        char_button("{", "[", '[', k); ImGui::SameLine();
        char_button("}", "]", ']', k);

        key_button("CTRL", "Ctrl", "", mod, h); ImGui::SameLine();
        key_button("CAPS", "CapsLock", "", mod, h); ImGui::SameLine();
        char_button("A", "A", 'a', k); ImGui::SameLine();
        char_button("S", "S", 's', k); ImGui::SameLine();
        char_button("D", "D", 'd', k); ImGui::SameLine();
        char_button("F", "F", 'f', k); ImGui::SameLine();
        char_button("G", "G", 'g', k); ImGui::SameLine();
        char_button("H", "H", 'h', k); ImGui::SameLine();
        char_button("J", "J", 'j', k); ImGui::SameLine();
        char_button("K", "K", 'k', k); ImGui::SameLine();
        char_button("L", "L", 'l', k); ImGui::SameLine();
        char_button(":", ";", ';', k); ImGui::SameLine();
        char_button("\"", "'", '\'', k);

        key_button("NOSCRL", "ScrollLock", "", mod, h); ImGui::SameLine();
        key_button("SHIFT", "LShift", "", shift, h); ImGui::SameLine();
        char_button("Z", "Z", 'z', k); ImGui::SameLine();
        char_button("X", "X", 'x', k); ImGui::SameLine();
        char_button("C", "C", 'c', k); ImGui::SameLine();
        char_button("V", "V", 'v', k); ImGui::SameLine();
        char_button("B", "B", 'b', k); ImGui::SameLine();
        char_button("N", "N", 'n', k); ImGui::SameLine();
        char_button("M", "M", 'm', k); ImGui::SameLine();
        char_button("<", ",", ',', k); ImGui::SameLine();
        char_button(">", ".", '.', k); ImGui::SameLine();
        char_button("?", "/", '/', k); ImGui::SameLine();
        key_button("SHIFT", "RShift", "", shift, h);

        key_button("SPACE", "Space", " ", space_w, h);
        ImGui::EndGroup();

        // Center-right: cursor/nav/edit cluster
        ImGui::SameLine(0.0f, inter_group_gap_1);
        ImGui::BeginGroup();
        key_button("UP", "Up", "\x1B[A", k, h); ImGui::SameLine();
        key_button("DOWN", "Down", "\x1B[B", k, h); ImGui::SameLine();
        key_button("LEFT", "Left", "\x1B[D", k, h); ImGui::SameLine();
        key_button("RIGHT", "Right", "\x1B[C", k, h);

        char_button("-", "-", '-', k); ImGui::SameLine();
        char_button("+", "=", '+', k); ImGui::SameLine();
        char_button("`", "`", '`', k); ImGui::SameLine();
        key_button("BACKSP", "Backspace", "\x7F", wide, h); ImGui::SameLine();
        key_button("BREAK", "Break", "", mod, h);

        key_button("DELETE", "Delete", "\x1B[3~", wide, h); ImGui::SameLine();
        char_button("|", "\\", '\\', k);

        key_button("RETURN", "Enter", "\r", ret_w, h); ImGui::SameLine();
        key_button("LINEFD", "Ctrl+J", "\n", mod, h);
        ImGui::EndGroup();

        // Right: numeric / PF keypad block
        ImGui::SameLine(0.0f, inter_group_gap_2);
        ImGui::BeginGroup();
        key_button("PF1", "F1", "\x1BOP", k, h); ImGui::SameLine();
        key_button("PF2", "F2", "\x1BOQ", k, h); ImGui::SameLine();
        key_button("PF3", "F3", "\x1BOR", k, h); ImGui::SameLine();
        key_button("PF4", "F4", "\x1BOS", k, h);

        char_button("7", "KP7", '7', k); ImGui::SameLine();
        char_button("8", "KP8", '8', k); ImGui::SameLine();
        char_button("9", "KP9", '9', k); ImGui::SameLine();
        char_button("/", "KP/", '/', k);

        char_button("4", "KP4", '4', k); ImGui::SameLine();
        char_button("5", "KP5", '5', k); ImGui::SameLine();
        char_button("6", "KP6", '6', k); ImGui::SameLine();
        char_button("*", "KP*", '*', k);

        char_button("1", "KP1", '1', k); ImGui::SameLine();
        char_button("2", "KP2", '2', k); ImGui::SameLine();
        char_button("3", "KP3", '3', k); ImGui::SameLine();
        char_button("-", "KP-", '-', k);

        char_button("0", "KP0", '0', kp_zero_w); ImGui::SameLine();
        char_button(".", "KP.", '.', k); ImGui::SameLine();
        key_button("ENTER", "KP Enter", "\r", k, h);
        ImGui::EndGroup();

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);

        ImGui::Separator();
        ImGui::Text("Last: %s", last_click_info[0] ? last_click_info : "(none)");
        ImGui::TextUnformatted("SET-UP host key: Pause or F12.");
        ImGui::TextUnformatted("Quit: Ctrl+Q (VT100) or Esc (VT52).");
        ImGui::TextUnformatted("Debugger keys reserved: F5, F10, F11.");
        ImGui::End();
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
