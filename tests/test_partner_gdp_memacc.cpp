#include "partner_gdp.hpp"

#include <cstdio>

class partner_gdp_test_shim : public partner_gdp
{
public:
    using partner_gdp::partner_gdp;

    uint8_t read_port(uint16_t port)
    {
        return io_read(port);
    }
};

int main()
{
    partner_gdp_test_shim emu(terminal_profile::vt100_ansi);
    emu.reset();

    const uint8_t a = emu.read_port(0x36);
    const uint8_t b = emu.read_port(0x36);
    const uint8_t c = emu.read_port(0x36);

    if ((a & 0x10u) == 0u || (b & 0x10u) != 0u || (c & 0x10u) == 0u) {
        std::printf("test_partner_gdp_memacc: FAIL a=%02X b=%02X c=%02X\n",
                    (unsigned)a, (unsigned)b, (unsigned)c);
        return 1;
    }

    std::puts("test_partner_gdp_memacc: PASS");
    return 0;
}
