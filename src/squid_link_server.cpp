#include "squid_link_server.hpp"

#include <algorithm>

namespace {
constexpr std::uint8_t control_type_mask = 0xE0;
constexpr std::uint8_t control_type_shift = 5;
constexpr std::uint8_t control_status_mask = 0x10;
constexpr std::uint8_t control_sequence_mask = 0x08;
constexpr std::uint8_t control_ack_mask = 0x04;
constexpr std::uint8_t control_ack_sequence_mask = 0x02;
constexpr auto retry_timeout = std::chrono::seconds(4);
constexpr unsigned int maximum_retries = 5;
}

squid_link_server::squid_link_server()
{
    reset();
}

void squid_link_server::reset()
{
    rx_frame_.fill(0);
    rx_position_ = 0;
    serial_tx_.clear();
    link_up_ = false;
    ++session_;
    receive_sequence_ = 0;
    transmit_sequence_ = 0;
    pending_ack_sequence_ = -1;
    waiting_for_ack_ = false;
    retry_count_ = 0;
    retry_frame_.fill(0);
    expected_request_size_ = 0;
    request_bytes_.clear();
    completed_requests_.clear();
    queued_responses_.clear();
    response_stream_.clear();
}

std::uint8_t squid_link_server::hash_frame(
    const std::array<std::uint8_t, frame_size_> &frame)
{
    std::uint8_t result = 0;
    for (std::size_t index = 1; index < frame_size_ - 2; ++index)
        result ^= frame[index];
    return result;
}

void squid_link_server::receive_serial_byte(std::uint8_t byte)
{
    if (rx_position_ == 0)
    {
        if (byte == stx_ || byte == control_stx_)
            rx_frame_[rx_position_++] = byte;
        return;
    }

    rx_frame_[rx_position_++] = byte;

    if (rx_frame_[0] == control_stx_)
    {
        if (rx_position_ < control_size_)
            return;
        if (rx_frame_[2] == control_etx_)
        {
            const std::uint8_t control = rx_frame_[1];
            rx_position_ = 0;
            handle_control(control);
            return;
        }
        resync_receive(control_size_);
        return;
    }

    if (rx_position_ < frame_size_)
        return;

    if (rx_frame_[frame_size_ - 1] == etx_ &&
        rx_frame_[frame_size_ - 2] == hash_frame(rx_frame_))
    {
        rx_position_ = 0;
        handle_frame();
        return;
    }

    resync_receive(frame_size_);
}

void squid_link_server::resync_receive(std::size_t received)
{
    // A valid start byte may be embedded in a damaged frame/control block.
    // Preserve the suffix so one bad item does not consume the next one.
    std::size_t start = 1;
    while (start < received && rx_frame_[start] != stx_ &&
           rx_frame_[start] != control_stx_)
        ++start;
    if (start == received)
    {
        rx_position_ = 0;
        return;
    }
    rx_position_ = received - start;
    std::move(rx_frame_.begin() + static_cast<std::ptrdiff_t>(start),
              rx_frame_.begin() + static_cast<std::ptrdiff_t>(received),
              rx_frame_.begin());
}

void squid_link_server::begin_session()
{
    serial_tx_.clear();
    link_up_ = true;
    ++session_;
    receive_sequence_ = 0;
    transmit_sequence_ = 0;
    pending_ack_sequence_ = -1;
    waiting_for_ack_ = false;
    retry_count_ = 0;
    expected_request_size_ = 0;
    request_bytes_.clear();
    completed_requests_.clear();
    queued_responses_.clear();
    response_stream_.clear();
}

void squid_link_server::accept_ack(std::uint8_t control)
{
    if (!waiting_for_ack_ || (control & control_ack_mask) == 0 ||
        (control & control_status_mask) != 0)
        return;

    const std::uint8_t acknowledged =
        (control & control_ack_sequence_mask) != 0 ? 1 : 0;
    if (acknowledged == transmit_sequence_)
    {
        waiting_for_ack_ = false;
        transmit_sequence_ ^= 1;
        retry_count_ = 0;
    }
}

void squid_link_server::handle_control(std::uint8_t control)
{
    const auto type = static_cast<frame_type>(
        (control & control_type_mask) >> control_type_shift);
    if (!link_up_ || type != frame_type::ack)
        return;
    accept_ack(control);
    pump_response();
}

void squid_link_server::handle_frame()
{
    const std::uint8_t channel_length = rx_frame_[1];
    const std::uint8_t control = rx_frame_[2];
    const auto type = static_cast<frame_type>(
        (control & control_type_mask) >> control_type_shift);
    const std::uint8_t channel = channel_length >> 4;
    const std::size_t payload_size = channel_length & 0x0F;
    const std::uint8_t sequence =
        (control & control_sequence_mask) != 0 ? 1 : 0;

    if (type == frame_type::hello)
    {
        begin_session();
        queue_frame(frame_type::hello_ack, 0, 0, -1, nullptr, 0);
        return;
    }
    if (type == frame_type::hello_ack)
    {
        if (!link_up_)
            begin_session();
        return;
    }
    if (!link_up_)
        return;

    accept_ack(control);

    if (type == frame_type::data)
    {
        if (sequence == receive_sequence_)
        {
            if (channel == retro_vault_channel_ && payload_size > 0)
                consume_request_bytes(rx_frame_.data() + 3, payload_size);
            receive_sequence_ ^= 1;
        }
        // A duplicate means our previous ACK was lost, so acknowledge both
        // accepted and duplicate DATA frames. Give an available response
        // DATA frame the first chance to carry the acknowledgement.
        pending_ack_sequence_ = sequence;
        pump_response();
        if (pending_ack_sequence_ >= 0)
        {
            queue_ack(pending_ack_sequence_);
            pending_ack_sequence_ = -1;
        }
        return;
    }
    else if (type == frame_type::ping)
    {
        queue_ack(-1);
    }

    pump_response();
}

