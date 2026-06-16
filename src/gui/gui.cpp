#include "gui.hpp"
#include "panel_display.hpp"
#include "panel_disasm.hpp"
#include "panel_regs.hpp"
#include "panel_fdc.hpp"
#include "panel_sio.hpp"
#include "panel_pio.hpp"
#include "panel_dma.hpp"
#include "panel_monitor.hpp"
#include "panel_rtc.hpp"
#include "panel_scn2674.hpp"
#include "panel_ef9367.hpp"
#include "panel_xebec.hpp"
#include "panel_devices.hpp"
#include "chrome_metrics.hpp"
#include "chrome_style.hpp"
#include "chrome_theme.hpp"
#include "custom_title_bar.hpp"
#include "terminal_keymap.hpp"
#include "window_chrome.hpp"
#include "../partner.hpp"
#include "../partner_gdp.hpp"
#include "../dap/dap_debugger.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_opengl3.h>
#include <SDL.h>
#include <SDL_opengl.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
static inline void push_cstr(std::vector<uint8_t>& out, const char* s)
{
    while (*s) {
        out.push_back((uint8_t)*s++);
    }
}

static std::string resolve_asset_path(const std::string& relative)
{
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates;
    std::error_code ec;

    candidates.push_back(fs::current_path(ec) / relative);

    char* base = SDL_GetBasePath();
    if (base)
    {
        fs::path exe_dir(base);
        SDL_free(base);
        exe_dir = exe_dir.lexically_normal();
        candidates.push_back(exe_dir / relative);
        if (exe_dir.filename() == "bin")
            candidates.push_back(exe_dir.parent_path() / relative);
    }

    for (const fs::path& p : candidates)
    {
        ec.clear();
        if (fs::exists(p, ec) && !ec)
            return p.lexically_normal().string();
    }
    return relative;
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

void gui::close_all_views()
{
    show_registers_ = false;
    show_disasm_ = false;
    show_fdc_ = false;
    show_sio_ = false;
    show_pio_ = false;
    show_devices_ = false;
    show_monitor_ = false;
    show_dma_ = false;
    show_xebec_ = false;
    show_rtc_ = false;
    show_scn2674_ = false;
    show_ef9367_ = false;
    show_keyboard_ = false;
}

void gui::queue_keyboard_tone(float freq_hz, float duration_ms, float amplitude, bool square_wave)
{
    if ((audio_device_ == 0) || (audio_spec_.freq <= 0) || (duration_ms <= 0.0f)) {
        return;
    }

    const int sample_rate = audio_spec_.freq;
    const int sample_count = std::max(1, (int)std::lround((duration_ms / 1000.0f) * (float)sample_rate));
    std::vector<float> samples;
    samples.resize((std::size_t)sample_count);

    const double two_pi = 6.28318530717958647692;
    const double step = two_pi * (double)freq_hz / (double)sample_rate;
    double phase = 0.0;

    for (int i = 0; i < sample_count; i++)
    {
        float v = std::sin((float)phase);
        if (square_wave) {
            v = (v >= 0.0f) ? 1.0f : -1.0f;
        }
        const float t = (float)i / (float)sample_count;
        float env = 1.0f;
        if (t < 0.04f) {
            env = t / 0.04f;
        } else if (t > 0.90f) {
            env = std::max(0.0f, (1.0f - t) / 0.10f);
        }
        samples[(std::size_t)i] = v * amplitude * env;
        phase += step;
    }

    const std::uint32_t queued = SDL_GetQueuedAudioSize(audio_device_);
    const std::uint32_t max_queued = (std::uint32_t)(audio_spec_.freq * sizeof(float) / 2);
    if (queued > max_queued && duration_ms <= 30.0f) {
        return;
    }
    SDL_QueueAudio(audio_device_, samples.data(), (std::uint32_t)(samples.size() * sizeof(float)));
}

void gui::queue_keyboard_sound(partner_gdp_keyboard_sound sound)
{
    if (audio_device_ != 0) {
        const std::uint32_t queued_bytes = SDL_GetQueuedAudioSize(audio_device_);
        const std::uint32_t bytes_per_ms =
            (audio_spec_.freq > 0)
                ? (std::uint32_t)((audio_spec_.freq * (int)sizeof(float)) / 1000)
                : 0u;
        const std::uint32_t queued_ms = (bytes_per_ms > 0) ? (queued_bytes / bytes_per_ms) : 0u;
        if ((sound == partner_gdp_keyboard_sound::short_beep || sound == partner_gdp_keyboard_sound::long_beep) &&
            queued_ms > 80u) {
            // Prefer current firmware beeps over stale queued tones.
            SDL_ClearQueuedAudio(audio_device_);
        }
    }

    switch (sound)
    {
    case partner_gdp_keyboard_sound::key_click:
        queue_keyboard_tone(2200.0f, 14.0f, 0.10f, true);
        break;
    case partner_gdp_keyboard_sound::short_beep:
        queue_keyboard_tone(980.0f, 70.0f, 0.15f, false);
        break;
    case partner_gdp_keyboard_sound::long_beep:
        queue_keyboard_tone(740.0f, 180.0f, 0.18f, false);
        break;
    case partner_gdp_keyboard_sound::none:
    default:
        break;
    }
}

void gui::service_keyboard_sound(partner &emu)
{
    auto *gdp = dynamic_cast<partner_gdp *>(&emu);
    if (!gdp) {
        return;
    }
    auto &kbd = gdp->get_keyboard();
    partner_gdp_keyboard_sound sound = partner_gdp_keyboard_sound::none;
    while (kbd.pop_sound(sound)) {
        queue_keyboard_sound(sound);
    }
}

void gui::open_disk_mount_dialog(partner &emu, int drive)
{
    disk_mount_drive_ = drive;
    disk_mount_path_ = emu.get_disk_path(drive);
    if (disk_mount_path_.empty()) {
        disk_mount_path_ = (drive == 0) ? "disks/fdd-partner-p.img" : "disks/fdd-partner-g.img";
    }
    disk_mount_error_.clear();
    open_disk_mount_popup_ = true;
}

void gui::open_remote_debugger_dialog()
{
    if (!remote_debugger_)
        return;
    remote_debug_host_ = remote_debugger_->bind_host();
    remote_debug_port_text_ = std::to_string(remote_debugger_->bind_port());
    remote_debug_error_.clear();
    open_remote_debugger_popup_ = true;
}

std::optional<gui::remote_debugger_request> gui::take_remote_debugger_request()
{
    auto request = std::move(pending_remote_debugger_request_);
    pending_remote_debugger_request_.reset();
    return request;
}

void gui::render_disk_mount_dialog(partner &emu)
{
    if (open_disk_mount_popup_) {
        ImGui::OpenPopup("Mount Floppy");
        open_disk_mount_popup_ = false;
    }

    bool open = true;
    if (!ImGui::BeginPopupModal("Mount Floppy", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    ImGui::Text("Floppy FD%d:", disk_mount_drive_);
    const std::string current_path = emu.get_disk_path(disk_mount_drive_);
    ImGui::TextDisabled("Current: %s", current_path.empty() ? "(empty)" : current_path.c_str());

    char path_buf[1024];
    std::snprintf(path_buf, sizeof(path_buf), "%s", disk_mount_path_.c_str());
    if (ImGui::InputText("Image path", path_buf, sizeof(path_buf))) {
        disk_mount_path_ = path_buf;
    }

    if (!disk_mount_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 90, 90, 255));
        ImGui::TextWrapped("%s", disk_mount_error_.c_str());
        ImGui::PopStyleColor();
    }

    if (ImGui::Button("Mount")) {
        try {
            emu.load_disk(disk_mount_drive_, disk_mount_path_);
            disk_mount_error_.clear();
            ImGui::CloseCurrentPopup();
        } catch (const std::exception &e) {
            disk_mount_error_ = e.what();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        disk_mount_error_.clear();
        ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
}

void gui::render_remote_debugger_dialog()
{
    if (!remote_debugger_)
        return;

    if (open_remote_debugger_popup_) {
        ImGui::OpenPopup("Remote Debugger");
        open_remote_debugger_popup_ = false;
    }

    bool open = true;
    if (!ImGui::BeginPopupModal("Remote Debugger", &open, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    char host_buf[128];
    std::snprintf(host_buf, sizeof(host_buf), "%s", remote_debug_host_.c_str());
    if (ImGui::InputText("Bind host", host_buf, sizeof(host_buf)))
        remote_debug_host_ = host_buf;

    char port_buf[32];
    std::snprintf(port_buf, sizeof(port_buf), "%s", remote_debug_port_text_.c_str());
    if (ImGui::InputText("Port", port_buf, sizeof(port_buf)))
        remote_debug_port_text_ = port_buf;

    ImGui::TextDisabled("Status: %s", remote_debugger_->status_summary().c_str());
    ImGui::TextWrapped("udap client: %s", remote_debugger_->debugger_command().c_str());

    if (!remote_debug_error_.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 90, 90, 255));
        ImGui::TextWrapped("%s", remote_debug_error_.c_str());
        ImGui::PopStyleColor();
    }

    const bool enabled = remote_debugger_->is_enabled();
    if (ImGui::Button(enabled ? "Stop" : "Start")) {
        remote_debug_error_.clear();
        if (enabled) {
            pending_remote_debugger_request_ = remote_debugger_request{
                remote_debugger_request::kind::stop,
                remote_debug_host_,
                0
            };
        } else {
            char *end = nullptr;
            const unsigned long parsed_port = std::strtoul(remote_debug_port_text_.c_str(), &end, 10);
            if (end == remote_debug_port_text_.c_str() || *end != '\0' || parsed_port == 0 || parsed_port > 65535UL) {
                remote_debug_error_ = "Enter a valid TCP port number.";
            } else {
                pending_remote_debugger_request_ = remote_debugger_request{
                    remote_debugger_request::kind::start,
                    remote_debug_host_,
                    static_cast<uint16_t>(parsed_port)
                };
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Close"))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

bool gui::init(const std::string &title, int width, int height)
{
    // Let debuggers and the OS deliver SIGINT/SIGTERM normally instead of
    // having SDL translate them into SDL_QUIT events.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");

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
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_BORDERLESS);
    if (!window_)
    {
        std::cerr << "[error] SDL_CreateWindow failed: " << SDL_GetError() << "\n";
        return false;
    }

    SDL_SetWindowMinimumSize(window_, 1024, 600);
    SDL_ShowWindow(window_);
    SDL_RaiseWindow(window_);

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
    const bool enable_vsync = [] {
        const char *s = std::getenv("IDP_EMU_VSYNC");
        return s && s[0] && s[0] != '0';
    }();
    if (SDL_GL_SetSwapInterval(enable_vsync ? 1 : 0) != 0)
    {
        std::cerr << "[warning] SDL_GL_SetSwapInterval failed: " << SDL_GetError() << "\n";
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO &io = ImGui::GetIO();
    // Keep Dear ImGui settings in a dedicated app-specific config file.
    io.IniFilename = "idp-emu.config";
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    const bool enable_viewports = [] {
        const char *s = std::getenv("IDP_EMU_VIEWPORTS");
        return s && s[0] && s[0] != '0';
    }();
    if (enable_viewports)
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

    ImFont* ui_font = nullptr;
    const std::string inter_path = resolve_asset_path("assets/fonts/Inter.ttf");
    ui_font = io.Fonts->AddFontFromFileTTF(inter_path.c_str(), 16.0f);
    if (!ui_font)
    {
        std::cerr << "[warning] Could not load Inter font from '" << inter_path
                  << "', falling back to ImGui default\n";
        ui_font = io.Fonts->AddFontDefault();
    }
    io.FontDefault = ui_font;

    ImGui::StyleColorsDark();
    chrome_style::apply();
    apply_chrome_theme();

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

    SDL_SetWindowHitTest(
        window_,
        [](SDL_Window* window, const SDL_Point* point, void*) -> SDL_HitTestResult {
            int width_px = 0;
            int height_px = 0;
            SDL_GetWindowSize(window, &width_px, &height_px);

            if (custom_title_close_hit(window, point->x, point->y))
                return SDL_HITTEST_NORMAL;

            const int border = chrome_metrics::resize_border;
            const int corner = chrome_metrics::resize_corner;

            if (point->x >= width_px - corner && point->y >= height_px - corner)
                return SDL_HITTEST_RESIZE_BOTTOMRIGHT;
            if (point->x < corner && point->y >= height_px - corner)
                return SDL_HITTEST_RESIZE_BOTTOMLEFT;
            if (point->x >= width_px - corner && point->y < corner)
                return SDL_HITTEST_RESIZE_TOPRIGHT;
            if (point->x < corner && point->y < corner)
                return SDL_HITTEST_RESIZE_TOPLEFT;

            if (point->x >= width_px - border)
                return SDL_HITTEST_RESIZE_RIGHT;
            if (point->x < border)
                return SDL_HITTEST_RESIZE_LEFT;
            if (point->y >= height_px - border)
                return SDL_HITTEST_RESIZE_BOTTOM;
            if (point->y < border)
                return SDL_HITTEST_RESIZE_TOP;

            if (point->y < static_cast<int>(chrome_metrics::title_bar_height) &&
                point->x < width_px - static_cast<int>(chrome_metrics::close_btn_width)) {
                if (custom_title_menu_hit(point->x, point->y) >= 0) {
                    return SDL_HITTEST_NORMAL;
                }
                return SDL_HITTEST_DRAGGABLE;
            }

            return SDL_HITTEST_NORMAL;
        },
        nullptr);

    // Enable SDL text input events for terminal key injection.
    SDL_StartTextInput();

    if (SDL_InitSubSystem(SDL_INIT_AUDIO) == 0)
    {
        SDL_AudioSpec want{};
        want.freq = 44100;
        want.format = AUDIO_F32SYS;
        want.channels = 1;
        want.samples = 1024;
        want.callback = nullptr;
        audio_device_ = SDL_OpenAudioDevice(nullptr, 0, &want, &audio_spec_, 0);
        if (audio_device_ != 0)
        {
            SDL_PauseAudioDevice(audio_device_, 0);
        }
        else
        {
            std::cerr << "[warning] SDL_OpenAudioDevice failed: " << SDL_GetError() << "\n";
        }
    }
    else
    {
        std::cerr << "[warning] SDL_InitSubSystem(AUDIO) failed: " << SDL_GetError() << "\n";
    }

    display_.init();

    return true;
}

void gui::shutdown()
{
    if (audio_device_ != 0)
    {
        SDL_CloseAudioDevice(audio_device_);
        audio_device_ = 0;
    }
    display_.shutdown();
    SDL_StopTextInput();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (gl_context_)
        SDL_GL_DeleteContext(gl_context_);
    if (window_)
        SDL_DestroyWindow(window_);
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    SDL_Quit();
}

bool gui::process_events(partner &emu, bool &paused, dbg_action &action)
{
    // Drain keyboard sound events whenever we pump the event loop. This keeps
    // audio close to emulation timing rather than waiting for render only.
    service_keyboard_sound(emu);

    const Uint32 main_window_id = window_ ? SDL_GetWindowID(window_) : 0;
    const bool gdp_keyboard_model = dynamic_cast<partner_gdp *>(&emu) != nullptr;
    const bool has_serial_mouse = emu.has_serial_mouse_attached();
    const auto point_in_display = [&](int window_x, int window_y) -> bool {
        int win_x = 0;
        int win_y = 0;
        if (window_)
            SDL_GetWindowPosition(window_, &win_x, &win_y);
        const int screen_x = win_x + window_x;
        const int screen_y = win_y + window_y;
        return (screen_x >= (int)std::floor(display_viewport_.x0)) &&
               (screen_x <  (int)std::ceil(display_viewport_.x1)) &&
               (screen_y >= (int)std::floor(display_viewport_.y0)) &&
               (screen_y <  (int)std::ceil(display_viewport_.y1));
    };
    const auto release_mouse_buttons = [&]() {
        if (mouse_left_down_ || mouse_right_down_ || mouse_middle_down_)
            emu.inject_serial_mouse_motion(0, 0, false, false, false);
        mouse_left_down_ = false;
        mouse_middle_down_ = false;
        mouse_right_down_ = false;
    };
    const auto deactivate_mouse_relative = [&]() {
        if (mouse_relative_active_)
        {
            SDL_SetRelativeMouseMode(SDL_FALSE);
            SDL_CaptureMouse(SDL_FALSE);
            mouse_relative_active_ = false;
        }
    };
    const auto activate_mouse_relative = [&]() {
        if (!mouse_relative_active_)
        {
            if (SDL_SetRelativeMouseMode(SDL_TRUE) == 0)
            {
                SDL_CaptureMouse(SDL_TRUE);
                mouse_relative_active_ = true;
            }
        }
    };
    const auto release_mouse_mode = [&]() {
        release_mouse_buttons();
        deactivate_mouse_relative();
        if (mouse_cursor_hidden_)
        {
            SDL_ShowCursor(SDL_ENABLE);
            mouse_cursor_hidden_ = false;
        }
    };
    if (!has_serial_mouse && (mouse_left_down_ || mouse_right_down_ || mouse_middle_down_))
        release_mouse_buttons();
    if (!has_serial_mouse && (mouse_cursor_hidden_ || mouse_relative_active_))
        release_mouse_mode();

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        ImGui_ImplSDL2_ProcessEvent(&event);

        if (event.type == SDL_QUIT)
        {
            close_all_views();
            release_mouse_mode();
            return false;
        }

        if (event.type == SDL_WINDOWEVENT &&
            event.window.event == SDL_WINDOWEVENT_CLOSE &&
            event.window.windowID == main_window_id)
        {
            close_all_views();
            release_mouse_mode();
            SDL_Event quit_event;
            quit_event.type = SDL_QUIT;
            SDL_PushEvent(&quit_event);
            return false;
        }

        if (event.type == SDL_WINDOWEVENT &&
            event.window.windowID == main_window_id &&
            (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
             event.window.event == SDL_WINDOWEVENT_LEAVE))
        {
            release_mouse_mode();
        }

        if (event.type == SDL_MOUSEBUTTONDOWN &&
            event.button.windowID == main_window_id &&
            event.button.button == SDL_BUTTON_LEFT)
        {
            if (custom_title_close_hit(window_, event.button.x, event.button.y))
            {
                close_all_views();
                release_mouse_mode();
                SDL_Event quit_event;
                quit_event.type = SDL_QUIT;
                SDL_PushEvent(&quit_event);
                return false;
            }

            const int menu_index = custom_title_menu_hit(event.button.x, event.button.y);
            if (menu_index >= 0)
            {
                open_menu_ = menu_index;
                continue;
            }
        }

        if (event.type == SDL_MOUSEMOTION && has_serial_mouse)
        {
            if (mouse_relative_active_)
            {
                const int dx = event.motion.xrel;
                const int dy = event.motion.yrel;
                if ((dx != 0) || (dy != 0))
                    emu.inject_serial_mouse_motion(dx, dy, mouse_left_down_, mouse_right_down_, mouse_middle_down_);
                continue;
            }

            const bool inside = point_in_display(event.motion.x, event.motion.y);
            if (!inside)
            {
                if (mouse_cursor_hidden_)
                    SDL_ShowCursor(SDL_ENABLE);
                mouse_cursor_hidden_ = false;
                continue;
            }

            if (inside && !mouse_cursor_hidden_)
            {
                SDL_ShowCursor(SDL_DISABLE);
                mouse_cursor_hidden_ = true;
            }

            const int dx = event.motion.xrel;
            const int dy = event.motion.yrel;
            if ((dx != 0) || (dy != 0))
            {
                emu.inject_serial_mouse_motion(dx, dy, mouse_left_down_, mouse_right_down_, mouse_middle_down_);
            }
            continue;
        }

        if ((event.type == SDL_MOUSEBUTTONDOWN || event.type == SDL_MOUSEBUTTONUP) &&
            has_serial_mouse)
        {
            const bool down = (event.type == SDL_MOUSEBUTTONDOWN);
            if (event.button.windowID != main_window_id)
            {
                continue;
            }

            const bool inside = point_in_display(event.button.x, event.button.y);
            if (!inside && down)
                continue;

            if (inside && down)
                activate_mouse_relative();

            if (inside && !mouse_cursor_hidden_)
            {
                SDL_ShowCursor(SDL_DISABLE);
                mouse_cursor_hidden_ = true;
            }

            switch (event.button.button)
            {
            case SDL_BUTTON_LEFT:
                mouse_left_down_ = down;
                break;
            case SDL_BUTTON_MIDDLE:
                mouse_middle_down_ = down;
                break;
            case SDL_BUTTON_RIGHT:
                mouse_right_down_ = down;
                break;
            default:
                break;
            }
            emu.inject_serial_mouse_motion(0, 0, mouse_left_down_, mouse_right_down_, mouse_middle_down_);
            continue;
        }

        if (event.type == SDL_KEYDOWN)
        {
            const SDL_Keycode sym = event.key.keysym.sym;
            const SDL_Keymod mods = (SDL_Keymod)event.key.keysym.mod;

            if (mouse_relative_active_ && (sym == SDLK_LCTRL || sym == SDLK_RCTRL))
            {
                release_mouse_mode();
                continue;
            }

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
                if (map_terminal_ctrl_key(sym, mods, key_buf_))
                    continue;

                if (map_terminal_key(sym, key_buf_, gdp_keyboard_model))
                    continue;

                switch (sym)
                {
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
    if (has_serial_mouse && !mouse_relative_active_ && mouse_cursor_hidden_ && !display_viewport_.hovered)
    {
        SDL_ShowCursor(SDL_ENABLE);
        mouse_cursor_hidden_ = false;
        if (mouse_left_down_ || mouse_right_down_ || mouse_middle_down_)
            release_mouse_buttons();
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
    const bool gdp_keyboard_model = dynamic_cast<partner_gdp *>(&emu) != nullptr;

    draw_custom_title_bar(window_);
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const float title_bar_h = chrome_metrics::title_bar_height;

    ImGui::SetNextWindowPos({vp->WorkPos.x, vp->WorkPos.y + title_bar_h});
    ImGui::SetNextWindowSize({vp->WorkSize.x, vp->WorkSize.y - title_bar_h});
    ImGui::SetNextWindowViewport(vp->ID);

    const ImGuiWindowFlags host_flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##host", nullptr, host_flags);
    ImGui::PopStyleVar(3);

    static const char* popup_ids[] = {"##m_emulation", "##m_view", "##m_devices"};
    if (open_menu_ >= 0) {
        ImGui::OpenPopup(popup_ids[open_menu_]);
        open_menu_ = -1;
    }

    if (ImGui::IsPopupOpen("##m_emulation"))
        ImGui::SetNextWindowPos(custom_title_menu_pos(0), ImGuiCond_Always);
    if (ImGui::BeginPopup("##m_emulation"))
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
        if (ImGui::MenuItem("Debugger (udap)..."))
            open_remote_debugger_dialog();
        if (remote_debugger_)
            ImGui::TextDisabled("udap: %s", remote_debugger_->status_summary().c_str());
        ImGui::Separator();
        if (ImGui::MenuItem("Quit", "Ctrl+Q"))
        {
            SDL_Event quit_event;
            quit_event.type = SDL_QUIT;
            SDL_PushEvent(&quit_event);
        }
        ImGui::EndPopup();
    }

    if (ImGui::IsPopupOpen("##m_view"))
        ImGui::SetNextWindowPos(custom_title_menu_pos(1), ImGuiCond_Always);
    if (ImGui::BeginPopup("##m_view"))
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
        ImGui::EndPopup();
    }

    if (ImGui::IsPopupOpen("##m_devices"))
        ImGui::SetNextWindowPos(custom_title_menu_pos(2), ImGuiCond_Always);
    if (ImGui::BeginPopup("##m_devices"))
    {
        ImGui::MenuItem("Device Routing", nullptr, &show_devices_);
        ImGui::MenuItem("Monitor", nullptr, &show_monitor_);
        ImGui::MenuItem("Virtual Keyboard", nullptr, &show_keyboard_);
        ImGui::Separator();
        if (ImGui::MenuItem("Mount Floppy FD0..."))
            open_disk_mount_dialog(emu, 0);
        if (ImGui::MenuItem("Mount Floppy FD1..."))
            open_disk_mount_dialog(emu, 1);
        ImGui::TextDisabled("FD0: %s", emu.get_disk_path(0).empty() ? "(empty)" : emu.get_disk_path(0).c_str());
        ImGui::TextDisabled("FD1: %s", emu.get_disk_path(1).empty() ? "(empty)" : emu.get_disk_path(1).c_str());
        ImGui::EndPopup();
    }

    const ImGuiID dockspace_id = ImGui::GetID("##dockspace");
    ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();

    if (!startup_layout_applied_)
    {
        startup_layout_applied_ = true;
        ImGui::DockBuilderRemoveNode(dockspace_id);
        ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(vp->WorkSize.x, vp->WorkSize.y - title_bar_h));
        // Dock the display into the central node so it fills the window.
        ImGui::DockBuilderDockWindow("Partner Display", dockspace_id);
        ImGui::DockBuilderFinish(dockspace_id);
        // Hide the tab bar on the central node — display is always there, no tabs needed.
        ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspace_id);
        if (node) node->LocalFlags |= ImGuiDockNodeFlags_NoTabBar;
    }

    // Panels
    panels::render_display(display_, &display_viewport_);

    if (show_registers_)
        panels::render_registers(emu, &show_registers_);

    if (show_sio_)
        panels::render_sio(emu, key_buf_, &show_sio_);

    if (show_pio_)
        panels::render_pio(emu, &show_pio_);

    if (show_devices_)
        panels::render_devices(emu, &show_devices_);

    if (show_monitor_)
        panels::render_monitor(display_, &show_monitor_);

    render_disk_mount_dialog(emu);
    render_remote_debugger_dialog();

    if (show_dma_)
        panels::render_dma(emu, &show_dma_);

    if (show_rtc_)
        panels::render_rtc(emu, &show_rtc_);

    if (show_scn2674_)
        panels::render_scn2674(emu, &show_scn2674_);

    if (show_ef9367_)
        panels::render_ef9367(emu, &show_ef9367_);

    if (show_xebec_)
        panels::render_xebec(emu, &show_xebec_);

    if (show_disasm_)
        panels::render_disasm(emu, paused, action, &show_disasm_);

    if (show_fdc_)
        panels::render_fdc(emu, &show_fdc_);

    if (show_keyboard_)
    {
        // Keep this panel floating by default (not initially docked) because it benefits from horizontal space.
        const ImGuiViewport* vp = ImGui::GetMainViewport();
        const ImVec2 vk_default_pos(vp->Pos.x + 8.0f, vp->Pos.y + title_bar_h + 8.0f);
        const ImVec2 vk_default_size(
            std::min(1560.0f, vp->WorkSize.x - 16.0f),
            std::min(530.0f, vp->WorkSize.y - (title_bar_h + 24.0f))
        );
        ImGui::SetNextWindowDockID(0, ImGuiCond_Appearing);
        ImGui::SetNextWindowSize(vk_default_size, ImGuiCond_Appearing);
        ImGui::SetNextWindowPos(vk_default_pos, ImGuiCond_Appearing);
        ImGui::Begin("Virtual Keyboard", &show_keyboard_);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.90f, 0.90f, 0.90f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.96f, 0.96f, 0.96f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.82f, 0.82f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.08f, 0.08f, 0.08f, 1.00f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 9.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.2f);

        const float key_scale = 1.25f;
        const float key_h = 42.0f * key_scale;
        const bool show_host_hints = !gdp_keyboard_model;
        int key_uid = 0;
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
            const std::string visible = show_host_hints ? (std::string(dec_key) + "\n[" + host_key + "]") : std::string(dec_key);
            const std::string label = visible + "##kbd_" + std::to_string(key_uid++);
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
        };
        auto char_button = [&](const char* dec_key, const char* host_key, uint8_t ch, float w = 46.0f, float h = -1.0f) {
            if (h <= 0.0f) h = key_h;
            const bool blinked = push_blink_style(host_key);
            const std::string visible = show_host_hints ? (std::string(dec_key) + "\n[" + host_key + "]") : std::string(dec_key);
            const std::string label = visible + "##kbd_" + std::to_string(key_uid++);
            if (ImGui::Button(label.c_str(), ImVec2(w, h))) {
                blink_host_key(host_key);
                key_buf_.push_back(ch);
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
        const auto row_w = [&](std::initializer_list<float> widths) -> float {
            float sum = 0.0f;
            int count = 0;
            for (float w : widths) {
                sum += w;
                count++;
            }
            return sum + (count > 1 ? (float)(count - 1) * sx : 0.0f);
        };

        if (gdp_keyboard_model)
        {
            const float setup_w = 72.0f * key_scale;
            const float caps_w = 86.0f * key_scale;
            const float shift_w = 96.0f * key_scale;
            const float backspace_w = 96.0f * key_scale;
            const float break_w = 74.0f * key_scale;
            const float delete_w = 78.0f * key_scale;
            const float return_w = 96.0f * key_scale;
            const float linefeed_w = 76.0f * key_scale;
            const float noscroll_w = 68.0f * key_scale;
            const float space_w_vt100 = 520.0f * key_scale;
            const float side_gap = 24.0f * key_scale;
            const float arrows_w = row_w({k, k, k, k});
            const float keypad_w = std::max(
                row_w({k, k, k, k}),
                row_w({kp_zero_w, k, k}));
            const float main_w = std::max(
                std::max(
                    std::max(
                        row_w({k, k, k, k, k, k, k, k, k, k, k, k, k, backspace_w, break_w}),
                        row_w({mod, k, k, k, k, k, k, k, k, k, k, k, k, delete_w})),
                    row_w({mod, caps_w, k, k, k, k, k, k, k, k, k, k, return_w, k})),
                std::max(
                    row_w({noscroll_w, shift_w, k, k, k, k, k, k, k, k, k, shift_w, linefeed_w}),
                    row_w({space_w_vt100})));
            const float keyboard_total_w = main_w + side_gap + arrows_w + side_gap + keypad_w;
            const float avail_w = ImGui::GetContentRegionAvail().x;
            if (avail_w > keyboard_total_w) {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_w - keyboard_total_w) * 0.5f);
            }

            key_button("SET UP", "Pause/F12/Delete", "\xfe", setup_w, h);
            ImGui::Spacing();
            if (auto *gdp = dynamic_cast<partner_gdp *>(&emu))
            {
                const auto &kbd = gdp->get_keyboard();
                const float led_size = 14.0f;
                const float led_gap = 18.0f;
                const float led_strip_w = (8.0f * led_size) + (7.0f * led_gap);
                const float led_avail = ImGui::GetContentRegionAvail().x;
                if (led_avail > led_strip_w) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (led_avail - led_strip_w) * 0.5f);
                }
                for (int i = 0; i < 8; i++)
                {
                    ImGui::PushID(i + 5000);
                    if (i > 0) ImGui::SameLine();
                    const bool on = kbd.led(i);
                    const ImU32 fill_col = ImGui::GetColorU32(on ? ImVec4(0.95f, 0.22f, 0.22f, 1.0f)
                                                                  : ImVec4(0.24f, 0.24f, 0.24f, 1.0f));
                    const ImU32 ring_col = ImGui::GetColorU32(ImVec4(0.09f, 0.09f, 0.09f, 1.0f));
                    ImGui::InvisibleButton("##led", ImVec2(led_size, led_size));
                    const ImVec2 p = ImGui::GetItemRectMin();
                    const ImVec2 c(p.x + (led_size * 0.5f), p.y + (led_size * 0.5f));
                    const float r = (led_size * 0.5f) - 1.0f;
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    dl->AddCircleFilled(c, r, fill_col, 24);
                    dl->AddCircle(c, r, ring_col, 24, 1.0f);
                    ImGui::PopID();
                }
            }
            ImGui::Spacing();

            ImGui::BeginGroup();
            char_button("ESC", "Esc", 0x1B, k); ImGui::SameLine();
            char_button("!\n1", "1", '1', k); ImGui::SameLine();
            char_button("@\n2", "2", '2', k); ImGui::SameLine();
            char_button("#\n3", "3", '3', k); ImGui::SameLine();
            char_button("$\n4", "4", '4', k); ImGui::SameLine();
            char_button("%\n5", "5", '5', k); ImGui::SameLine();
            char_button("^\n6", "6", '6', k); ImGui::SameLine();
            char_button("&\n7", "7", '7', k); ImGui::SameLine();
            char_button("*\n8", "8", '8', k); ImGui::SameLine();
            char_button("(\n9", "9", '9', k); ImGui::SameLine();
            char_button(")\n0", "0", '0', k); ImGui::SameLine();
            char_button("_\n-", "-", '-', k); ImGui::SameLine();
            char_button("+\n=", "=", '=', k); ImGui::SameLine();
            char_button("~\n`", "`", '`', k); ImGui::SameLine();
            char_button("BACK\nSPACE", "Backspace", 0x08, backspace_w, h); ImGui::SameLine();
            key_button("BREAK", "Break", "", break_w, h);

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
            char_button("{\n[", "[", '[', k); ImGui::SameLine();
            char_button("}\n]", "]", ']', k); ImGui::SameLine();
            char_button("DELETE", "Delete", 0x7F, delete_w, h);

            key_button("CTRL", "Ctrl", "", mod, h); ImGui::SameLine();
            key_button("CAPS\nLOCK", "CapsLock", "", caps_w, h); ImGui::SameLine();
            char_button("A", "A", 'a', k); ImGui::SameLine();
            char_button("S", "S", 's', k); ImGui::SameLine();
            char_button("D", "D", 'd', k); ImGui::SameLine();
            char_button("F", "F", 'f', k); ImGui::SameLine();
            char_button("G", "G", 'g', k); ImGui::SameLine();
            char_button("H", "H", 'h', k); ImGui::SameLine();
            char_button("J", "J", 'j', k); ImGui::SameLine();
            char_button("K", "K", 'k', k); ImGui::SameLine();
            char_button("L", "L", 'l', k); ImGui::SameLine();
            char_button(":\n;", ";", ';', k); ImGui::SameLine();
            char_button("\"\n'", "'", '\'', k); ImGui::SameLine();
            key_button("RETURN", "Enter", "\r", return_w, h); ImGui::SameLine();
            char_button("|\n\\", "\\", '\\', k);

            key_button("NO\nSCROLL", "ScrollLock", "\xb0", noscroll_w, h); ImGui::SameLine();
            key_button("SHIFT", "LShift", "", shift_w, h); ImGui::SameLine();
            char_button("Z", "Z", 'z', k); ImGui::SameLine();
            char_button("X", "X", 'x', k); ImGui::SameLine();
            char_button("C", "C", 'c', k); ImGui::SameLine();
            char_button("V", "V", 'v', k); ImGui::SameLine();
            char_button("B", "B", 'b', k); ImGui::SameLine();
            char_button("N", "N", 'n', k); ImGui::SameLine();
            char_button("M", "M", 'm', k); ImGui::SameLine();
            char_button("<\n,", ",", ',', k); ImGui::SameLine();
            char_button(">\n.", ".", '.', k); ImGui::SameLine();
            char_button("?\n/", "/", '/', k); ImGui::SameLine();
            key_button("SHIFT", "RShift", "", shift_w, h); ImGui::SameLine();
            key_button("LINE\nFEED", "Ctrl+J", "\n", linefeed_w, h);

            key_button("SPACE", "Space", " ", space_w_vt100, h);
            ImGui::EndGroup();

            ImGui::SameLine(0.0f, side_gap);
            ImGui::BeginGroup();
            key_button("UP", "Up", "\x1b""A", k, h); ImGui::SameLine();
            key_button("DOWN", "Down", "\x1b""B", k, h); ImGui::SameLine();
            key_button("LEFT", "Left", "\x1b""D", k, h); ImGui::SameLine();
            key_button("RIGHT", "Right", "\x1b""C", k, h);
            ImGui::EndGroup();

            ImGui::SameLine(0.0f, side_gap);
            ImGui::BeginGroup();
            char_button("PF1", "F1", 0x04, k, h); ImGui::SameLine();
            char_button("PF2", "F2", 0x05, k, h); ImGui::SameLine();
            char_button("PF3", "F3", 0x06, k, h); ImGui::SameLine();
            char_button("PF4", "F4", 0x07, k, h);

            char_button("7", "KP7", '7', k); ImGui::SameLine();
            char_button("8", "KP8", '8', k); ImGui::SameLine();
            char_button("9", "KP9", '9', k); ImGui::SameLine();
            char_button("-", "KP-", '-', k);

            char_button("4", "KP4", '4', k); ImGui::SameLine();
            char_button("5", "KP5", '5', k); ImGui::SameLine();
            char_button("6", "KP6", '6', k); ImGui::SameLine();
            char_button(",", "KP,", ',', k);

            char_button("1", "KP1", '1', k); ImGui::SameLine();
            char_button("2", "KP2", '2', k); ImGui::SameLine();
            char_button("3", "KP3", '3', k); ImGui::SameLine();
            key_button("ENTER", "KP Enter", "\r", k, h);

            char_button("0", "KP0", '0', kp_zero_w); ImGui::SameLine();
            char_button(".", "KP.", '.', k);
            ImGui::EndGroup();
        }
        else
        {
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

            ImGui::BeginGroup();
            char_button("ESC", "Esc", 0x1B, mod);
            ImGui::SameLine();
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

            ImGui::SameLine(0.0f, inter_group_gap_1);
            ImGui::BeginGroup();
            key_button("UP", "Up", "\x1b""A", k, h); ImGui::SameLine();
            key_button("DOWN", "Down", "\x1b""B", k, h); ImGui::SameLine();
            key_button("LEFT", "Left", "\x1b""D", k, h); ImGui::SameLine();
            key_button("RIGHT", "Right", "\x1b""C", k, h);

            char_button("-", "-", '-', k); ImGui::SameLine();
            char_button("+", "=", '+', k); ImGui::SameLine();
            char_button("`", "`", '`', k); ImGui::SameLine();
            char_button("BACKSP", "Backspace", 0x08, wide, h); ImGui::SameLine();
            key_button("BREAK", "Break", "", mod, h);

            char_button("DELETE", "Delete", 0x7F, wide, h); ImGui::SameLine();
            char_button("|", "\\", '\\', k);

            key_button("RETURN", "Enter", "\r", ret_w, h); ImGui::SameLine();
            key_button("LINEFD", "Ctrl+J", "\n", mod, h);
            ImGui::EndGroup();

            ImGui::SameLine(0.0f, inter_group_gap_2);
            ImGui::BeginGroup();
            char_button("PF1", "F1", 0x04, k, h); ImGui::SameLine();
            char_button("PF2", "F2", 0x05, k, h); ImGui::SameLine();
            char_button("PF3", "F3", 0x06, k, h); ImGui::SameLine();
            char_button("PF4", "F4", 0x07, k, h);

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
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(5);

        ImGui::Separator();
        ImGui::Text("Last: %s", last_click_info[0] ? last_click_info : "(none)");
        ImGui::TextUnformatted("SET-UP host key: Pause, F12, or Delete.");
        ImGui::TextUnformatted("Quit: Ctrl+Q.");
        ImGui::TextUnformatted("Debugger keys reserved: F10, F11.");
        ImGui::End();
    }

    draw_window_chrome();
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
    glClearColor(chrome_theme().gl_clear[0], chrome_theme().gl_clear[1], chrome_theme().gl_clear[2], 1.0f);
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
