#pragma once

#include "gui/display.hpp"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

class partner;

class idp_mcp_server
{
public:
    idp_mcp_server(partner &machine, std::string model);

    std::optional<nlohmann::json> handle(const nlohmann::json &message);
    std::string handle_line(const std::string &line);
    nlohmann::json list_tools() const;

private:
    struct breakpoint_entry {
        uint32_t id = 0;
        std::string kind;
        uint16_t address = 0;
        std::optional<uint8_t> value;
        bool enabled = true;
        uint64_t hits = 0;
    };

    struct captured_screen {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgb;
    };

    partner &machine_;
    std::string model_;
    display framebuffer_;
    std::vector<breakpoint_entry> breakpoints_;
    uint32_t next_breakpoint_id_ = 1;
    std::ofstream video_file_;
    std::string video_path_;
    uint64_t video_start_tick_ = 0;
    uint64_t video_frames_ = 0;
    int video_width_ = 0;
    int video_height_ = 0;

    nlohmann::json invoke_tool(const std::string &name,
                               const nlohmann::json &arguments);
    nlohmann::json machine_state() const;
    nlohmann::json register_state() const;
    nlohmann::json run_machine(uint64_t tick_limit, uint64_t instruction_limit,
                               std::optional<uint16_t> until_pc,
                               bool stop_on_halt);
    nlohmann::json breakpoint_state() const;
    std::optional<uint32_t> execute_breakpoint(uint16_t address);
    std::optional<uint32_t> bus_breakpoint(uint64_t pins);
    captured_screen capture_screen(int scale = 1);
    void record_video_frame();
    void capture_video_if_due();
};
