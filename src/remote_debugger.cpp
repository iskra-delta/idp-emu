#include "remote_debugger.hpp"

#include "partner.hpp"

#include <xdbgstub/client.hpp>
#include <xdbgstub/error.hpp>
#include <xdbgstub/server.hpp>

#include <algorithm>
#include <sstream>
#include <utility>

namespace {

xdbgstub::cpu_state to_xdbgstub_state(const partner::debug_cpu_state &state)
{
    xdbgstub::cpu_state out;
    out.af = state.af;
    out.bc = state.bc;
    out.de = state.de;
    out.hl = state.hl;
    out.ix = state.ix;
    out.iy = state.iy;
    out.sp = state.sp;
    out.pc = state.pc;
    out.i = state.i;
    out.r = state.r;
    out.iff1 = state.iff1;
    out.iff2 = state.iff2;
    out.halted = state.halted;
    return out;
}

partner::debug_cpu_state to_partner_state(const xdbgstub::cpu_state &state)
{
    partner::debug_cpu_state out;
    out.af = state.af;
    out.bc = state.bc;
    out.de = state.de;
    out.hl = state.hl;
    out.ix = state.ix;
    out.iy = state.iy;
    out.sp = state.sp;
    out.pc = state.pc;
    out.i = state.i;
    out.r = state.r;
    out.iff1 = state.iff1;
    out.iff2 = state.iff2;
    out.halted = state.halted;
    return out;
}

} // namespace

class remote_debugger::target_adapter final : public xdbgstub::target
{
public:
    explicit target_adapter(remote_debugger &owner)
        : owner_(owner)
    {
    }

    xdbgstub::target_status status() override
    {
        std::lock_guard<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        if (owner_.emu_ == nullptr)
            return {};
        return owner_.snapshot_status_locked(*owner_.emu_);
    }

    xdbgstub::cpu_state read_registers() override
    {
        std::lock_guard<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        if (owner_.emu_ == nullptr)
            return {};
        return to_xdbgstub_state(owner_.emu_->capture_debug_cpu_state());
    }

    void write_registers(const xdbgstub::cpu_state &state) override
    {
        std::lock_guard<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        if (owner_.emu_ == nullptr)
            return;
        owner_.emu_->apply_debug_cpu_state(to_partner_state(state));
    }

    std::vector<uint8_t> read_memory(uint32_t address, std::size_t length) override
    {
        std::lock_guard<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        if (owner_.emu_ == nullptr)
            return {};
        return owner_.emu_->read_debug_memory(address, length);
    }

    void write_memory(uint32_t address, const std::vector<uint8_t> &data) override
    {
        std::lock_guard<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        if (owner_.emu_ == nullptr)
            return;
        owner_.emu_->write_debug_memory(address, data);
    }

    xdbgstub::target_status continue_execution() override
    {
        std::unique_lock<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        if (owner_.emu_ == nullptr)
            return {};

        owner_.completed_status_.reset();
        owner_.command_in_flight_ = true;
        owner_.pause_requested_ = false;
        owner_.last_stop_reason_ = xdbgstub::stop_reason::none;
        if (owner_.paused_) {
            owner_.pending_command_ = pending_command::continue_execution;
        } else {
            owner_.remote_continue_active_ = true;
            owner_.pending_command_ = pending_command::none;
        }
        owner_.command_cv_.notify_all();
        owner_.command_cv_.wait(lock, [&]() {
            return !owner_.command_in_flight_ || owner_.stop_requested_;
        });
        if (owner_.emu_ == nullptr)
            return {};
        return owner_.completed_status_.value_or(owner_.snapshot_status_locked(*owner_.emu_));
    }

    xdbgstub::target_status step_instruction() override
    {
        std::unique_lock<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        if (owner_.emu_ == nullptr)
            return {};

        owner_.completed_status_.reset();
        owner_.command_in_flight_ = true;
        owner_.pending_command_ = pending_command::step_instruction;
        if (!owner_.paused_)
            owner_.pause_requested_ = true;
        owner_.command_cv_.notify_all();
        owner_.command_cv_.wait(lock, [&]() {
            return !owner_.command_in_flight_ || owner_.stop_requested_;
        });
        if (owner_.emu_ == nullptr)
            return {};
        return owner_.completed_status_.value_or(owner_.snapshot_status_locked(*owner_.emu_));
    }

