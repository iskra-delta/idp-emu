/*
 * Based on ZX0 by Einar Saukas and Urusergi.
 * This C++ port keeps the reference bitstream format.
 */

#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace zx0 {

struct options {
    int skip = 0;
    bool classic = false;
    bool backwards = false;
    bool quick = false;
};

struct compression_result {
    std::vector<std::uint8_t> data;
    int delta = 0;
};

compression_result compress(std::span<const std::uint8_t> input,
                            const options &options,
                            bool show_progress = true);

} // namespace zx0
