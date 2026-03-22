#include "partner_gdp_keyboard.hpp"

namespace {
constexpr uint8_t GDP_KEYBOARD_MODE_APPLY_MASK = 0x57;
constexpr std::size_t GDP_KEYBOARD_MAX_PENDING_SOUNDS = 128;
}

void partner_gdp_keyboard::reset()
{
    leds_.fill(false);
    key_click_enabled_ = true;
    autorepeat_enabled_ = true;
    qwertz_enabled_ = false;
    last_host_command_ = 0;
    host_command_count_ = 0;
    sound_sequence_ = 0;
    last_sound_ = partner_gdp_keyboard_sound::none;
    pending_sounds_.clear();
}

void partner_gdp_keyboard::update_leds_from_command(uint8_t value)
{
    uint8_t led_bits = value;
    switch (value & 0x03u) {
    case 0x00:
        led_bits = (uint8_t)(value - 2u);
        break;
    case 0x02:
    case 0x03:
        led_bits = (uint8_t)(value - 1u);
        break;
    default:
        break;
    }
    for (int i = 0; i < 8; i++) {
        leds_[static_cast<std::size_t>(i)] = ((led_bits >> i) & 1u) != 0;
    }
}

void partner_gdp_keyboard::maybe_update_mode_from_command(uint8_t value)
{
    if ((value & GDP_KEYBOARD_MODE_APPLY_MASK) != GDP_KEYBOARD_MODE_APPLY_MASK) {
        return;
    }
    key_click_enabled_ = (value & 0x08u) == 0;
    autorepeat_enabled_ = (value & 0x20u) == 0;
    qwertz_enabled_ = (value & 0x80u) != 0;
}

void partner_gdp_keyboard::emit_sound(partner_gdp_keyboard_sound kind)
{
    last_sound_ = kind;
    sound_sequence_++;
    pending_sounds_.push_back(kind);
    if (pending_sounds_.size() > GDP_KEYBOARD_MAX_PENDING_SOUNDS) {
        pending_sounds_.pop_front();
    }
}

void partner_gdp_keyboard::host_write(uint8_t value)
{
    last_host_command_ = value;
    host_command_count_++;
    update_leds_from_command(value);
    maybe_update_mode_from_command(value);

    if ((value & 0x07u) == 0x04u) {
        emit_sound(partner_gdp_keyboard_sound::long_beep);
    } else if ((value & 0x03u) == 0x02u) {
        emit_sound(partner_gdp_keyboard_sound::short_beep);
    }
}

void partner_gdp_keyboard::local_keypress()
{
    if (key_click_enabled_) {
        emit_sound(partner_gdp_keyboard_sound::key_click);
    }
}

bool partner_gdp_keyboard::pop_sound(partner_gdp_keyboard_sound &out)
{
    if (pending_sounds_.empty()) {
        return false;
    }
    out = pending_sounds_.front();
    pending_sounds_.pop_front();
    return true;
}