    xdbgstub::target_status pause_execution() override
    {
        std::unique_lock<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        if (owner_.emu_ == nullptr)
            return {};

        if (owner_.paused_) {
            owner_.last_stop_reason_ = xdbgstub::stop_reason::pause;
            return owner_.snapshot_status_locked(*owner_.emu_);
        }

        owner_.completed_status_.reset();
        owner_.command_in_flight_ = true;
        owner_.pending_command_ = pending_command::pause_execution;
        owner_.pause_requested_ = true;
        owner_.command_cv_.notify_all();
        owner_.command_cv_.wait(lock, [&]() {
            return !owner_.command_in_flight_ || owner_.stop_requested_;
        });
        if (owner_.emu_ == nullptr)
            return {};
        return owner_.completed_status_.value_or(owner_.snapshot_status_locked(*owner_.emu_));
    }

    void set_breakpoint(uint32_t address) override
    {
        std::lock_guard<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        const uint16_t addr16 = static_cast<uint16_t>(address);
        if (std::find(owner_.breakpoints_.begin(), owner_.breakpoints_.end(), addr16) == owner_.breakpoints_.end())
            owner_.breakpoints_.push_back(addr16);
    }

    void clear_breakpoint(uint32_t address) override
    {
        std::lock_guard<std::recursive_mutex> lock(owner_.mutex_);
        owner_.note_client_activity_locked();
        const uint16_t addr16 = static_cast<uint16_t>(address);
        owner_.breakpoints_.erase(
            std::remove(owner_.breakpoints_.begin(), owner_.breakpoints_.end(), addr16),
            owner_.breakpoints_.end());
    }

    void detach() override
    {
        std::lock_guard<std::recursive_mutex> lock(owner_.mutex_);
        owner_.client_connected_ = false;
        owner_.pause_requested_ = true;
        owner_.remote_continue_active_ = false;
        owner_.last_stop_reason_ = xdbgstub::stop_reason::pause;
    }

private:
    remote_debugger &owner_;
};

remote_debugger::remote_debugger() = default;

remote_debugger::~remote_debugger()
{
    std::string ignored;
    if (!stop(&ignored) && server_thread_.joinable())
        server_thread_.detach();
}

bool remote_debugger::start(partner &emu, const std::string &host, uint16_t port, std::string *error_out)
{
    if (server_thread_.joinable())
        server_thread_.join();

    if (port == 0) {
        if (error_out)
            *error_out = "Port must be greater than zero.";
        return false;
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (enabled_) {
        if (error_out)
            *error_out = "Remote debugger is already enabled.";
        return false;
    }

    emu_ = &emu;
    host_ = host;
    port_ = port;
    last_error_.clear();
    last_stop_reason_ = xdbgstub::stop_reason::none;
    pending_command_ = pending_command::none;
    completed_status_.reset();
    pause_requested_ = false;
    remote_continue_active_ = false;
    command_in_flight_ = false;
    client_connected_ = false;
    stop_requested_ = false;
    target_ = std::make_unique<target_adapter>(*this);
    enabled_ = true;
    server_thread_ = std::thread([this]() { server_loop(); });
    return true;
}

bool remote_debugger::stop(std::string *error_out)
{
    std::string host;
    uint16_t port = 0;
    bool join_thread = false;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        if (!enabled_) {
            join_thread = server_thread_.joinable();
        } else {
            if (client_connected_) {
                if (error_out)
                    *error_out = "Detach xdbg before stopping the remote debugger.";
                return false;
            }
            stop_requested_ = true;
            host = host_;
            port = port_;
            join_thread = server_thread_.joinable();
        }
    }

    if (!host.empty()) {
        try {
            xdbgstub::client client;
            client.connect(host, port);
            client.detach();
        } catch (...) {
            // Best-effort wakeup for the accept loop; ignore transient failures.
        }
    }

    if (join_thread && server_thread_.joinable())
        server_thread_.join();

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    enabled_ = false;
    client_connected_ = false;
    pause_requested_ = false;
    remote_continue_active_ = false;
    command_in_flight_ = false;
    pending_command_ = pending_command::none;
    completed_status_.reset();
    stop_requested_ = false;
    target_.reset();
    return true;
}

bool remote_debugger::is_enabled() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return enabled_;
}

bool remote_debugger::is_client_connected() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return client_connected_;
}

std::string remote_debugger::bind_host() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return host_;
}

uint16_t remote_debugger::bind_port() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return port_;
}

std::string remote_debugger::last_error() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return last_error_;
}

