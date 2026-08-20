// dap_debugger.cpp - udap DAP debug server for the partner emulator.
#include <cerrno>
#include <cstring>
#include <iostream>
#include <istream>
#include <ostream>
#include <streambuf>

#include "dap_debugger.hpp"
#include "dap_target.hpp"

namespace {

// Adapts a connected socket fd into a std::streambuf so the DAP session can
// run over plain std::istream/std::ostream.
class fd_streambuf : public std::streambuf
{
public:
    explicit fd_streambuf(idp_socket_t fd) : fd_(fd)
    {
        setg(in_buf_, in_buf_, in_buf_);
        setp(out_buf_, out_buf_ + kBufSize);
    }

protected:
    int underflow() override
    {
        if (gptr() < egptr())
            return traits_type::to_int_type(*gptr());
        const idp_socket_count_t n = idp_socket_recv(fd_, in_buf_, kBufSize);
        if (n <= 0)
            return traits_type::eof();
        setg(in_buf_, in_buf_, in_buf_ + n);
        return traits_type::to_int_type(*gptr());
    }

    int overflow(int c) override
    {
        if (sync() == -1)
            return traits_type::eof();
        if (c != traits_type::eof()) {
            *pptr() = static_cast<char>(c);
            pbump(1);
        }
        return c;
    }

    int sync() override
    {
        const char *p = pbase();
        while (p < pptr()) {
            const idp_socket_count_t n = idp_socket_send(
                fd_, p, static_cast<size_t>(pptr() - p));
            if (n <= 0)
                return -1;
            p += n;
        }
        setp(out_buf_, out_buf_ + kBufSize);
        return 0;
    }

private:
    static constexpr size_t kBufSize = 4096;
    idp_socket_t fd_;
    char in_buf_[kBufSize];
    char out_buf_[kBufSize];
};

} // namespace

dap_debugger::dap_debugger() = default;

dap_debugger::~dap_debugger()
{
    stop();
}

void dap_debugger::close_fd(idp_socket_t &fd)
{
    if (fd >= 0) {
        idp_socket_shutdown(fd);
        idp_socket_close(fd);
        fd = idp_invalid_socket;
    }
}

bool dap_debugger::start(partner &emu, const std::string &host, uint16_t port,
                         std::string *error_out)
{
    auto fail = [&](const std::string &msg) {
        if (error_out)
            *error_out = msg;
        return false;
    };

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (enabled_)
            return fail("Debug server is already running.");
    }

    if (!idp_socket_initialize())
        return fail("could not initialize the host socket API");

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    const std::string bind_addr = host.empty() ? "127.0.0.1" : host;
    if (::inet_pton(AF_INET, bind_addr.c_str(), &addr.sin_addr) != 1)
        return fail("Invalid bind address: " + bind_addr);

    const idp_socket_t fd = (idp_socket_t)::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return fail(std::string("socket() failed: ") + idp_socket_error_text());

    idp_socket_set_reuse_address(fd);

    if (::bind(idp_native_socket(fd), reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0) {
        std::string msg = std::string("bind() failed: ") + idp_socket_error_text();
        idp_socket_close(fd);
        return fail(msg);
    }
    if (::listen(idp_native_socket(fd), 1) < 0) {
        std::string msg = std::string("listen() failed: ") + idp_socket_error_text();
        idp_socket_close(fd);
        return fail(msg);
    }

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        emu_ = &emu;
        host_ = bind_addr;
        port_ = port;
        listen_fd_ = fd;
        enabled_ = true;
        pending_command_ = pending_command::none;
        pause_requested_.store(false);
        pause_silent_ = false;
    }

    server_thread_ = std::thread(&dap_debugger::server_loop, this);
    std::cerr << "[dap] udap server listening on " << bind_addr << ":" << port << "\n";
    return true;
}

bool dap_debugger::stop(std::string *error_out)
{
    (void)error_out;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (!enabled_ && !server_thread_.joinable())
            return true;
        enabled_ = false;
        // Unblock accept() and any in-flight session read.
        close_fd(listen_fd_);
        close_fd(client_fd_);
    }
    state_cv_.notify_all();
    if (server_thread_.joinable())
        server_thread_.join();
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        client_connected_ = false;
        pending_command_ = pending_command::none;
        pause_requested_.store(false);
    }
    return true;
}

