#include <cstdint>
#include <cstdio>

#include "partner_gdp_keyboard.hpp"

namespace {

static int test_defaults_and_click_behavior()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    partner_gdp_keyboard kbd{};
    kbd.reset();
    CHECK(kbd.key_click_enabled());
    CHECK(kbd.autorepeat_enabled());
    CHECK(!kbd.qwertz_enabled());
    CHECK(kbd.pending_sound_count() == 0);

    kbd.local_keypress();
    CHECK(kbd.pending_sound_count() == 1);

    partner_gdp_keyboard_sound sound = partner_gdp_keyboard_sound::none;
    CHECK(kbd.pop_sound(sound));
    CHECK(sound == partner_gdp_keyboard_sound::key_click);
    CHECK(!kbd.pop_sound(sound));

#undef CHECK
    return fails;
}

static int test_beep_decode_and_queue_order()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    partner_gdp_keyboard kbd{};
    kbd.reset();
    kbd.host_write(0x02); // short beep
    kbd.host_write(0x04); // long beep
    CHECK(kbd.host_command_count() == 2);

    partner_gdp_keyboard_sound sound = partner_gdp_keyboard_sound::none;
    CHECK(kbd.pop_sound(sound));
    CHECK(sound == partner_gdp_keyboard_sound::short_beep);
    CHECK(kbd.pop_sound(sound));
    CHECK(sound == partner_gdp_keyboard_sound::long_beep);
    CHECK(!kbd.pop_sound(sound));

#undef CHECK
    return fails;
}

static int test_mode_command_disable_click_and_autorepeat()
{
    int fails = 0;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++fails; } } while (0)

    partner_gdp_keyboard kbd{};
    kbd.reset();

    kbd.host_write(0x7Fu); // 0x57 mask + keyclick off + autorepeat off
    CHECK(!kbd.key_click_enabled());
    CHECK(!kbd.autorepeat_enabled());
    CHECK(!kbd.qwertz_enabled());

    kbd.local_keypress();
    CHECK(kbd.pending_sound_count() == 0);

    kbd.host_write(0xD7u); // 0x57 mask + qwertz on + keyclick on + autorepeat on
    CHECK(kbd.key_click_enabled());
    CHECK(kbd.autorepeat_enabled());
    CHECK(kbd.qwertz_enabled());

    kbd.local_keypress();
    CHECK(kbd.pending_sound_count() == 1);

#undef CHECK
    return fails;
}

} // namespace

int main()
{
    int fails = 0;
    fails += test_defaults_and_click_behavior();
    fails += test_beep_decode_and_queue_order();
    fails += test_mode_command_disable_click_and_autorepeat();

    if (fails == 0) {
        std::printf("test_partner_gdp_keyboard: all tests passed\n");
        return 0;
    }

    std::printf("test_partner_gdp_keyboard: %d failure(s)\n", fails);
    return 1;
}
