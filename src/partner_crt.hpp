#pragma once
#include "partner.hpp"
#include <iostream>

class partner_crt : public partner
{
public:
    partner_crt() = default;

protected:
    uint8_t io_read(uint16_t port) override;
    void io_write(uint16_t port, uint8_t data) override;
};
