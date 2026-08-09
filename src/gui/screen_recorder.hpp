#pragma once

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
               std::string &error);
    bool capture_due(display &source, std::string &error);
    bool stop(std::string &error);

    bool is_recording() const { return file_ != nullptr; }
    const std::filesystem::path &output_path() const { return output_path_; }
    uint64_t frame_count() const { return frame_count_; }
    double elapsed_seconds() const;

private:
    struct avi_index_entry {
        uint32_t offset;
        uint32_t size;
    };

    static constexpr unsigned FPS = 25;
    static constexpr uint64_t MAX_CATCH_UP_FRAMES = FPS;

    bool write_avi_header(std::string &error);
    bool finalize_avi(std::string &error);
    bool write_current_frame(uint64_t repeats, std::string &error);

    FILE *file_ = nullptr;
    std::filesystem::path output_path_;
    std::vector<uint8_t> pixels_;
    std::vector<uint8_t> encoded_frame_;
    std::vector<avi_index_entry> index_;
    int width_ = 0;
    int height_ = 0;
    uint32_t largest_frame_ = 0;
    uint64_t frame_count_ = 0;
    uint64_t started_counter_ = 0;
    uint64_t next_frame_counter_ = 0;
    uint64_t counter_frequency_ = 0;
    uint64_t frame_interval_ = 0;
    int64_t riff_size_pos_ = 0;
    int64_t avih_max_bytes_pos_ = 0;
    int64_t avih_total_frames_pos_ = 0;
    int64_t avih_buffer_size_pos_ = 0;
    int64_t strh_length_pos_ = 0;
    int64_t strh_buffer_size_pos_ = 0;
    int64_t movi_size_pos_ = 0;
    int64_t movi_data_pos_ = 0;
};
