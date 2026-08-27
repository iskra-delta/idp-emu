#pragma once

#include "squid_link_server.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>

class internal_squid_server
{
public:
    internal_squid_server();
    ~internal_squid_server();

    internal_squid_server(const internal_squid_server &) = delete;
    internal_squid_server &operator=(const internal_squid_server &) = delete;

    void reset_link();
    bool can_receive_serial_byte() const;
    bool receive_serial_byte(std::uint8_t byte);
    void service(std::deque<std::uint8_t> &serial_receive_queue,
                 bool guest_ready);

    bool link_up() const;
    bool busy() const;
    std::string status_text() const;
    std::size_t pending_serial_bytes() const;

private:
    struct implementation;
    std::unique_ptr<implementation> implementation_;
    squid_link_server link_;
};
