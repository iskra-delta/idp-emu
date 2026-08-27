#include "squid_link_server.hpp"

extern "C" {
#include "squid/snet.h"
#include "squid/socket.h"
}

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <cstdio>
#include <stdexcept>
#include <utility>

namespace {
constexpr std::uint8_t retro_vault_channel = 3;
constexpr std::uint16_t socket_capacity = 512;
constexpr auto protocol_tick = std::chrono::milliseconds(20);
std::uint8_t configured_payload_bytes =
    squid_link_server::default_payload_bytes;
}

struct squid_link_server::implementation
{
    struct queued_response
    {
        std::uint64_t session = 0;
        std::vector<std::uint8_t> bytes;
    };

    static implementation *active;

    squid_platform_t platform{};
    squid_timing_t timing{};
    int socket_fd = -1;
    std::deque<std::uint8_t> serial_rx;
    std::deque<std::uint8_t> serial_tx;
    std::deque<request> completed_requests;
    std::deque<queued_response> queued_responses;
    std::vector<std::uint8_t> request_bytes;
    std::size_t expected_request_size = 0;
    std::uint64_t session_id = 0;
    bool was_link_up = false;
    std::chrono::steady_clock::time_point clock_origin{};

    implementation()
    {
        if (active != nullptr)
            throw std::runtime_error("only one internal Squid endpoint is supported");
        active = this;
        reset();
    }

    ~implementation()
    {
        if (active == this)
        {
            snet_init(nullptr, nullptr);
            active = nullptr;
        }
    }

    static int send_character(std::uint8_t byte)
    {
        if (active == nullptr)
            return -1;
        active->serial_tx.push_back(byte);
        return 0;
    }

    static int receive_character()
    {
        if (active == nullptr || active->serial_rx.empty())
            return -1;
        const std::uint8_t byte = active->serial_rx.front();
        active->serial_rx.pop_front();
        return byte;
    }

    static std::uint8_t get_tick()
    {
        if (active == nullptr)
            return 0;
        const auto elapsed = std::chrono::steady_clock::now() -
            active->clock_origin;
        return static_cast<std::uint8_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(elapsed) /
            protocol_tick);
    }

    void reset()
    {
        serial_rx.clear();
        serial_tx.clear();
        completed_requests.clear();
        queued_responses.clear();
        request_bytes.clear();
        expected_request_size = 0;
        was_link_up = false;
        ++session_id;
        clock_origin = std::chrono::steady_clock::now();

        platform.send_char = send_character;
        platform.recv_char = receive_character;
        platform.get_tick = get_tick;
        platform.mem_alloc = std::malloc;
        platform.mem_free = std::free;
        timing.timeout_ticks = 100;
        timing.ack_delay_ticks = 0;
        timing.ping_ticks = 0;
        timing.max_retries = 5;
#if defined(SQUID_PAYLOAD_MAX)
        timing.payload_bytes = configured_payload_bytes;
#endif
        snet_init(&platform, &timing);
        socket_fd = squid_open(socket_capacity, socket_capacity);
        if (socket_fd < 0 || squid_bind(socket_fd, retro_vault_channel) != 0)
            throw std::runtime_error("could not initialize internal Squid socket");
    }

    void observe_session()
    {
        const bool up = snet_link_is_up();
        if (up && !was_link_up)
        {
            if (const char *trace = std::getenv("IDP_TRACE_SQUID");
                trace != nullptr && trace[0] != '\0' && trace[0] != '0')
                std::fprintf(stderr, "[squid] link up, payload=%u\n",
#if defined(SQUID_PAYLOAD_MAX)
                             static_cast<unsigned int>(snet_payload_bytes()));
#else
                             static_cast<unsigned int>(
                                 squid_link_server::minimum_payload_bytes));
#endif
            ++session_id;
            completed_requests.clear();
            queued_responses.clear();
            request_bytes.clear();
            expected_request_size = 0;
        }
        else if (!up && was_link_up)
        {
            ++session_id;
            completed_requests.clear();
            queued_responses.clear();
            request_bytes.clear();
            expected_request_size = 0;
        }
        was_link_up = up;
    }

    void consume_request_bytes(const std::uint8_t *data, std::size_t size)
    {
        for (std::size_t index = 0; index < size; ++index)
        {
            const std::uint8_t byte = data[index];
            if (expected_request_size == 0)
            {
                if (byte == 0)
                {
                    request_bytes.clear();
                    continue;
                }
                expected_request_size = byte;
                request_bytes.clear();
                request_bytes.reserve(expected_request_size);
                continue;
            }
            request_bytes.push_back(byte);
            if (request_bytes.size() == expected_request_size)
            {
                completed_requests.push_back(
                    {session_id, std::move(request_bytes)});
                request_bytes.clear();
                expected_request_size = 0;
            }
        }
    }

    void drain_socket()
    {
        std::array<std::uint8_t, socket_capacity> bytes{};
        for (;;)
        {
            const int pending = squid_rx_pending(socket_fd);
            if (pending <= 0)
                break;
            const auto count = static_cast<std::uint16_t>(std::min<int>(
                pending, static_cast<int>(bytes.size())));
            const int received = squid_recv(socket_fd, bytes.data(), count);
            if (received <= 0)
                break;
            consume_request_bytes(bytes.data(), static_cast<std::size_t>(received));
        }
    }

    void pump_responses()
    {
        while (!queued_responses.empty())
        {
            const auto &response = queued_responses.front();
            if (response.session != session_id)
            {
                queued_responses.pop_front();
                continue;
            }
            const int sent = squid_send(
                socket_fd, response.bytes.data(),
                static_cast<std::uint16_t>(response.bytes.size()));
            if (sent < 0)
                break;
            queued_responses.pop_front();
        }
    }

    void service()
    {
        snet_burst();
        observe_session();
        if (!snet_link_is_up())
            return;
        drain_socket();
        pump_responses();
        // Give newly queued response bytes an immediate transmit opportunity.
        snet_burst();
        observe_session();
    }
};

