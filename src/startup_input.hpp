#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class startup_input
{
public:
    using clock = std::chrono::steady_clock;

    startup_input(std::vector<uint8_t> keys,
                  std::chrono::milliseconds initial_delay,
                  std::chrono::milliseconds key_interval,
                  std::chrono::milliseconds enter_delay);

    static bool decode(std::string_view text,
                       std::vector<uint8_t> &keys,
                       std::string &error);

    void start(clock::time_point now);
    std::optional<uint8_t> take_due(clock::time_point now);

    bool empty() const { return keys_.empty(); }
    bool finished() const { return next_key_ >= keys_.size(); }
    std::size_t size() const { return keys_.size(); }

private:
    std::vector<uint8_t> keys_;
    std::chrono::milliseconds initial_delay_{};
    std::chrono::milliseconds key_interval_{};
    std::chrono::milliseconds enter_delay_{};
    clock::time_point next_due_{};
    std::size_t next_key_ = 0;
    bool started_ = false;
};
