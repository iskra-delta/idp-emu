#pragma once
#include "display.hpp"
#include "panel_display.hpp"
#include "../debugger.hpp"
#include "../terminal/terminal_factory.hpp"
#include <SDL.h>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include <optional>

class partner;
class remote_debugger;
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
    void set_remote_debugger(remote_debugger *debugger) { remote_debugger_ = debugger; }
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
    void open_disk_mount_dialog(partner &emu, int drive);
    void render_disk_mount_dialog(partner &emu);
    void open_remote_debugger_dialog();
    void render_remote_debugger_dialog();

    SDL_Window *window_ = nullptr;
    SDL_GLContext gl_context_ = nullptr;
    display display_;

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
    bool startup_layout_applied_ = false;
    bool mouse_cursor_hidden_ = false;
    bool mouse_relative_active_ = false;
    bool mouse_left_down_ = false;
    bool mouse_middle_down_ = false;
    bool mouse_right_down_ = false;
    bool open_disk_mount_popup_ = false;
    int disk_mount_drive_ = 1;
    panels::display_viewport_info display_viewport_{};
    terminal_profile terminal_profile_ = terminal_profile::vt52;
    SDL_AudioDeviceID audio_device_ = 0;
    SDL_AudioSpec audio_spec_{};

    std::vector<uint8_t> key_buf_;
    std::unordered_map<std::string, uint32_t> key_blink_until_ms_{};
    remote_debugger *remote_debugger_ = nullptr;
    std::string disk_mount_path_;
    std::string disk_mount_error_;
    std::string remote_debug_host_ = "127.0.0.1";
    std::string remote_debug_port_text_ = "9000";
    std::string remote_debug_error_;
    std::optional<remote_debugger_request> pending_remote_debugger_request_;
    bool open_remote_debugger_popup_ = false;
    static constexpr uint32_t key_blink_ms_ = 130;
};