void dap_debugger::server_loop()
{
    while (true) {
        idp_socket_t lfd;
        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!enabled_)
                break;
            lfd = listen_fd_;
        }
        if (lfd < 0)
            break;

        sockaddr_in peer{};
        idp_socklen_t peer_len = sizeof(peer);
        const idp_socket_t client = (idp_socket_t)::accept(
            idp_native_socket(lfd), reinterpret_cast<sockaddr *>(&peer), &peer_len);
        if (client < 0) {
            std::lock_guard<std::mutex> lock(state_mutex_);
            if (!enabled_)
                break;
            continue;
        }

        idp_socket_set_no_delay(client);

        char peer_str[INET_ADDRSTRLEN] = "?";
        ::inet_ntop(AF_INET, &peer.sin_addr, peer_str, sizeof(peer_str));
        std::cerr << "[dap] client connected: " << peer_str << "\n";

        auto target = std::make_shared<partner_target>(*emu_, *this);
        {
            std::lock_guard<std::recursive_mutex> emu_lock(emu_mutex_);
            std::lock_guard<std::mutex> lock(state_mutex_);
            client_fd_ = client;
            client_connected_ = true;
            target_ = target;
            target_hot_ = target.get();
            session_active_.store(true);
        }

        fd_streambuf buf(client);
        std::istream in(&buf);
        std::ostream out(&buf);
        target->run(in, out); // blocks for the duration of the session

        {
            std::lock_guard<std::recursive_mutex> emu_lock(emu_mutex_);
            std::lock_guard<std::mutex> lock(state_mutex_);
            session_active_.store(false);
            target_hot_ = nullptr;
            target_.reset();
            client_connected_ = false;
            pending_command_ = pending_command::none;
            pause_requested_.store(false);
            continue_active_ = false;
            close_fd(client_fd_);
        }
        std::cerr << "[dap] client disconnected\n";
    }
}

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

bool dap_debugger::is_enabled() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return enabled_;
}

bool dap_debugger::is_client_connected() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return client_connected_;
}

std::string dap_debugger::bind_host() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return host_;
}

uint16_t dap_debugger::bind_port() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return port_;
}

std::string dap_debugger::status_summary() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    if (!enabled_)
        return "stopped";
    if (client_connected_)
        return "client connected (" + host_ + ":" + std::to_string(port_) + ")";
    return "listening on " + host_ + ":" + std::to_string(port_);
}

std::string dap_debugger::debugger_command() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return "launch.json: { \"type\": \"udap\", \"request\": \"launch\", "
           "\"program\": \"<file.ihx|.bin>\", \"debugServer\": "
           + std::to_string(port_) + " }";
}

// ---------------------------------------------------------------------------
// Main-loop API
// ---------------------------------------------------------------------------

void dap_debugger::sync_paused_state(bool paused)
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (paused_ == paused)
            return;
        paused_ = paused;
    }
    state_cv_.notify_all();
}

dap_debugger::pending_command dap_debugger::take_pending_command()
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    pending_command cmd = pending_command_;
    pending_command_ = pending_command::none;
    return cmd;
}

void dap_debugger::complete_pause()
{
    bool silent;
    std::shared_ptr<partner_target> target;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        silent = pause_silent_;
        pause_requested_.store(false);
        pause_silent_ = false;
        paused_ = true;
        if (!silent)
            continue_active_ = false;
        target = target_;
    }
    state_cv_.notify_all();
    if (!silent && target)
        target->send_stopped("pause");
}

bool dap_debugger::continue_active() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return continue_active_;
}

bool dap_debugger::has_breakpoint(uint16_t address) const
{
    // Caller (the main loop) holds mutex(), which serializes against both
    // target_hot_ lifetime and the session thread's breakpoint mutations.
    return target_hot_ && target_hot_->dbg().is_breakpoint(address);
}

void dap_debugger::notify_stopped(const std::string &reason)
{
    std::shared_ptr<partner_target> target;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        paused_ = true;
        pause_requested_.store(false);
        pause_silent_ = false;
        continue_active_ = false;
        target = target_;
    }
    state_cv_.notify_all();
    if (target)
        target->send_stopped(reason);
}

// ---------------------------------------------------------------------------
// Session-thread API
// ---------------------------------------------------------------------------

void dap_debugger::request_continue()
{
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        pending_command_ = pending_command::continue_execution;
        pause_requested_.store(false);
        pause_silent_ = false;
        continue_active_ = true;
    }
    state_cv_.notify_all();
}

void dap_debugger::request_pause(bool silent)
{
    std::shared_ptr<partner_target> already_paused_target;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        if (paused_) {
            // Already stopped (e.g. paused from the GUI). The client still
            // expects a stopped event for a non-silent pause.
            if (!silent)
                already_paused_target = target_;
        } else {
            pause_silent_ = silent;
            pause_requested_.store(true);
        }
    }
    state_cv_.notify_all();
    if (already_paused_target)
        already_paused_target->send_stopped("pause");
}

bool dap_debugger::wait_until_paused(int timeout_ms)
{
    std::unique_lock<std::mutex> lock(state_mutex_);
    return state_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
                              [this] { return paused_; });
}

bool dap_debugger::paused() const
{
    std::lock_guard<std::mutex> lock(state_mutex_);
    return paused_;
}