std::string remote_debugger::status_summary() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (!last_error_.empty())
        return "error: " + last_error_;
    if (client_connected_)
        return "connected on " + host_ + ":" + std::to_string(port_);
    if (enabled_)
        return "listening on " + host_ + ":" + std::to_string(port_);
    return "disabled";
}

std::string remote_debugger::debugger_command() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    std::ostringstream out;
    out << "./bin/xdbg --remote " << host_ << ":" << port_;
    return out.str();
}

void remote_debugger::sync_paused_state(bool paused)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    paused_ = paused;
}

remote_debugger::pending_command remote_debugger::take_pending_command(bool paused)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    paused_ = paused;

    if (paused_ && pending_command_ == pending_command::pause_execution && command_in_flight_ && emu_ != nullptr) {
        last_stop_reason_ = xdbgstub::stop_reason::pause;
        completed_status_ = snapshot_status_locked(*emu_);
        command_in_flight_ = false;
        pending_command_ = pending_command::none;
        command_cv_.notify_all();
        return pending_command::none;
    }

    if (!paused_)
        return pending_command::none;

    const pending_command command = pending_command_;
    if (command == pending_command::continue_execution) {
        pending_command_ = pending_command::none;
        remote_continue_active_ = true;
        pause_requested_ = false;
        last_stop_reason_ = xdbgstub::stop_reason::none;
    } else if (command == pending_command::step_instruction) {
        pending_command_ = pending_command::none;
        pause_requested_ = false;
    }
    return command;
}

remote_debugger::pending_command remote_debugger::current_pending_command() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return pending_command_;
}

bool remote_debugger::remote_continue_active() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return remote_continue_active_;
}

bool remote_debugger::pause_requested() const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return pause_requested_;
}

bool remote_debugger::has_breakpoint(uint16_t address) const
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return std::find(breakpoints_.begin(), breakpoints_.end(), address) != breakpoints_.end();
}

void remote_debugger::complete_stop(partner &emu, xdbgstub::stop_reason reason, bool paused)
{
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    paused_ = paused;
    last_stop_reason_ = reason;
    pause_requested_ = false;
    remote_continue_active_ = false;
    if (command_in_flight_) {
        completed_status_ = snapshot_status_locked(emu);
        command_in_flight_ = false;
        command_cv_.notify_all();
    }
}

xdbgstub::target_status remote_debugger::snapshot_status_locked(partner &emu) const
{
    xdbgstub::target_status status;
    const partner::debug_cpu_state cpu = emu.capture_debug_cpu_state();
    status.state = (remote_continue_active_ || !paused_)
        ? xdbgstub::execution_state::running
        : xdbgstub::execution_state::stopped;
    status.reason = (status.state == xdbgstub::execution_state::running)
        ? xdbgstub::stop_reason::none
        : last_stop_reason_;
    if (cpu.halted && status.state != xdbgstub::execution_state::running &&
        status.reason == xdbgstub::stop_reason::none) {
        status.reason = xdbgstub::stop_reason::halted;
    }
    status.pc = emu.get_current_pc();
    status.registers = to_xdbgstub_state(cpu);
    return status;
}

void remote_debugger::note_client_activity_locked()
{
    client_connected_ = true;
    if (!enabled_)
        enabled_ = true;
    last_error_.clear();
}

void remote_debugger::server_loop()
{
    std::string host;
    uint16_t port = 0;
    {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        host = host_;
        port = port_;
    }

    try {
        xdbgstub::server server;
        server.listen(host, port);
        while (true) {
            {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                if (stop_requested_)
                    break;
            }

            try {
                server.serve(*target_);
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                client_connected_ = false;
                if (stop_requested_)
                    break;
            } catch (const xdbgstub::error &e) {
                std::lock_guard<std::recursive_mutex> lock(mutex_);
                client_connected_ = false;
                if (stop_requested_)
                    break;
                if (std::string(e.what()) == "connection closed")
                    continue;
                last_error_ = e.what();
                break;
            }
        }
        server.close();
    } catch (const std::exception &e) {
        std::lock_guard<std::recursive_mutex> lock(mutex_);
        last_error_ = e.what();
    }

    std::lock_guard<std::recursive_mutex> lock(mutex_);
    enabled_ = false;
    client_connected_ = false;
    pause_requested_ = false;
    remote_continue_active_ = false;
    if (command_in_flight_ && emu_ != nullptr) {
        completed_status_ = snapshot_status_locked(*emu_);
        command_in_flight_ = false;
        command_cv_.notify_all();
    }
}
