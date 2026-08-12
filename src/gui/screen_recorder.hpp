#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

class display;

class screen_recorder
{
public:
    screen_recorder() = default;
    ~screen_recorder();

    screen_recorder(const screen_recorder &) = delete;
    screen_recorder &operator=(const screen_recorder &) = delete;

    bool start(display &source, const std::filesystem::path &path,
               uint64_t emulation_tick, bool with_audio,
               std::string &error);
    bool capture_due(display &source, uint64_t emulation_tick,
                     std::string &error);
    bool append_audio_samples(const int16_t *samples, size_t count,
                              std::string &error);
    bool stop(std::string &error);

    bool is_recording() const { return file_ != nullptr; }
    bool has_audio() const { return file_ != nullptr && with_audio_; }
    const std::filesystem::path &output_path() const { return output_path_; }
    uint64_t frame_count() const { return frame_count_; }
    double elapsed_seconds() const;

private:
    struct avi_index_entry {
        char chunk_id[4];
        uint32_t flags;
        uint32_t offset;
        uint32_t size;
    };

    static constexpr unsigned FPS = 25;
    static constexpr uint32_t CPU_CLOCK_HZ = 4000000;
    static constexpr uint32_t AUDIO_SAMPLE_RATE = 44100;
    static constexpr size_t AUDIO_CHUNK_SAMPLES = AUDIO_SAMPLE_RATE / FPS;
    bool write_avi_header(std::string &error);
    bool finalize_avi(std::string &error);
    bool write_current_frame(uint64_t repeats, std::string &error);
    bool write_audio_chunk(const int16_t *samples, size_t count,
                           std::string &error);
    bool flush_pending_audio(std::string &error);

    FILE *file_ = nullptr;
    std::filesystem::path output_path_;
    std::vector<uint8_t> pixels_;
    std::vector<uint8_t> encoded_frame_;
    std::vector<int16_t> pending_audio_;
    std::vector<avi_index_entry> index_;
    int width_ = 0;
    int height_ = 0;
    uint32_t largest_frame_ = 0;
    uint32_t largest_audio_chunk_ = 0;
    uint64_t frame_count_ = 0;
    uint64_t audio_sample_count_ = 0;
    uint64_t started_emulation_tick_ = 0;
    uint64_t current_emulation_tick_ = 0;
    uint64_t next_frame_tick_ = 0;
    bool with_audio_ = false;
    int64_t riff_size_pos_ = 0;
    int64_t avih_max_bytes_pos_ = 0;
    int64_t avih_total_frames_pos_ = 0;
    int64_t avih_buffer_size_pos_ = 0;
    int64_t strh_length_pos_ = 0;
    int64_t strh_buffer_size_pos_ = 0;
    int64_t audio_strh_length_pos_ = 0;
    int64_t audio_strh_buffer_size_pos_ = 0;
    int64_t movi_size_pos_ = 0;
    int64_t movi_data_pos_ = 0;
};
