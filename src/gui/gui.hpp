#pragma once
#include "display.hpp"
#include "file_dialog.hpp"
#include "panel_display.hpp"
#include "screen_recorder.hpp"
#include "terminal_keymap.hpp"
#include "../debugger.hpp"
#include "../terminal/terminal_factory.hpp"
#include <SDL.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

class partner;
class dap_debugger;
enum class partner_gdp_keyboard_sound : uint8_t;

class gui
{
public:
    struct remote_debugger_request {
        enum class kind {
            start,
            stop
        };

        kind action = kind::start;
        std::string host;
        uint16_t port = 0;
    };

    bool init(const std::string &title, int width, int height);
    void shutdown();

    bool process_events(partner &emu, bool &paused, dbg_action &action);

    void begin_frame();
    void render_panels(partner &emu, bool &paused, dbg_action &action);
    void end_frame();

    display &get_display() { return display_; }
    void set_terminal_profile(terminal_profile profile) { terminal_profile_ = profile; }
    void set_remote_debugger(dap_debugger *debugger) { remote_debugger_ = debugger; }
    std::optional<remote_debugger_request> take_remote_debugger_request();
    void set_remote_debugger_error(const std::string &error) { remote_debug_error_ = error; }

    std::vector<uint8_t> drain_keys();

private:
    void blink_host_key(const char *host_key);
    bool is_host_key_blinking(const char *host_key);
    void close_all_views();
    void queue_keyboard_tone(float freq_hz, float duration_ms, float amplitude, bool square_wave);
    void queue_keyboard_sound(partner_gdp_keyboard_sound sound);
    void service_keyboard_sound(partner &emu);
    void service_covox_audio(partner &emu);
    void reset_covox_audio_timeline(partner &emu, uint64_t tick);
    void open_disk_mount_dialog(partner &emu, int drive);
    void open_screenshot_dialog();
    void open_recording_dialog();
    void render_file_dialog(partner &emu);
    void render_file_operation_error();
    void start_screen_recording(const std::filesystem::path &path, partner &emu);
    void stop_screen_recording();
    void service_screen_recording(partner &emu);
    void open_remote_debugger_dialog();
    void render_remote_debugger_dialog();

    SDL_Window *window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    display display_;
    file_dialog file_dialog_;
    screen_recorder screen_recorder_;

    enum class file_dialog_action {
        none,
        mount_disk,
        save_screenshot,
        start_recording
    };

    bool show_registers_ = false;
    bool show_disasm_ = false;
    bool show_fdc_ = false;
    bool show_sio_ = false;
    bool show_pio_ = false;
    bool show_dma_ = false;
    bool show_xebec_ = false;
    bool show_rtc_ = false;
    bool show_scn2674_ = false;
    bool show_ef9367_ = false;
    bool show_keyboard_ = false;
    bool show_devices_ = false;
    bool show_monitor_ = false;
    int open_menu_ = -1;
    int active_menu_ = -1;
    bool startup_layout_applied_ = false;
    bool mouse_cursor_hidden_ = false;
    bool mouse_relative_active_ = false;
    bool mouse_left_down_ = false;
    bool mouse_middle_down_ = false;
    bool mouse_right_down_ = false;
    int disk_mount_drive_ = 1;
    panels::display_viewport_info display_viewport_{};
    terminal_profile terminal_profile_ = terminal_profile::vt52;
    SDL_AudioDeviceID audio_device_ = 0;
    SDL_AudioSpec audio_spec_{};
    bool covox_audio_timeline_active_ = false;
    uint64_t covox_audio_tick_ = 0;
    uint64_t covox_audio_phase_ = 0;
    std::array<uint8_t, 2> covox_audio_levels_{{0x80, 0x80}};
    std::array<bool, 2> covox_audio_attached_{{false, false}};

    std::vector<uint8_t> key_buf_;
    terminal_key_repeat_limiter key_repeat_limiter_{};
    std::unordered_map<std::string, uint32_t> key_blink_until_ms_{};
    dap_debugger *remote_debugger_ = nullptr;
    std::string screenshot_saved_path_;
    std::string recording_error_;
    std::string recording_saved_path_;
    std::string file_operation_error_;
    std::string remote_debug_host_ = "127.0.0.1";
    std::string remote_debug_port_text_ = "4711";
    std::string remote_debug_error_;
    std::optional<remote_debugger_request> pending_remote_debugger_request_;
    file_dialog_action pending_file_dialog_action_ = file_dialog_action::none;
    bool open_file_operation_error_popup_ = false;
    bool open_remote_debugger_popup_ = false;
    static constexpr uint32_t key_blink_ms_ = 130;
};
