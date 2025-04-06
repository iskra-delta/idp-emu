#include "partner_crt.hpp"

uint8_t partner_crt::io_read(uint16_t port)
{
    if (port == 0xD9)
        pins |= Z80SIO_CS_A;
    if (port == 0xE1)
        pins |= Z80SIO_CS_B;

    pins |= Z80SIO_CE | Z80SIO_IORQ | Z80SIO_RD;
    pins &= ~Z80SIO_WR;

    pins = z80sio_tick(&sio, pins);
    uint8_t data = Z80SIO_GET_DATA(pins);

    pins &= ~(Z80SIO_CE | Z80SIO_IORQ | Z80SIO_RD | Z80SIO_CS_A | Z80SIO_CS_B);
    return data;
}

void partner_crt::io_write(uint16_t port, uint8_t data)
{
    if (port == 0xD8 || port == 0xD9)
        pins |= Z80SIO_CS_A;
    if (port == 0xE0 || port == 0xE1)
        pins |= Z80SIO_CS_B;

    if (port == 0xD8)
    {
        std::cout << static_cast<char>(data);
        std::cout.flush();
    }

    Z80SIO_SET_DATA(pins, data);
    pins |= Z80SIO_CE | Z80SIO_IORQ | Z80SIO_WR;
    pins &= ~Z80SIO_RD;

    pins = z80sio_tick(&sio, pins);
    pins &= ~(Z80SIO_CE | Z80SIO_IORQ | Z80SIO_WR | Z80SIO_CS_A | Z80SIO_CS_B);
}
