#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

// Emulator-facing wrapper around the portable negotiated libsquid wire-v2 engine.
// The library owns framing, CRC, ARQ, retransmission and channel queues; this
// class adds the channel-3 packet-length convention used by squid-server.
class squid_link_server
{
public:
    static constexpr std::uint8_t minimum_payload_bytes = 16;
    static constexpr std::uint8_t maximum_payload_bytes = 112;
    static constexpr std::uint8_t default_payload_bytes = maximum_payload_bytes;
    static constexpr std::size_t maximum_packet_size = 255;
    static constexpr std::size_t serial_input_capacity = 64;

    struct request
    {
        std::uint64_t session = 0;
        std::vector<std::uint8_t> payload;
    };

    squid_link_server();
    ~squid_link_server();

    // Applies to subsequently constructed and reset internal endpoints.
    static bool set_default_payload_bytes(std::uint8_t bytes);

    squid_link_server(const squid_link_server &) = delete;
    squid_link_server &operator=(const squid_link_server &) = delete;

    void reset();
    bool can_receive_serial_byte() const;
    bool receive_serial_byte(std::uint8_t byte);
    void service();

    bool take_serial_byte(std::uint8_t &byte);
    bool take_request(request &value);
    bool submit_response(std::uint64_t session,
                         const std::uint8_t *data,
                         std::size_t size);

    bool link_up() const;
    std::uint64_t session() const;
    std::size_t pending_serial_bytes() const;

private:
    struct implementation;
    std::unique_ptr<implementation> implementation_;
};
