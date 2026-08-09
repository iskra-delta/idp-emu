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

    size_t accepted = 0;
    for (size_t i = 0; i < 1024; i++) {
        if (emu.key_input('K'))
            accepted++;
    }
    const size_t pending = emu.pending_key_count();
    if (accepted > partner_gdp::KEY_FIFO_CAPACITY + 1u ||
        pending > partner_gdp::KEY_FIFO_CAPACITY + 1u ||
        accepted == 1024u)
    {
        std::printf("test_partner_gdp_memacc: FAIL keyboard accepted=%zu pending=%zu\n",
                    accepted, pending);
        return 1;
    }

    emu.reset();
    if (emu.pending_key_count() != 0u) {
        std::puts("test_partner_gdp_memacc: FAIL keyboard reset did not clear backlog");
        return 1;
    }

    std::puts("test_partner_gdp_memacc: PASS");
    return 0;
}
