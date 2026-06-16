/*
 * Based on ZX0 by Einar Saukas and Urusergi.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of its author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "zx0.hpp"

#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace zx0 {
namespace {

constexpr int initial_offset = 1;
constexpr int max_offset_zx0 = 32640;
constexpr int max_offset_zx7 = 2176;
constexpr int max_scale = 50;
constexpr std::size_t qty_blocks = 10000;

struct block {
    block *chain = nullptr;
    block *ghost_chain = nullptr;
    int bits = 0;
    int index = 0;
    int offset = 0;
    int references = 0;
};

class block_pool {
public:
    block *allocate(int bits, int index, int offset, block *chain)
    {
        block *ptr = nullptr;

        if (ghost_root_ != nullptr) {
            ptr = ghost_root_;
            ghost_root_ = ptr->ghost_chain;
            if ((ptr->chain != nullptr) && (--ptr->chain->references == 0)) {
                ptr->chain->ghost_chain = ghost_root_;
                ghost_root_ = ptr->chain;
            }
        } else {
            if (remaining_ == 0) {
                slabs_.push_back(std::make_unique<block[]>(qty_blocks));
                current_slab_ = slabs_.back().get();
                remaining_ = qty_blocks;
            }
            ptr = &current_slab_[--remaining_];
        }

        ptr->bits = bits;
        ptr->index = index;
        ptr->offset = offset;
        if (chain != nullptr)
            chain->references++;
        ptr->chain = chain;
        ptr->ghost_chain = nullptr;
        ptr->references = 0;
        return ptr;
    }

    void assign(block *&slot, block *chain)
    {
        chain->references++;
        if ((slot != nullptr) && (--slot->references == 0)) {
            slot->ghost_chain = ghost_root_;
            ghost_root_ = slot;
        }
        slot = chain;
    }

private:
    std::vector<std::unique_ptr<block[]>> slabs_{};
    block *current_slab_ = nullptr;
    block *ghost_root_ = nullptr;
    std::size_t remaining_ = 0;
};

class bit_writer {
public:
    bit_writer(std::size_t output_size, int input_size, int skip)
        : data_(output_size, 0),
          diff_(static_cast<int>(output_size) - input_size + skip),
          input_index_(skip)
    {
    }

    void read_bytes(int count)
    {
        input_index_ += count;
        diff_ += count;
        if (delta_ < diff_)
            delta_ = diff_;
    }

    void write_byte(int value)
    {
        data_[output_index_++] = static_cast<std::uint8_t>(value);
        diff_--;
    }

    void write_bit(bool value)
    {
        if (backtrack_) {
            if (value) {
                if (output_index_ == 0)
                    throw std::runtime_error("internal zx0 bitstream error");
                data_[output_index_ - 1] |= 1;
            }
            backtrack_ = false;
            return;
        }

        if (bit_mask_ == 0) {
            bit_mask_ = 0x80;
            bit_index_ = output_index_;
            write_byte(0);
        }

        if (value)
            data_[bit_index_] |= static_cast<std::uint8_t>(bit_mask_);
        bit_mask_ >>= 1;
    }

    void write_interlaced_elias_gamma(int value, bool backwards_mode, bool invert_mode)
    {
        int bit = 2;
        while (bit <= value)
            bit <<= 1;
        bit >>= 1;

        while ((bit >>= 1) != 0) {
            write_bit(backwards_mode);
            write_bit(invert_mode ? ((value & bit) == 0) : ((value & bit) != 0));
        }
        write_bit(!backwards_mode);
    }

    void enable_backtrack()
    {
        backtrack_ = true;
    }

    [[nodiscard]] int delta() const
    {
        return delta_;
    }

    [[nodiscard]] std::vector<std::uint8_t> take_data()
    {
        return std::move(data_);
    }

private:
    std::vector<std::uint8_t> data_{};
    int output_index_ = 0;
    int diff_ = 0;
    int input_index_ = 0;
    int bit_index_ = 0;
    int bit_mask_ = 0;
    int delta_ = 0;
    bool backtrack_ = true;
};

int narrow_size(std::size_t value, const char *what)
{
    if (value > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error(std::string(what) + " is too large for zx0");
    return static_cast<int>(value);
}

int offset_ceiling(int index, int offset_limit)
{
    if (index > offset_limit)
        return offset_limit;
    if (index < initial_offset)
        return initial_offset;
    return index;
}

int elias_gamma_bits(int value)
{
    int bits = 1;
    while ((value >>= 1) != 0)
        bits += 2;
    return bits;
}

void reverse_range(std::vector<std::uint8_t> &data)
{
    std::reverse(data.begin(), data.end());
}

block *optimize(const std::vector<std::uint8_t> &input,
                int skip,
                int offset_limit,
                bool show_progress,
                block_pool &pool)
{
    const int input_size = narrow_size(input.size(), "input");
    const int max_offset = offset_ceiling(input_size - 1, offset_limit);

    std::vector<block *> last_literal(static_cast<std::size_t>(max_offset + 1), nullptr);
    std::vector<block *> last_match(static_cast<std::size_t>(max_offset + 1), nullptr);
    std::vector<block *> optimal(static_cast<std::size_t>(input_size), nullptr);
    std::vector<int> match_length(static_cast<std::size_t>(max_offset + 1), 0);
    std::vector<int> best_length(static_cast<std::size_t>(input_size), 0);

    if (input_size > 2)
        best_length[2] = 2;

    pool.assign(last_match[static_cast<std::size_t>(initial_offset)],
                pool.allocate(-1, skip - 1, initial_offset, nullptr));

    int dots = 2;
    if (show_progress) {
        std::printf("[");
        std::fflush(stdout);
    }

    for (int index = skip; index < input_size; index++) {
        int best_length_size = 2;
        const int current_max_offset = offset_ceiling(index, offset_limit);

        for (int offset = 1; offset <= current_max_offset; offset++) {
            const std::size_t slot = static_cast<std::size_t>(offset);
            if ((index != skip) &&
                (index >= offset) &&
                (input[static_cast<std::size_t>(index)] ==
                 input[static_cast<std::size_t>(index - offset)]))
            {
                if (last_literal[slot] != nullptr) {
                    const int length = index - last_literal[slot]->index;
                    const int bits = last_literal[slot]->bits + 1 + elias_gamma_bits(length);
                    pool.assign(last_match[slot],
                                pool.allocate(bits, index, offset, last_literal[slot]));
                    if ((optimal[static_cast<std::size_t>(index)] == nullptr) ||
                        (optimal[static_cast<std::size_t>(index)]->bits > bits))
                    {
                        pool.assign(optimal[static_cast<std::size_t>(index)], last_match[slot]);
                    }
                }

                if (++match_length[slot] > 1) {
                    if (best_length_size < match_length[slot]) {
                        int bits = optimal[static_cast<std::size_t>(index - best_length[best_length_size])]->bits +
                                   elias_gamma_bits(best_length[best_length_size] - 1);
                        do {
                            best_length_size++;
                            const int bits2 =
                                optimal[static_cast<std::size_t>(index - best_length_size)]->bits +
                                elias_gamma_bits(best_length_size - 1);
                            if (bits2 <= bits) {
                                best_length[static_cast<std::size_t>(best_length_size)] =
                                    best_length_size;
                                bits = bits2;
                            } else {
                                best_length[static_cast<std::size_t>(best_length_size)] =
                                    best_length[static_cast<std::size_t>(best_length_size - 1)];
                            }
                        } while (best_length_size < match_length[slot]);
                    }

                    const int length = best_length[static_cast<std::size_t>(match_length[slot])];
                    const int bits =
                        optimal[static_cast<std::size_t>(index - length)]->bits +
                        8 +
                        elias_gamma_bits((offset - 1) / 128 + 1) +
                        elias_gamma_bits(length - 1);

                    if ((last_match[slot] == nullptr) ||
                        (last_match[slot]->index != index) ||
                        (last_match[slot]->bits > bits))
                    {
                        pool.assign(last_match[slot],
                                    pool.allocate(bits,
                                                  index,
                                                  offset,
                                                  optimal[static_cast<std::size_t>(index - length)]));
                        if ((optimal[static_cast<std::size_t>(index)] == nullptr) ||
                            (optimal[static_cast<std::size_t>(index)]->bits > bits))
                        {
                            pool.assign(optimal[static_cast<std::size_t>(index)], last_match[slot]);
                        }
                    }
                }
            } else {
                match_length[slot] = 0;
                if (last_match[slot] != nullptr) {
                    const int length = index - last_match[slot]->index;
                    const int bits = last_match[slot]->bits +
                                     1 +
                                     elias_gamma_bits(length) +
                                     length * 8;
                    pool.assign(last_literal[slot],
                                pool.allocate(bits, index, 0, last_match[slot]));
                    if ((optimal[static_cast<std::size_t>(index)] == nullptr) ||
                        (optimal[static_cast<std::size_t>(index)]->bits > bits))
                    {
                        pool.assign(optimal[static_cast<std::size_t>(index)], last_literal[slot]);
                    }
                }
            }
        }

        if (show_progress && ((index * max_scale) / input_size > dots)) {
            std::printf(".");
            std::fflush(stdout);
            dots++;
        }
    }

    if (show_progress)
        std::printf("]\n");

    return optimal.back();
}

compression_result build_output(block *optimal,
                                const std::vector<std::uint8_t> &input,
                                const options &options)
{
    std::vector<block *> sequence;
    for (block *current = optimal; current != nullptr; current = current->chain)
        sequence.push_back(current);
    std::reverse(sequence.begin(), sequence.end());

    if (sequence.size() < 2)
        throw std::runtime_error("internal zx0 chain error");

    const bool invert_mode = !options.classic && !options.backwards;
    int last_offset = initial_offset;
    bit_writer writer(static_cast<std::size_t>((optimal->bits + 25) / 8),
                      narrow_size(input.size(), "input"),
                      options.skip);

    for (std::size_t index = 1; index < sequence.size(); index++) {
        block *prev = sequence[index - 1];
        block *current = sequence[index];
        const int length = current->index - prev->index;

        if (current->offset == 0) {
            writer.write_bit(false);
            writer.write_interlaced_elias_gamma(length, options.backwards, false);
            for (int i = 0; i < length; i++) {
                writer.write_byte(input[static_cast<std::size_t>(prev->index + 1 + i)]);
                writer.read_bytes(1);
            }
        } else if (current->offset == last_offset) {
            writer.write_bit(false);
            writer.write_interlaced_elias_gamma(length, options.backwards, false);
            writer.read_bytes(length);
        } else {
            writer.write_bit(true);
            writer.write_interlaced_elias_gamma((current->offset - 1) / 128 + 1,
                                                options.backwards,
                                                invert_mode);
            if (options.backwards)
                writer.write_byte(((current->offset - 1) % 128) << 1);
            else
                writer.write_byte((127 - ((current->offset - 1) % 128)) << 1);

            writer.enable_backtrack();
            writer.write_interlaced_elias_gamma(length - 1, options.backwards, false);
            writer.read_bytes(length);
            last_offset = current->offset;
        }
    }

    writer.write_bit(true);
    writer.write_interlaced_elias_gamma(256, options.backwards, invert_mode);

    compression_result result;
    result.data = writer.take_data();
    result.delta = writer.delta();
    return result;
}

} // namespace

compression_result compress(std::span<const std::uint8_t> input,
                            const options &options,
                            bool show_progress)
{
    if (input.empty())
        throw std::runtime_error("empty input");

    const int input_size = narrow_size(input.size(), "input");
    if (options.skip < 0)
        throw std::runtime_error("skip must be non-negative");
    if (options.skip >= input_size)
        throw std::runtime_error("skip reaches the end of input");

    std::vector<std::uint8_t> working(input.begin(), input.end());
    if (options.backwards)
        reverse_range(working);

    block_pool pool;
    block *optimal = optimize(working,
                              options.skip,
                              options.quick ? max_offset_zx7 : max_offset_zx0,
                              show_progress,
                              pool);
    if (optimal == nullptr)
        throw std::runtime_error("compression failed");

    compression_result result = build_output(optimal, working, options);
    if (options.backwards)
        reverse_range(result.data);
    return result;
}

} // namespace zx0
