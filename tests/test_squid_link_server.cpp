#include "squid_link_server.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace {
constexpr std::uint8_t etx = 0xd3;
constexpr std::uint8_t data_base = 0xd0;
constexpr std::uint8_t hello = 0xe0;
constexpr std::uint8_t hello_ack = 0xe1;
constexpr std::uint8_t ack0 = 0xe2;
constexpr std::uint8_t ack1 = 0xe3;
constexpr std::uint8_t config = 0xe6;
constexpr std::uint8_t config_ack = 0xe7;

std::uint8_t crc8(const std::uint8_t *data, std::size_t size)
{
    std::uint8_t crc = 0;
    while (size-- != 0)
    {
        crc ^= *data++;
        for (unsigned int bit = 0; bit < 8; ++bit)
            crc = static_cast<std::uint8_t>(
                (crc & 0x80U) != 0 ? (crc << 1U) ^ 0x07U : crc << 1U);
    }
    return crc;
}

std::vector<std::uint8_t> control(std::uint8_t tag)
{
    return {tag, crc8(&tag, 1), etx};
}

std::vector<std::uint8_t> configuration(
    std::uint8_t tag, std::uint8_t payload_bytes)
{
    std::vector<std::uint8_t> bytes{tag, payload_bytes};
    bytes.push_back(crc8(bytes.data(), bytes.size()));
    bytes.push_back(etx);
    return bytes;
}

std::vector<std::uint8_t> data_frame(
    std::uint8_t sequence, int acknowledged_sequence,
    const std::vector<std::uint8_t> &payload)
{
    std::vector<std::uint8_t> frame;
    std::uint8_t tag = static_cast<std::uint8_t>(
        data_base | (sequence != 0 ? 0x04U : 0U));
    if (acknowledged_sequence == 0)
        tag |= 0x01U;
    else if (acknowledged_sequence == 1)
        tag |= 0x02U;
    frame.push_back(tag);
    frame.push_back(static_cast<std::uint8_t>(
        0x30U | (payload.size() == 16 ? 0U : payload.size())));
    frame.insert(frame.end(), payload.begin(), payload.end());
    frame.push_back(crc8(frame.data(), frame.size()));
    frame.push_back(etx);
    return frame;
}

void feed(squid_link_server &link, const std::vector<std::uint8_t> &bytes)
{
    for (std::uint8_t byte : bytes)
        (void)link.receive_serial_byte(byte);
    link.service();
}

std::vector<std::uint8_t> take_all(squid_link_server &link)
{
    std::vector<std::uint8_t> bytes;
    std::uint8_t byte = 0;
    while (link.take_serial_byte(byte))
        bytes.push_back(byte);
    return bytes;
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

    CHECK(squid_link_server::set_default_payload_bytes(64));
    squid_link_server link;
    CHECK(link.can_receive_serial_byte());
    for (std::size_t index = 0;
         index < squid_link_server::serial_input_capacity; ++index)
        CHECK(link.receive_serial_byte(0));
    CHECK(!link.can_receive_serial_byte());
    CHECK(!link.receive_serial_byte(0));
    link.service();
    CHECK(link.can_receive_serial_byte());

    feed(link, configuration(config, 112));
    const auto greeting = take_all(link);
    CHECK(link.link_up());
    CHECK(greeting == configuration(config_ack, 64));

    std::vector<std::uint8_t> oversized(
        squid_link_server::maximum_packet_size + 1U);
    CHECK(!link.submit_response(
        link.session(), oversized.data(), oversized.size()));

    // One-byte capabilities request, preceded by the squid-server stream
    // length. Wire v2 acknowledges it with a compact three-byte ACK0.
    feed(link, data_frame(0, -1, {1, 0}));
    CHECK(take_all(link) == control(ack0));
    squid_link_server::request request;
    CHECK(link.take_request(request));
    CHECK(request.payload == std::vector<std::uint8_t>({0}));

    const std::uint8_t capabilities[] = {0x80, 0, 1, 0x0f, 5, 11, 245};
    CHECK(link.submit_response(request.session, capabilities,
                               sizeof(capabilities)));
    link.service();
    const auto response = take_all(link);
    CHECK(response.size() == sizeof(capabilities) + 5U);
    CHECK((response[0] & 0xf8U) == data_base);
    CHECK((response[1] >> 4U) == 3U);
    CHECK((response[1] & 0x0fU) == sizeof(capabilities) + 1U);
    CHECK(response[2] == sizeof(capabilities));
    for (std::size_t index = 0; index < sizeof(capabilities); ++index)
        CHECK(response[index + 3U] == capabilities[index]);
    CHECK(response[response.size() - 2U] ==
          crc8(response.data(), response.size() - 2U));
    CHECK(response.back() == etx);
    feed(link, control(ack0));
    CHECK(take_all(link).empty());

    // A 255-byte plugin packet is legal in v2. Reassemble a 20-byte request
    // split at the new 16-byte payload boundary.
    std::vector<std::uint8_t> first{20};
    for (std::uint8_t value = 0; value < 15; ++value)
        first.push_back(value);
    feed(link, data_frame(1, -1, first));
    CHECK(take_all(link) == control(ack1));
    std::vector<std::uint8_t> second;
    for (std::uint8_t value = 15; value < 20; ++value)
        second.push_back(value);
    feed(link, data_frame(0, -1, second));
    CHECK(take_all(link) == control(ack0));
    CHECK(link.take_request(request));
    CHECK(request.payload.size() == 20);
    for (std::uint8_t value = 0; value < 20; ++value)
        CHECK(request.payload[value] == value);

    // CRC corruption is rejected, and the parser resynchronizes to the valid
    // frame which immediately follows it in the same receive budget.
    auto damaged = data_frame(1, -1, {1, 0x55});
    damaged[2] ^= 0x80U;
    auto valid = data_frame(1, -1, {1, 0x66});
    damaged.insert(damaged.end(), valid.begin(), valid.end());
    feed(link, damaged);
    CHECK(take_all(link) == control(ack1));
    CHECK(link.take_request(request));
    CHECK(request.payload == std::vector<std::uint8_t>({0x66}));

    const std::uint64_t old_session = link.session();
    feed(link, control(hello));
    CHECK(!link.link_up());
    CHECK(!link.submit_response(old_session, capabilities,
                                sizeof(capabilities)));
    feed(link, control(hello));
    CHECK(link.link_up());
    CHECK(take_all(link) == control(hello_ack));
    CHECK(link.session() != old_session);

#undef CHECK
    if (failures == 0)
    {
        std::puts("test_squid_link_server: all tests passed");
        return 0;
    }
    std::printf("test_squid_link_server: %d failure(s)\n", failures);
    return 1;
}
