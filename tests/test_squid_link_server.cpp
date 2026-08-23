#include "squid_link_server.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <vector>

namespace {
constexpr std::size_t frame_size = 20;
constexpr std::size_t control_size = 3;

std::array<std::uint8_t, frame_size> make_frame(
    std::uint8_t type, std::uint8_t channel, std::uint8_t sequence,
    int acknowledged_sequence, const std::vector<std::uint8_t> &payload = {})
{
    std::array<std::uint8_t, frame_size> frame{};
    frame[0] = 0x7E;
    frame[1] = static_cast<std::uint8_t>((channel << 4) | payload.size());
    frame[2] = static_cast<std::uint8_t>(type << 5);
    if (sequence != 0)
        frame[2] |= 0x08;
    if (acknowledged_sequence >= 0)
    {
        frame[2] |= 0x04;
        if (acknowledged_sequence != 0)
            frame[2] |= 0x02;
    }
    for (std::size_t index = 0; index < payload.size(); ++index)
        frame[3 + index] = payload[index];
    for (std::size_t index = 1; index < 18; ++index)
        frame[18] ^= frame[index];
    frame[19] = 0xD3;
    return frame;
}

void feed(squid_link_server &link,
          const std::array<std::uint8_t, frame_size> &frame)
{
    for (std::uint8_t byte : frame)
        link.receive_serial_byte(byte);
}

void feed_ack(squid_link_server &link, std::uint8_t sequence)
{
    link.receive_serial_byte(0xE0);
    link.receive_serial_byte(static_cast<std::uint8_t>(
        (3U << 5) | 0x04U | (sequence != 0 ? 0x02U : 0U)));
    link.receive_serial_byte(0xCF);
}

std::array<std::uint8_t, frame_size> take_frame(squid_link_server &link)
{
    std::array<std::uint8_t, frame_size> frame{};
    for (std::uint8_t &byte : frame)
    {
        if (!link.take_serial_byte(byte))
            return {};
    }
    return frame;
}


std::array<std::uint8_t, control_size> take_control(squid_link_server &link)
{
    std::array<std::uint8_t, control_size> control{};
    for (std::uint8_t &byte : control)
    {
        if (!link.take_serial_byte(byte))
            return {};
    }
    return control;
}
}

int main()
{
    int failures = 0;
#define CHECK(condition) do { \
    if (!(condition)) { \
        std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

    squid_link_server link;
    feed(link, make_frame(0, 0, 0, -1));
    const auto hello_ack = take_frame(link);
    CHECK(link.link_up());
    CHECK(hello_ack[0] == 0x7E);
    CHECK((hello_ack[2] >> 5) == 1);

    // The 254-byte libsquid stream capacity includes the packet-length byte.
    // Reject an application payload that PAKET cannot represent.
    std::array<std::uint8_t,
        squid_link_server::maximum_packet_size + 1> oversized_response{};
    CHECK(!link.submit_response(link.session(), oversized_response.data(),
                                oversized_response.size()));

    // A complete one-byte capabilities packet is length-prefixed in the
    // channel-3 byte stream.
    feed(link, make_frame(2, 3, 0, -1, {1, 0}));
    const auto request_ack = take_control(link);
    CHECK(request_ack[0] == 0xE0);
    CHECK((request_ack[1] >> 5) == 3);
    CHECK((request_ack[1] & 0x04) != 0);
    CHECK((request_ack[1] & 0x02) == 0);
    CHECK(request_ack[2] == 0xCF);
    CHECK(link.pending_serial_bytes() == 0);

    squid_link_server::request request;
    CHECK(link.take_request(request));
    CHECK(request.payload == std::vector<std::uint8_t>({0}));

    const std::uint8_t capabilities[] = {0x80, 0, 1, 0x0F, 5, 11, 243};
    CHECK(link.submit_response(request.session, capabilities,
                               sizeof(capabilities)));
    const auto response = take_frame(link);
    CHECK((response[1] >> 4) == 3);
    CHECK((response[1] & 0x0F) == sizeof(capabilities) + 1);
    CHECK(response[3] == sizeof(capabilities));
    for (std::size_t index = 0; index < sizeof(capabilities); ++index)
        CHECK(response[4 + index] == capabilities[index]);

    // Retire the host's response frame, then verify a request larger than one
    // 15-byte Squid payload is reassembled without losing its length byte.
    feed_ack(link, 0);
    std::vector<std::uint8_t> first{20};
    for (std::uint8_t value = 0; value < 14; ++value)
        first.push_back(value);
    feed(link, make_frame(2, 3, 1, -1, first));
    (void)take_frame(link);
    std::vector<std::uint8_t> second;
    for (std::uint8_t value = 14; value < 20; ++value)
        second.push_back(value);
    feed(link, make_frame(2, 3, 0, -1, second));
    (void)take_frame(link);

    CHECK(link.take_request(request));
    CHECK(request.payload.size() == 20);
    for (std::uint8_t value = 0; value < 20; ++value)
        CHECK(request.payload[value] == value);

    // If response DATA is ready when incoming DATA arrives, its CTRL byte
    // carries the ACK and no separate three-byte control block is emitted.
    squid_link_server piggyback_link;
    feed(piggyback_link, make_frame(0, 0, 0, -1));
    (void)take_frame(piggyback_link);
    const std::uint8_t first_response[] = {0x11};
    const std::uint8_t second_response[] = {0x22};
    CHECK(piggyback_link.submit_response(piggyback_link.session(),
                                         first_response,
                                         sizeof(first_response)));
    (void)take_frame(piggyback_link);
    CHECK(piggyback_link.submit_response(piggyback_link.session(),
                                         second_response,
                                         sizeof(second_response)));
    feed(piggyback_link, make_frame(2, 3, 0, 0, {1, 0}));
    const auto piggybacked = take_frame(piggyback_link);
    CHECK((piggybacked[2] >> 5) == 2);
    CHECK((piggybacked[2] & 0x04) != 0);
    CHECK((piggybacked[2] & 0x02) == 0);
    CHECK(piggyback_link.pending_serial_bytes() == 0);

#undef CHECK
    if (failures == 0)
    {
        std::puts("test_squid_link_server: all tests passed");
        return 0;
    }
    std::printf("test_squid_link_server: %d failure(s)\n", failures);
    return 1;
}
