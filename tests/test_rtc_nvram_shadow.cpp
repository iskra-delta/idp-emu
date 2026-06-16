#include <array>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#include "partner.hpp"

namespace {

class partner_test : public partner {
public:
    using partner::partner;
    using partner::io_read;
    using partner::io_write;
};

static int fail(const char *msg)
{
    std::printf("FAIL %s\n", msg);
    return 1;
}

} // namespace

int main()
{
    namespace fs = std::filesystem;

    const fs::path tmp_dir = fs::temp_directory_path() / "idp-emu-nvram-shadow";
    const fs::path nvram_path = tmp_dir / "shadow.bin";
    const std::array<uint8_t, 8> defaults = {
        0xF0, 0x98, 0xFF, 0x01, 0x85, 0x07, 0x00, 0x57
    };
    const std::array<uint8_t, 8> updated = {
        0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0
    };

    std::error_code ec;
    fs::create_directories(tmp_dir, ec);
    fs::remove(nvram_path, ec);

    {
        partner_test emu(nvram_path.string());
        std::ifstream file(nvram_path, std::ios::binary);
        if (!file)
            return fail("shadow NVRAM file was not created");

        std::array<uint8_t, 8> disk{};
        file.read(reinterpret_cast<char *>(disk.data()), (std::streamsize)disk.size());
        if (file.gcount() != (std::streamsize)disk.size())
            return fail("shadow NVRAM file size is not 8 bytes");
        if (disk != defaults)
            return fail("shadow NVRAM defaults do not match seeded Partner bytes");

        for (size_t i = 0; i < updated.size(); ++i)
            emu.io_write((uint16_t)(0xA8 + i), updated[i]);
    }

    {
        std::ifstream file(nvram_path, std::ios::binary);
        if (!file)
            return fail("shadow NVRAM file disappeared after writes");

        std::array<uint8_t, 8> disk{};
        file.read(reinterpret_cast<char *>(disk.data()), (std::streamsize)disk.size());
        if (file.gcount() != (std::streamsize)disk.size())
            return fail("shadow NVRAM file size changed after writes");
        if (disk != updated)
            return fail("shadow NVRAM file did not persist written bytes");
    }

    {
        partner_test emu(nvram_path.string());
        for (size_t i = 0; i < updated.size(); ++i) {
            const uint8_t value = emu.io_read((uint16_t)(0xA8 + i));
            if (value != updated[i])
                return fail("shadow NVRAM contents were not restored on reload");
        }
    }

    fs::remove(nvram_path, ec);
    fs::remove(tmp_dir, ec);
    std::puts("PASS rtc_nvram_shadow");
    return 0;
}
