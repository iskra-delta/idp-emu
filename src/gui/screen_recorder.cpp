#include "screen_recorder.hpp"

#include "display.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBI_WRITE_NO_STDIO
#include "../../third_party/stb/stb_image_write.h"

namespace {
bool write_bytes(FILE *file, const void *data, size_t size)
{
    return std::fwrite(data, 1, size, file) == size;
}

bool write_fourcc(FILE *file, const char value[5])
{
    return write_bytes(file, value, 4);
}

bool write_u16(FILE *file, uint16_t value)
{
    const uint8_t bytes[] = {
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8),
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

bool write_u32(FILE *file, uint32_t value)
{
    const uint8_t bytes[] = {
        static_cast<uint8_t>(value),
        static_cast<uint8_t>(value >> 8),
        static_cast<uint8_t>(value >> 16),
        static_cast<uint8_t>(value >> 24),
    };
    return write_bytes(file, bytes, sizeof(bytes));
}

int64_t file_position(FILE *file)
{
#if defined(_WIN32)
    return _ftelli64(file);
#else
    return ftello(file);
#endif
}

bool seek_file(FILE *file, int64_t position)
{
#if defined(_WIN32)
    return _fseeki64(file, position, SEEK_SET) == 0;
#else
    return fseeko(file, position, SEEK_SET) == 0;
#endif
}

bool patch_u32(FILE *file, int64_t position, uint32_t value)
{
    const int64_t restore = file_position(file);
    return restore >= 0 && seek_file(file, position) && write_u32(file, value) &&
           seek_file(file, restore);
}

void append_jpeg_bytes(void *context, void *data, int size)
{
    auto &output = *static_cast<std::vector<uint8_t> *>(context);
    const auto *bytes = static_cast<const uint8_t *>(data);
    output.insert(output.end(), bytes, bytes + size);
}

std::string file_error(const char *action)
{
    if (errno != 0)
        return std::string(action) + ": " + std::strerror(errno);
    return std::string(action) + ": I/O error.";
}
} // namespace

screen_recorder::~screen_recorder()
{
    std::string ignored;
    stop(ignored);
}

bool screen_recorder::start(display &source, const std::filesystem::path &path,
                            uint64_t emulation_tick, bool with_audio,
                            std::string &error)
{
    if (is_recording())
    {
        error = "A screen recording is already in progress.";
        return false;
    }

    if (!source.capture_rgba(pixels_, width_, height_, error))
        return false;

    file_ = std::fopen(path.string().c_str(), "wb");
    if (!file_)
    {
        error = file_error("Could not create the movie");
        return false;
    }

    output_path_ = path;
    frame_count_ = 0;
    largest_frame_ = 0;
    largest_audio_chunk_ = 0;
    audio_sample_count_ = 0;
    with_audio_ = with_audio;
    index_.clear();
    index_.reserve(FPS * 60 * 10);
    encoded_frame_.clear();
    pending_audio_.clear();
    pending_audio_.reserve(AUDIO_CHUNK_SAMPLES);

    if (!write_avi_header(error))
    {
        std::fclose(file_);
        file_ = nullptr;
        return false;
    }

    started_emulation_tick_ = emulation_tick;
    current_emulation_tick_ = emulation_tick;
    next_frame_tick_ = emulation_tick + CPU_CLOCK_HZ / FPS;

    if (!write_current_frame(1, error))
    {
        std::string ignored;
        stop(ignored);
        return false;
    }

    error.clear();
    return true;
}

bool screen_recorder::write_avi_header(std::string &error)
{
    errno = 0;
    int64_t hdrl_size_pos = -1;
    int64_t strl_size_pos = -1;
    int64_t audio_strl_size_pos = -1;

    if (!write_fourcc(file_, "RIFF"))
        goto write_failed;
    riff_size_pos_ = file_position(file_);
    if (!write_u32(file_, 0) || !write_fourcc(file_, "AVI "))
        goto write_failed;

    if (!write_fourcc(file_, "LIST"))
        goto write_failed;
    hdrl_size_pos = file_position(file_);
    if (!write_u32(file_, 0) || !write_fourcc(file_, "hdrl"))
        goto write_failed;

    if (!write_fourcc(file_, "avih") || !write_u32(file_, 56) ||
        !write_u32(file_, 1000000u / FPS))
        goto write_failed;
    avih_max_bytes_pos_ = file_position(file_);
    if (!write_u32(file_, 0) || !write_u32(file_, 0) || !write_u32(file_, 0x10))
        goto write_failed;
    avih_total_frames_pos_ = file_position(file_);
    if (!write_u32(file_, 0) || !write_u32(file_, 0) ||
        !write_u32(file_, with_audio_ ? 2u : 1u))
        goto write_failed;
    avih_buffer_size_pos_ = file_position(file_);
    if (!write_u32(file_, 0) || !write_u32(file_, static_cast<uint32_t>(width_)) ||
        !write_u32(file_, static_cast<uint32_t>(height_)) ||
        !write_u32(file_, 0) || !write_u32(file_, 0) ||
        !write_u32(file_, 0) || !write_u32(file_, 0))
        goto write_failed;

    if (!write_fourcc(file_, "LIST"))
        goto write_failed;
    strl_size_pos = file_position(file_);
    if (!write_u32(file_, 0) || !write_fourcc(file_, "strl") ||
        !write_fourcc(file_, "strh") || !write_u32(file_, 56) ||
        !write_fourcc(file_, "vids") || !write_fourcc(file_, "MJPG") ||
        !write_u32(file_, 0) || !write_u16(file_, 0) || !write_u16(file_, 0) ||
        !write_u32(file_, 0) || !write_u32(file_, 1) || !write_u32(file_, FPS) ||
        !write_u32(file_, 0))
        goto write_failed;
    strh_length_pos_ = file_position(file_);
    if (!write_u32(file_, 0))
        goto write_failed;
    strh_buffer_size_pos_ = file_position(file_);
    if (!write_u32(file_, 0) || !write_u32(file_, 0xFFFFFFFFu) ||
        !write_u32(file_, 0) ||
        !write_u16(file_, 0) || !write_u16(file_, 0) ||
        !write_u16(file_, static_cast<uint16_t>(width_)) ||
        !write_u16(file_, static_cast<uint16_t>(height_)))
        goto write_failed;

    if (!write_fourcc(file_, "strf") || !write_u32(file_, 40) ||
        !write_u32(file_, 40) || !write_u32(file_, static_cast<uint32_t>(width_)) ||
        !write_u32(file_, static_cast<uint32_t>(height_)) ||
        !write_u16(file_, 1) || !write_u16(file_, 24) ||
        !write_fourcc(file_, "MJPG") ||
        !write_u32(file_, static_cast<uint32_t>(width_ * height_ * 3)) ||
        !write_u32(file_, 0) || !write_u32(file_, 0) ||
        !write_u32(file_, 0) || !write_u32(file_, 0))
        goto write_failed;

    {
        const int64_t strl_end = file_position(file_);
        if (strl_end < 0 ||
            !patch_u32(file_, strl_size_pos,
                       static_cast<uint32_t>(strl_end - strl_size_pos - 4)))
            goto write_failed;
    }

    if (with_audio_)
    {
        if (!write_fourcc(file_, "LIST"))
            goto write_failed;
        audio_strl_size_pos = file_position(file_);
        if (!write_u32(file_, 0) || !write_fourcc(file_, "strl") ||
            !write_fourcc(file_, "strh") || !write_u32(file_, 56) ||
            !write_fourcc(file_, "auds") || !write_u32(file_, 0) ||
            !write_u32(file_, 0) || !write_u16(file_, 0) ||
            !write_u16(file_, 0) || !write_u32(file_, 0) ||
            !write_u32(file_, 2) ||
            !write_u32(file_, AUDIO_SAMPLE_RATE * 2) ||
            !write_u32(file_, 0))
            goto write_failed;
        audio_strh_length_pos_ = file_position(file_);
        if (!write_u32(file_, 0))
            goto write_failed;
        audio_strh_buffer_size_pos_ = file_position(file_);
        if (!write_u32(file_, 0) || !write_u32(file_, 0xFFFFFFFFu) ||
            !write_u32(file_, 2) || !write_u16(file_, 0) ||
            !write_u16(file_, 0) || !write_u16(file_, 0) ||
            !write_u16(file_, 0))
            goto write_failed;

        if (!write_fourcc(file_, "strf") || !write_u32(file_, 16) ||
            !write_u16(file_, 1) || !write_u16(file_, 1) ||
            !write_u32(file_, AUDIO_SAMPLE_RATE) ||
            !write_u32(file_, AUDIO_SAMPLE_RATE * 2) ||
            !write_u16(file_, 2) || !write_u16(file_, 16))
            goto write_failed;

        const int64_t audio_strl_end = file_position(file_);
        if (audio_strl_end < 0 ||
            !patch_u32(file_, audio_strl_size_pos,
                       static_cast<uint32_t>(audio_strl_end -
                                             audio_strl_size_pos - 4)))
            goto write_failed;
    }

    {
        const int64_t hdrl_end = file_position(file_);
        if (hdrl_end < 0 ||
            !patch_u32(file_, hdrl_size_pos,
                       static_cast<uint32_t>(hdrl_end - hdrl_size_pos - 4)))
            goto write_failed;
    }

    if (!write_fourcc(file_, "LIST"))
        goto write_failed;
    movi_size_pos_ = file_position(file_);
    if (!write_u32(file_, 0) || !write_fourcc(file_, "movi"))
        goto write_failed;
    movi_data_pos_ = file_position(file_);
    if (movi_data_pos_ < 0)
        goto write_failed;

    error.clear();
    return true;

write_failed:
    error = file_error("Could not write the AVI header");
    return false;
}

bool screen_recorder::capture_due(display &source, uint64_t emulation_tick,
                                  std::string &error)
{
    if (!is_recording())
    {
        error.clear();
        return true;
    }

    current_emulation_tick_ = std::max(current_emulation_tick_, emulation_tick);
    if (current_emulation_tick_ < next_frame_tick_)
    {
        error.clear();
        return true;
    }

    const uint64_t frame_interval = CPU_CLOCK_HZ / FPS;
    const uint64_t repeats =
        1 + (current_emulation_tick_ - next_frame_tick_) / frame_interval;
    next_frame_tick_ += repeats * frame_interval;

    int capture_width = 0;
    int capture_height = 0;
    if (!source.capture_rgba(pixels_, capture_width, capture_height, error))
        return false;
    if (capture_width != width_ || capture_height != height_)
    {
        error = "The Partner display size changed while recording.";
        return false;
    }

    return write_current_frame(repeats, error);
}

bool screen_recorder::write_current_frame(uint64_t repeats, std::string &error)
{
    encoded_frame_.clear();
    if (!stbi_write_jpg_to_func(append_jpeg_bytes, &encoded_frame_, width_, height_,
                                4, pixels_.data(), 90))
    {
        error = "Could not encode a movie frame.";
        return false;
    }
    if (encoded_frame_.size() > std::numeric_limits<uint32_t>::max())
    {
        error = "An encoded movie frame is too large for AVI.";
        return false;
    }

    const uint32_t frame_size = static_cast<uint32_t>(encoded_frame_.size());
    for (uint64_t frame = 0; frame < repeats; ++frame)
    {
        const int64_t chunk_position = file_position(file_);
        if (chunk_position < 0)
        {
            error = file_error("Could not inspect the movie file");
            return false;
        }

        const uint64_t projected_size = static_cast<uint64_t>(chunk_position) + 8u +
            frame_size + (frame_size & 1u) + 8u +
            static_cast<uint64_t>(index_.size() + 1u) * 16u;
        if (projected_size > std::numeric_limits<uint32_t>::max())
        {
            error = "Recording reached the 4 GiB AVI file-size limit.";
            return false;
        }

        const uint64_t index_offset = static_cast<uint64_t>(chunk_position) -
                                      static_cast<uint64_t>(movi_data_pos_ - 4);
        if (!write_fourcc(file_, "00dc") || !write_u32(file_, frame_size) ||
            !write_bytes(file_, encoded_frame_.data(), encoded_frame_.size()) ||
            ((frame_size & 1u) && std::fputc(0, file_) == EOF))
        {
            error = file_error("Could not write a movie frame");
            return false;
        }

        index_.push_back({{'0', '0', 'd', 'c'}, 0x10,
                          static_cast<uint32_t>(index_offset), frame_size});
        largest_frame_ = std::max(largest_frame_, frame_size);
        ++frame_count_;
    }

    error.clear();
    return true;
}

bool screen_recorder::write_audio_chunk(const int16_t *samples, size_t count,
                                        std::string &error)
{
    if (count == 0)
    {
        error.clear();
        return true;
    }
    if (count > std::numeric_limits<uint32_t>::max() / 2u)
    {
        error = "An audio chunk is too large for AVI.";
        return false;
    }

    const uint32_t chunk_size = static_cast<uint32_t>(count * 2u);
    const int64_t chunk_position = file_position(file_);
    if (chunk_position < 0)
    {
        error = file_error("Could not inspect the movie file");
        return false;
    }
    const uint64_t projected_size = static_cast<uint64_t>(chunk_position) + 8u +
        chunk_size + (chunk_size & 1u) + 8u +
        static_cast<uint64_t>(index_.size() + 1u) * 16u;
    if (projected_size > std::numeric_limits<uint32_t>::max())
    {
        error = "Recording reached the 4 GiB AVI file-size limit.";
        return false;
    }

    std::vector<uint8_t> bytes(chunk_size);
    for (size_t i = 0; i < count; ++i)
    {
        const uint16_t value = static_cast<uint16_t>(samples[i]);
        bytes[i * 2] = static_cast<uint8_t>(value);
        bytes[i * 2 + 1] = static_cast<uint8_t>(value >> 8);
    }

    const uint64_t index_offset = static_cast<uint64_t>(chunk_position) -
                                  static_cast<uint64_t>(movi_data_pos_ - 4);
    if (!write_fourcc(file_, "01wb") || !write_u32(file_, chunk_size) ||
        !write_bytes(file_, bytes.data(), bytes.size()) ||
        ((chunk_size & 1u) && std::fputc(0, file_) == EOF))
    {
        error = file_error("Could not write movie audio");
        return false;
    }

    index_.push_back({{'0', '1', 'w', 'b'}, 0,
                      static_cast<uint32_t>(index_offset), chunk_size});
    largest_audio_chunk_ = std::max(largest_audio_chunk_, chunk_size);
    audio_sample_count_ += count;
    error.clear();
    return true;
}

bool screen_recorder::append_audio_samples(const int16_t *samples, size_t count,
                                           std::string &error)
{
    if (!is_recording() || !with_audio_ || count == 0)
    {
        error.clear();
        return true;
    }

    pending_audio_.insert(pending_audio_.end(), samples, samples + count);
    while (pending_audio_.size() >= AUDIO_CHUNK_SAMPLES)
    {
        if (!write_audio_chunk(pending_audio_.data(), AUDIO_CHUNK_SAMPLES, error))
            return false;
        pending_audio_.erase(pending_audio_.begin(),
                             pending_audio_.begin() + AUDIO_CHUNK_SAMPLES);
    }
    error.clear();
    return true;
}

bool screen_recorder::flush_pending_audio(std::string &error)
{
    if (!with_audio_ || pending_audio_.empty())
    {
        error.clear();
        return true;
    }
    if (!write_audio_chunk(pending_audio_.data(), pending_audio_.size(), error))
        return false;
    pending_audio_.clear();
    return true;
}

bool screen_recorder::finalize_avi(std::string &error)
{
    errno = 0;
    const int64_t movi_end = file_position(file_);
    if (movi_end < 0 ||
        !patch_u32(file_, movi_size_pos_,
                   static_cast<uint32_t>(movi_end - movi_size_pos_ - 4)) ||
        !write_fourcc(file_, "idx1") ||
        !write_u32(file_, static_cast<uint32_t>(index_.size() * 16u)))
    {
        error = file_error("Could not finalize the AVI movie");
        return false;
    }

    for (const avi_index_entry &entry : index_)
    {
        if (!write_bytes(file_, entry.chunk_id, sizeof(entry.chunk_id)) ||
            !write_u32(file_, entry.flags) ||
            !write_u32(file_, entry.offset) || !write_u32(file_, entry.size))
        {
            error = file_error("Could not write the AVI index");
            return false;
        }
    }

    const int64_t file_end = file_position(file_);
    const uint64_t max_bytes_per_second =
        static_cast<uint64_t>(largest_frame_) * FPS +
        (with_audio_ ? AUDIO_SAMPLE_RATE * 2u : 0u);
    const uint32_t largest_buffer =
        std::max(largest_frame_, largest_audio_chunk_);
    if (file_end < 8 || static_cast<uint64_t>(file_end - 8) >
                            std::numeric_limits<uint32_t>::max() ||
        index_.size() > std::numeric_limits<uint32_t>::max() ||
        frame_count_ > std::numeric_limits<uint32_t>::max() ||
        audio_sample_count_ > std::numeric_limits<uint32_t>::max() ||
        !patch_u32(file_, riff_size_pos_, static_cast<uint32_t>(file_end - 8)) ||
        !patch_u32(file_, avih_max_bytes_pos_,
                   static_cast<uint32_t>(std::min<uint64_t>(
                       max_bytes_per_second, std::numeric_limits<uint32_t>::max()))) ||
        !patch_u32(file_, avih_total_frames_pos_, static_cast<uint32_t>(frame_count_)) ||
        !patch_u32(file_, avih_buffer_size_pos_, largest_buffer) ||
        !patch_u32(file_, strh_length_pos_, static_cast<uint32_t>(frame_count_)) ||
        !patch_u32(file_, strh_buffer_size_pos_, largest_frame_) ||
        (with_audio_ &&
         (!patch_u32(file_, audio_strh_length_pos_,
                     static_cast<uint32_t>(audio_sample_count_)) ||
          !patch_u32(file_, audio_strh_buffer_size_pos_, largest_audio_chunk_))))
    {
        error = file_error("Could not finish the AVI metadata");
        return false;
    }

    error.clear();
    return true;
}

bool screen_recorder::stop(std::string &error)
{
    if (!file_)
    {
        error.clear();
        return true;
    }

    const bool audio_flushed = flush_pending_audio(error);
    const bool finalized = audio_flushed && finalize_avi(error);
    FILE *file = file_;
    file_ = nullptr;
    const bool flush_failed = std::fflush(file) != 0;
    const bool close_failed = std::fclose(file) != 0;
    if (!finalized)
        return false;
    if (flush_failed || close_failed)
    {
        error = file_error("Could not close the AVI movie");
        return false;
    }

    error.clear();
    return true;
}

double screen_recorder::elapsed_seconds() const
{
    if (!is_recording())
        return 0.0;
    return static_cast<double>(current_emulation_tick_ - started_emulation_tick_) /
           static_cast<double>(CPU_CLOCK_HZ);
}
