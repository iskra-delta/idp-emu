// dap_debugger.hpp - udap DAP debug server for the partner emulator.
//
// Owns the TCP listener and the per-connection DAP session (udap's libdap,
// see https://github.com/retro-vault/udap). The emulator main loop keeps
// ownership of the machine; this class mediates between the DAP session
// thread and the main loop:
//
//  - DAP "continue" is queued as a pending command the main loop picks up.
//  - DAP "pause" sets a request flag the main loop honours at the next
//    instruction boundary.
//  - Synchronous steps run on the session thread under mutex() while the
//    main loop is paused.
//  - During free run the main loop tests has_breakpoint() at instruction
//    boundaries and calls notify_stopped() when the machine stops.
#pragma once

#include "../platform/socket_compat.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

class partner;
class partner_target;

class dap_debugger
{
public:
    enum class pending_command {
        none,
        continue_execution
    };

    dap_debugger();
    ~dap_debugger();

    // Big emulator lock: the main loop holds it while ticking the machine;
    // session-thread target methods hold it while touching the machine.
    std::recursive_mutex &mutex() { return emu_mutex_; }

    // Lifecycle (call without holding mutex()).
    bool start(partner &emu, const std::string &host, uint16_t port,
               std::string *error_out = nullptr);
    bool stop(std::string *error_out = nullptr);

    bool is_enabled() const;
    bool is_client_connected() const;
    std::string bind_host() const;
    uint16_t bind_port() const;
    std::string status_summary() const;
    std::string debugger_command() const;

    // ---- main-loop API (call with mutex() held) ----
    void sync_paused_state(bool paused);
    pending_command take_pending_command();
    // Lock-free: polled at every instruction boundary in the emulation loop.
    bool pause_requested() const { return pause_requested_.load(std::memory_order_relaxed); }
    // Honour a pause request: clears it and, unless it was a silent pause
    // (used by launch), sends the DAP "stopped" event.
    void complete_pause();
    // Lock-free: polled at every instruction boundary in the emulation loop.
    bool session_active() const { return session_active_.load(std::memory_order_relaxed); }
    // True between a client "continue" and the next stop notification; used
    // by the main loop to tell the client when the user pauses from the GUI.
    bool continue_active() const;
    bool has_breakpoint(uint16_t address) const;
    // Machine stopped during free run (breakpoint hit, HALT, GUI pause while
    // a client continue was active). Sends the DAP "stopped" event.
    void notify_stopped(const std::string &reason);

    // ---- session-thread API (called by partner_target) ----
    void request_continue();
    void request_pause(bool silent);
    // Wait until the main loop reports the machine paused. Call WITHOUT
    // holding mutex(). Returns false on timeout.
    bool wait_until_paused(int timeout_ms);
    bool paused() const;

private:
    void server_loop();
    void close_fd(idp_socket_t &fd);

    std::recursive_mutex emu_mutex_;

    // Session/main-loop handshake state.
    mutable std::mutex state_mutex_;
    std::condition_variable state_cv_;
    partner *emu_ = nullptr;
    std::shared_ptr<partner_target> target_;
    // Raw mirror of target_ for the emulation hot loop; set/cleared while
    // holding mutex(), read only with mutex() held (has_breakpoint).
    partner_target *target_hot_ = nullptr;
    std::atomic<bool> pause_requested_{false};
    std::atomic<bool> session_active_{false};
    std::thread server_thread_;
    std::string host_ = "127.0.0.1";
    uint16_t port_ = 4711;
    idp_socket_t listen_fd_ = idp_invalid_socket;
    idp_socket_t client_fd_ = idp_invalid_socket;
    pending_command pending_command_ = pending_command::none;
    bool enabled_ = false;
    bool client_connected_ = false;
    bool paused_ = false;
    bool pause_silent_ = false;
    bool continue_active_ = false;
};
