#pragma once

#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <xdbgstub/model.hpp>

class partner;

class remote_debugger
{
public:
    enum class pending_command {
        none,
        continue_execution,
        step_instruction,
        pause_execution
    };

    remote_debugger();
    ~remote_debugger();

    std::recursive_mutex &mutex() { return mutex_; }

    bool start(partner &emu, const std::string &host, uint16_t port, std::string *error_out = nullptr);
    bool stop(std::string *error_out = nullptr);

    bool is_enabled() const;
    bool is_client_connected() const;
    std::string bind_host() const;
    uint16_t bind_port() const;
    std::string last_error() const;
    std::string status_summary() const;
    std::string debugger_command() const;

    void sync_paused_state(bool paused);
    pending_command take_pending_command(bool paused);
    pending_command current_pending_command() const;
    bool remote_continue_active() const;
    bool pause_requested() const;
    bool has_breakpoint(uint16_t address) const;
    void complete_stop(partner &emu, xdbgstub::stop_reason reason, bool paused);

private:
    class target_adapter;

    xdbgstub::target_status snapshot_status_locked(partner &emu) const;
    void note_client_activity_locked();
    void server_loop();

    mutable std::recursive_mutex mutex_;
    std::condition_variable_any command_cv_;
    std::unique_ptr<target_adapter> target_;
    partner *emu_ = nullptr;
    std::thread server_thread_;
    std::vector<uint16_t> breakpoints_;
    std::optional<xdbgstub::target_status> completed_status_;
    std::string host_ = "127.0.0.1";
    uint16_t port_ = 9000;
    std::string last_error_;
    xdbgstub::stop_reason last_stop_reason_ = xdbgstub::stop_reason::none;
    pending_command pending_command_ = pending_command::none;
    bool enabled_ = false;
    bool client_connected_ = false;
    bool paused_ = false;
    bool pause_requested_ = false;
    bool remote_continue_active_ = false;
    bool command_in_flight_ = false;
    bool stop_requested_ = false;
};
