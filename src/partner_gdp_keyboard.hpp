#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>

enum class partner_gdp_keyboard_sound : uint8_t {
    none = 0,
    key_click,
    short_beep,
    long_beep,
};

class partner_gdp_keyboard
{
public:
    void reset();
    void host_write(uint8_t value);
    void local_keypress();
    bool pop_sound(partner_gdp_keyboard_sound &out);

    const std::array<bool, 8>& leds() const { return leds_; }
    bool led(int index) const { return (index >= 0) && (index < 8) ? leds_[static_cast<std::size_t>(index)] : false; }

    bool key_click_enabled() const { return key_click_enabled_; }
    bool autorepeat_enabled() const { return autorepeat_enabled_; }
    bool qwertz_enabled() const { return qwertz_enabled_; }

    uint8_t last_host_command() const { return last_host_command_; }
    uint64_t host_command_count() const { return host_command_count_; }
    uint64_t sound_sequence() const { return sound_sequence_; }
    partner_gdp_keyboard_sound last_sound() const { return last_sound_; }
    size_t pending_sound_count() const { return pending_sounds_.size(); }

private:
    void update_leds_from_command(uint8_t value);
    void maybe_update_mode_from_command(uint8_t value);
    void emit_sound(partner_gdp_keyboard_sound kind);

    std::array<bool, 8> leds_{};
    bool key_click_enabled_ = true;
    bool autorepeat_enabled_ = true;
    bool qwertz_enabled_ = false;
    uint8_t last_host_command_ = 0;
    uint64_t host_command_count_ = 0;
    uint64_t sound_sequence_ = 0;
    partner_gdp_keyboard_sound last_sound_ = partner_gdp_keyboard_sound::none;
    std::deque<partner_gdp_keyboard_sound> pending_sounds_{};
};