void squid_link_server::consume_request_bytes(const std::uint8_t *data,
                                               std::size_t size)
{
    for (std::size_t index = 0; index < size; ++index)
    {
        const std::uint8_t byte = data[index];
        if (expected_request_size_ == 0)
        {
            if (byte == 0 || byte > maximum_packet_size)
            {
                request_bytes_.clear();
                continue;
            }
            expected_request_size_ = byte;
            request_bytes_.clear();
            request_bytes_.reserve(expected_request_size_);
            continue;
        }

        request_bytes_.push_back(byte);
        if (request_bytes_.size() == expected_request_size_)
        {
            completed_requests_.push_back({session_, std::move(request_bytes_)});
            request_bytes_.clear();
            expected_request_size_ = 0;
        }
    }
}

void squid_link_server::queue_frame(frame_type type, std::uint8_t channel,
                                    std::uint8_t sequence,
                                    int acknowledged_sequence,
                                    const std::uint8_t *payload,
                                    std::size_t payload_size,
                                    bool remember_for_retry)
{
    std::array<std::uint8_t, frame_size_> frame{};
    payload_size = std::min(payload_size, frame_payload_size_);
    frame[0] = stx_;
    frame[1] = static_cast<std::uint8_t>((channel << 4) | payload_size);
    frame[2] = static_cast<std::uint8_t>(static_cast<std::uint8_t>(type) << 5);
    if (sequence != 0)
        frame[2] |= control_sequence_mask;
    if (acknowledged_sequence >= 0)
    {
        frame[2] |= control_ack_mask;
        if (acknowledged_sequence != 0)
            frame[2] |= control_ack_sequence_mask;
    }
    if (payload != nullptr && payload_size > 0)
        std::copy_n(payload, payload_size, frame.begin() + 3);
    frame[frame_size_ - 2] = hash_frame(frame);
    frame[frame_size_ - 1] = etx_;
    serial_tx_.insert(serial_tx_.end(), frame.begin(), frame.end());

    if (remember_for_retry)
    {
        retry_frame_ = frame;
        waiting_for_ack_ = true;
        retry_count_ = 0;
        retry_started_ = std::chrono::steady_clock::now();
    }
}

void squid_link_server::queue_ack(int sequence)
{
    std::uint8_t control = static_cast<std::uint8_t>(
        static_cast<std::uint8_t>(frame_type::ack) << control_type_shift);
    if (sequence >= 0)
    {
        control |= control_ack_mask;
        if (sequence != 0)
            control |= control_ack_sequence_mask;
    }
    serial_tx_.push_back(control_stx_);
    serial_tx_.push_back(control);
    serial_tx_.push_back(control_etx_);
}

void squid_link_server::pump_response()
{
    if (!link_up_ || waiting_for_ack_)
        return;

    while (response_stream_.empty() && !queued_responses_.empty())
    {
        queued_response response = std::move(queued_responses_.front());
        queued_responses_.pop_front();
        if (response.session != session_)
            continue;
        response_stream_.insert(response_stream_.end(),
                                response.bytes.begin(), response.bytes.end());
    }
    if (response_stream_.empty())
        return;

    std::array<std::uint8_t, frame_payload_size_> payload{};
    const std::size_t size = std::min(response_stream_.size(), payload.size());
    for (std::size_t index = 0; index < size; ++index)
    {
        payload[index] = response_stream_.front();
        response_stream_.pop_front();
    }
    queue_frame(frame_type::data, retro_vault_channel_, transmit_sequence_,
                pending_ack_sequence_, payload.data(), size, true);
    pending_ack_sequence_ = -1;
}

void squid_link_server::service()
{
    if (waiting_for_ack_ && serial_tx_.empty() &&
        std::chrono::steady_clock::now() - retry_started_ >= retry_timeout)
    {
        if (++retry_count_ > maximum_retries)
        {
            reset();
            return;
        }
        serial_tx_.insert(serial_tx_.end(), retry_frame_.begin(), retry_frame_.end());
        retry_started_ = std::chrono::steady_clock::now();
    }
    pump_response();
}

bool squid_link_server::take_serial_byte(std::uint8_t &byte)
{
    if (serial_tx_.empty())
        return false;
    byte = serial_tx_.front();
    serial_tx_.pop_front();
    return true;
}

bool squid_link_server::take_request(request &value)
{
    if (completed_requests_.empty())
        return false;
    value = std::move(completed_requests_.front());
    completed_requests_.pop_front();
    return true;
}

bool squid_link_server::submit_response(std::uint64_t session,
                                        const std::uint8_t *data,
                                        std::size_t size)
{
    if (session != session_ || data == nullptr || size == 0 ||
        size > maximum_packet_size)
        return false;

    queued_response response;
    response.session = session;
    response.bytes.reserve(size + 1);
    response.bytes.push_back(static_cast<std::uint8_t>(size));
    response.bytes.insert(response.bytes.end(), data, data + size);
    queued_responses_.push_back(std::move(response));
    pump_response();
    return true;
}