squid_link_server::implementation *squid_link_server::implementation::active =
    nullptr;

squid_link_server::squid_link_server()
    : implementation_(std::make_unique<implementation>())
{
}

squid_link_server::~squid_link_server() = default;

bool squid_link_server::set_default_payload_bytes(std::uint8_t bytes)
{
    if (bytes < minimum_payload_bytes || bytes > maximum_payload_bytes)
        return false;
    configured_payload_bytes = bytes;
    return true;
}

void squid_link_server::reset()
{
    implementation_->reset();
}

bool squid_link_server::can_receive_serial_byte() const
{
    return implementation_->serial_rx.size() < serial_input_capacity;
}

bool squid_link_server::receive_serial_byte(std::uint8_t byte)
{
    if (!can_receive_serial_byte())
        return false;
    implementation_->serial_rx.push_back(byte);
    return true;
}

void squid_link_server::service()
{
    implementation_->service();
}

bool squid_link_server::take_serial_byte(std::uint8_t &byte)
{
    if (implementation_->serial_tx.empty())
        return false;
    byte = implementation_->serial_tx.front();
    implementation_->serial_tx.pop_front();
    return true;
}

bool squid_link_server::take_request(request &value)
{
    if (implementation_->completed_requests.empty())
        return false;
    value = std::move(implementation_->completed_requests.front());
    implementation_->completed_requests.pop_front();
    return true;
}

bool squid_link_server::submit_response(std::uint64_t session,
                                        const std::uint8_t *data,
                                        std::size_t size)
{
    if (session != implementation_->session_id || data == nullptr ||
        size == 0 || size > maximum_packet_size)
        return false;
    implementation::queued_response response;
    response.session = session;
    response.bytes.reserve(size + 1);
    response.bytes.push_back(static_cast<std::uint8_t>(size));
    response.bytes.insert(response.bytes.end(), data, data + size);
    implementation_->queued_responses.push_back(std::move(response));
    implementation_->pump_responses();
    return true;
}

bool squid_link_server::link_up() const
{
    return snet_link_is_up();
}

std::uint64_t squid_link_server::session() const
{
    return implementation_->session_id;
}

std::size_t squid_link_server::pending_serial_bytes() const
{
    return implementation_->serial_tx.size();
}
