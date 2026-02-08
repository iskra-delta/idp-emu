#include "partner_crt.hpp"

uint8_t partner_crt::io_read(uint16_t port)
{
    // CRT-specific I/O handling can go here
    // For now, just call the base class which handles all chips
    return partner::io_read(port);
}

void partner_crt::io_write(uint16_t port, uint8_t data)
{
    port &= 0xFF;

    // Special handling for CRT text output on SIO port 0xD8 (data register)
    if (port == 0xD8)
    {
        // Echo characters to console for debugging
        std::cout << static_cast<char>(data);
        std::cout.flush();
    }

    // Let base class handle the actual chip I/O
    partner::io_write(port, data);
}