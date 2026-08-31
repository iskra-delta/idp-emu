#include "startup_input.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {
int failures = 0;

void expect(bool condition, const char *message)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

void test_command_decoding()
{
    std::vector<uint8_t> keys;
    std::string error;
    expect(startup_input::decode("b:\\ntest\\n", keys, error),
           "escaped command string decodes");
    const std::vector<uint8_t> expected = {'b', ':', 0x0D, 't', 'e', 's', 't', 0x0D};
    expect(keys == expected, "escaped newlines become Enter keys");

    keys.clear();
    expect(startup_input::decode("b:\ntest\r\n", keys, error),
           "literal newline command string decodes");
    expect(keys == expected, "literal LF and CRLF become one Enter each");

    keys.clear();
    expect(startup_input::decode("\\t\\e\\x41\\\\", keys, error),
           "control and hexadecimal escapes decode");
    const std::vector<uint8_t> escaped = {0x09, 0x1B, 'A', '\\'};
    expect(keys == escaped, "decoded control bytes are correct");
}

void test_invalid_escapes()
{
    std::vector<uint8_t> keys = {'x'};
    std::string error;
    expect(!startup_input::decode("bad\\", keys, error),
           "trailing backslash is rejected");
    expect(keys == std::vector<uint8_t>{'x'},
           "failed decoding does not append partial input");
    expect(!startup_input::decode("\\xG0", keys, error),
           "invalid hexadecimal escape is rejected");
    expect(!startup_input::decode("\\q", keys, error),
           "unknown escape is rejected");
}

void test_cpm_prompt_detection()
{
    expect(startup_input::cpm_prompt_visible("Partner G\n\nA>"),
           "system-drive CP/M prompt is recognized");
    expect(startup_input::cpm_prompt_visible("B>dir"),
           "alternate CP/M drive prompt is recognized");
    expect(!startup_input::cpm_prompt_visible("Partner G booting..."),
           "boot text is not mistaken for a command prompt");
    expect(!startup_input::cpm_prompt_visible("TESTING MEMORY >"),
           "unqualified greater-than sign is not a command prompt");
}

void test_timing()
{
    using namespace std::chrono_literals;
    startup_input input({'a', 'b'}, 100ms, 40ms, 200ms);
    const startup_input::clock::time_point start{};
    input.start(start);

    expect(!input.take_due(start + 99ms), "initial delay is observed");
    expect(input.peek_due(start + 100ms) == std::optional<uint8_t>{'a'},
           "due key can be inspected without consuming it");
    expect(input.peek_due(start + 100ms) == std::optional<uint8_t>{'a'},
           "inspecting a due key is repeatable until accepted");
    expect(input.take_due(start + 100ms) == std::optional<uint8_t>{'a'},
           "first key is produced after initial delay");
    expect(!input.take_due(start + 139ms), "key interval is observed");
    expect(input.take_due(start + 140ms) == std::optional<uint8_t>{'b'},
           "second key is produced after key interval");
    expect(input.finished(), "input reports completion");
    expect(!input.take_due(start + 1000ms), "completed input produces no more keys");

    startup_input commands({'b', ':', 0x0D, 'm'}, 0ms, 40ms, 200ms);
    commands.start(start);
    expect(commands.take_due(start) == std::optional<uint8_t>{'b'},
           "command typing starts normally");
    expect(commands.take_due(start + 40ms) == std::optional<uint8_t>{':'},
           "normal key interval remains in use");
    expect(commands.take_due(start + 80ms) == std::optional<uint8_t>{0x0D},
           "Enter is emitted at the normal interval");
    expect(!commands.take_due(start + 279ms),
           "the next command observes the Enter delay");
    expect(commands.take_due(start + 280ms) == std::optional<uint8_t>{'m'},
           "the first key after Enter uses the dedicated delay");
}
}

int main()
{
    test_command_decoding();
    test_invalid_escapes();
    test_cpm_prompt_detection();
    test_timing();
    if (failures == 0)
    {
        std::puts("test_startup_input: PASS");
        return 0;
    }
    std::printf("test_startup_input: %d failure(s)\n", failures);
    return 1;
}
