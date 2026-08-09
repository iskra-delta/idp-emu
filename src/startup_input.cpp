#include "startup_input.hpp"

#include <utility>

namespace {
int hex_value(char value)
{
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}
}

startup_input::startup_input(std::vector<uint8_t> keys,
                             std::chrono::milliseconds initial_delay,
                             std::chrono::milliseconds key_interval)
    : keys_(std::move(keys)),
      initial_delay_(initial_delay),
      key_interval_(key_interval)
{
}

bool startup_input::decode(std::string_view text,
                           std::vector<uint8_t> &keys,
                           std::string &error)
{
    std::vector<uint8_t> decoded;
    decoded.reserve(text.size());

    for (std::size_t index = 0; index < text.size(); ++index)
    {
        const unsigned char value = static_cast<unsigned char>(text[index]);
        if (value == '\r')
        {
            decoded.push_back(0x0D);
            if (index + 1 < text.size() && text[index + 1] == '\n')
                ++index;
            continue;
        }
        if (value == '\n')
        {
            decoded.push_back(0x0D);
            continue;
        }
        if (value != '\\')
        {
            decoded.push_back(value);
            continue;
        }

        if (++index >= text.size())
        {
            error = "trailing backslash in startup commands";
            return false;
        }

        const char escape = text[index];
        switch (escape)
        {
        case 'n':
        case 'r':
            decoded.push_back(0x0D);
            break;
        case 't':
            decoded.push_back(0x09);
            break;
        case 'b':
            decoded.push_back(0x08);
            break;
        case 'e':
            decoded.push_back(0x1B);
            break;
        case '\\':
            decoded.push_back('\\');
            break;
        case '\"':
            decoded.push_back('\"');
            break;
        case '\'':
            decoded.push_back('\'');
            break;
        case 'x':
        {
            if (index + 2 >= text.size())
            {
                error = "\\x in startup commands requires two hexadecimal digits";
                return false;
            }
            const int high = hex_value(text[index + 1]);
            const int low = hex_value(text[index + 2]);
            if (high < 0 || low < 0)
            {
                error = "invalid hexadecimal escape in startup commands";
                return false;
            }
            decoded.push_back(static_cast<uint8_t>((high << 4) | low));
            index += 2;
            break;
        }
        default:
            error = "unknown escape \\";
            error.push_back(escape);
            error += " in startup commands";
            return false;
        }
    }

    keys.insert(keys.end(), decoded.begin(), decoded.end());
    error.clear();
    return true;
}

void startup_input::start(clock::time_point now)
{
    next_key_ = 0;
    next_due_ = now + initial_delay_;
    started_ = true;
}

std::optional<uint8_t> startup_input::take_due(clock::time_point now)
{
    if (!started_ || finished() || now < next_due_)
        return std::nullopt;

    const uint8_t key = keys_[next_key_++];
    next_due_ = now + key_interval_;
    return key;
}
