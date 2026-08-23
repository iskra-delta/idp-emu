#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

// Host side of the small reliable serial protocol used by libsquid.  The
// wire format is deliberately implemented here instead of starting an
// external server process, so it can run on every emulator platform and on
// whichever free Partner SIO channel the user selects.
class squid_link_server
{
public:
    // A libsquid socket ring carries 254 bytes. The internal packet stream
    // uses one of them for its length prefix, leaving 253 application bytes,
    // exactly matching PAKET's RV_PACKET_MAX.
    static constexpr std::size_t maximum_packet_size = 253;

    struct request
    {
        std::uint64_t session = 0;
        std::vector<std::uint8_t> payload;
    };

    squid_link_server();

    void reset();
    void receive_serial_byte(std::uint8_t byte);
    void service();

    bool take_serial_byte(std::uint8_t &byte);
    bool take_request(request &value);
    bool submit_response(std::uint64_t session,
                         const std::uint8_t *data,
                         std::size_t size);

    bool link_up() const { return link_up_; }
    std::uint64_t session() const { return session_; }
    std::size_t pending_serial_bytes() const { return serial_tx_.size(); }

private:
    static constexpr std::size_t frame_size_ = 20;
    static constexpr std::size_t frame_payload_size_ = 15;
    static constexpr std::size_t control_size_ = 3;
    static constexpr std::uint8_t stx_ = 0x7E;
    static constexpr std::uint8_t etx_ = 0xD3;
    static constexpr std::uint8_t control_stx_ = 0xE0;
    static constexpr std::uint8_t control_etx_ = 0xCF;
    static constexpr std::uint8_t retro_vault_channel_ = 3;

    enum class frame_type : std::uint8_t
    {
        hello = 0,
        hello_ack = 1,
        data = 2,
        ack = 3,
        ping = 4
    };

    struct queued_response
    {
        std::uint64_t session = 0;
        std::vector<std::uint8_t> bytes;
    };

    void handle_frame();
    void handle_control(std::uint8_t control);
    void accept_ack(std::uint8_t control);
    void resync_receive(std::size_t received);
    void begin_session();
    void consume_request_bytes(const std::uint8_t *data, std::size_t size);
    void queue_frame(frame_type type, std::uint8_t channel,
                     std::uint8_t sequence, int acknowledged_sequence,
                     const std::uint8_t *payload, std::size_t payload_size,
                     bool remember_for_retry = false);
    void queue_ack(int sequence);
    void pump_response();
    static std::uint8_t hash_frame(const std::array<std::uint8_t, frame_size_> &frame);

    std::array<std::uint8_t, frame_size_> rx_frame_{};
    std::size_t rx_position_ = 0;
    std::deque<std::uint8_t> serial_tx_;

    bool link_up_ = false;
    std::uint64_t session_ = 0;
    std::uint8_t receive_sequence_ = 0;
    std::uint8_t transmit_sequence_ = 0;
    int pending_ack_sequence_ = -1;
    bool waiting_for_ack_ = false;
    unsigned int retry_count_ = 0;
    std::array<std::uint8_t, frame_size_> retry_frame_{};
    std::chrono::steady_clock::time_point retry_started_{};

    std::size_t expected_request_size_ = 0;
    std::vector<std::uint8_t> request_bytes_;
    std::deque<request> completed_requests_;

    std::deque<queued_response> queued_responses_;
    std::deque<std::uint8_t> response_stream_;
};
