#include "gui/display.hpp"
#include "gui/screen_recorder.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

bool display::capture_rgba(std::vector<uint8_t> &pixels, int &width, int &height,
                           std::string &error) const
{
    width = 16;
    height = 12;
    pixels.resize(static_cast<size_t>(width * height * 4));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const size_t offset = static_cast<size_t>((y * width + x) * 4);
            pixels[offset] = static_cast<uint8_t>(x * 16);
            pixels[offset + 1] = static_cast<uint8_t>(y * 20);
            pixels[offset + 2] = 0x40;
            pixels[offset + 3] = 0xFF;
        }
    }
    error.clear();
    return true;
}

namespace {

uint32_t read_u32(const std::vector<uint8_t> &bytes, size_t offset)
{
    return static_cast<uint32_t>(bytes[offset]) |
           (static_cast<uint32_t>(bytes[offset + 1]) << 8) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24);
}

size_t find_fourcc(const std::vector<uint8_t> &bytes, const char value[5])
{
    const auto found = std::search(bytes.begin(), bytes.end(), value, value + 4);
    return found == bytes.end() ? bytes.size() :
        static_cast<size_t>(std::distance(bytes.begin(), found));
}

} // namespace

int main()
{
    int failures = 0;
#define CHECK(condition) do { \
    if (!(condition)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

    const std::filesystem::path output =
        std::filesystem::path(IDP_SOURCE_ROOT) /
        "tests/dump/screen-recorder-audio.avi";
    std::error_code ec;
    std::filesystem::remove(output, ec);

    display source;
    screen_recorder recorder;
    std::string error;
    CHECK(recorder.start(source, output, 1000, true, error));

    std::vector<int16_t> samples(4410);
    for (size_t i = 0; i < samples.size(); ++i)
        samples[i] = (i & 1u) ? 12000 : -12000;
    CHECK(recorder.append_audio_samples(samples.data(), samples.size(), error));
    CHECK(recorder.capture_due(source, 401000, error));
    CHECK(recorder.stop(error));

    std::ifstream stream(output, std::ios::binary);
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(stream)),
                                     std::istreambuf_iterator<char>());
    CHECK(bytes.size() > 256);
    if (bytes.size() > 256)
    {
        CHECK(find_fourcc(bytes, "RIFF") == 0);
        CHECK(read_u32(bytes, 4) == bytes.size() - 8);
        const size_t avih = find_fourcc(bytes, "avih");
        const size_t auds = find_fourcc(bytes, "auds");
        CHECK(avih < bytes.size());
        CHECK(auds < bytes.size());
        CHECK(find_fourcc(bytes, "00dc") < bytes.size());
        CHECK(find_fourcc(bytes, "01wb") < bytes.size());
        CHECK(find_fourcc(bytes, "idx1") < bytes.size());
        if (avih < bytes.size() - 36)
            CHECK(read_u32(bytes, avih + 8 + 24) == 2);
        if (auds < bytes.size() - 36)
            CHECK(read_u32(bytes, auds + 32) == samples.size());
    }

    if (std::getenv("IDP_KEEP_TEST_AVI") == nullptr)
        std::filesystem::remove(output, ec);
#undef CHECK
    if (failures == 0)
    {
        std::puts("test_screen_recorder: all tests passed");
        return 0;
    }
    std::printf("test_screen_recorder: %d failure(s)\n", failures);
    return 1;
}
