#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

using idp_socket_t = std::intptr_t;
using idp_socklen_t = int;
using idp_socket_count_t = int;
inline constexpr idp_socket_t idp_invalid_socket = -1;

inline SOCKET idp_native_socket(idp_socket_t socket)
{
    return static_cast<SOCKET>(socket);
}

inline bool idp_socket_initialize()
{
    static const bool initialized = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    return initialized;
}

inline void idp_socket_close(idp_socket_t socket)
{
    closesocket(idp_native_socket(socket));
}

inline void idp_socket_shutdown(idp_socket_t socket)
{
    shutdown(idp_native_socket(socket), SD_BOTH);
}

inline bool idp_socket_set_nonblocking(idp_socket_t socket)
{
    u_long enabled = 1;
    return ioctlsocket(idp_native_socket(socket), FIONBIO, &enabled) == 0;
}

inline bool idp_socket_would_block()
{
    const int error = WSAGetLastError();
    return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
}

inline std::string idp_socket_error_text()
{
    return "Winsock error " + std::to_string(WSAGetLastError());
}

inline idp_socket_count_t idp_socket_recv(idp_socket_t socket, void *data,
                                           size_t size)
{
    return recv(idp_native_socket(socket), static_cast<char *>(data),
                static_cast<int>(size), 0);
}

inline idp_socket_count_t idp_socket_send(idp_socket_t socket,
                                           const void *data, size_t size)
{
    return send(idp_native_socket(socket), static_cast<const char *>(data),
                static_cast<int>(size), 0);
}

inline void idp_socket_set_reuse_address(idp_socket_t socket)
{
    const char enabled = 1;
    (void)setsockopt(idp_native_socket(socket), SOL_SOCKET, SO_REUSEADDR,
                     &enabled, sizeof(enabled));
}

inline void idp_socket_set_no_delay(idp_socket_t socket)
{
    const char enabled = 1;
    (void)setsockopt(idp_native_socket(socket), IPPROTO_TCP, TCP_NODELAY,
                     &enabled, sizeof(enabled));
}

#else

#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>

using idp_socket_t = std::intptr_t;
using idp_socklen_t = socklen_t;
using idp_socket_count_t = ssize_t;
inline constexpr idp_socket_t idp_invalid_socket = -1;

inline int idp_native_socket(idp_socket_t socket)
{
    return static_cast<int>(socket);
}

inline bool idp_socket_initialize() { return true; }
inline void idp_socket_close(idp_socket_t socket) { ::close(idp_native_socket(socket)); }
inline void idp_socket_shutdown(idp_socket_t socket)
{
    ::shutdown(idp_native_socket(socket), SHUT_RDWR);
}

inline bool idp_socket_set_nonblocking(idp_socket_t socket)
{
    const int fd = idp_native_socket(socket);
    const int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

inline bool idp_socket_would_block()
{
    return errno == EAGAIN || errno == EWOULDBLOCK;
}

inline std::string idp_socket_error_text() { return std::strerror(errno); }

inline idp_socket_count_t idp_socket_recv(idp_socket_t socket, void *data,
                                           size_t size)
{
    return ::recv(idp_native_socket(socket), data, size, 0);
}

inline idp_socket_count_t idp_socket_send(idp_socket_t socket,
                                           const void *data, size_t size)
{
#ifdef MSG_NOSIGNAL
    return ::send(idp_native_socket(socket), data, size, MSG_NOSIGNAL);
#else
    return ::send(idp_native_socket(socket), data, size, 0);
#endif
}

inline void idp_socket_set_reuse_address(idp_socket_t socket)
{
    const int enabled = 1;
    (void)::setsockopt(idp_native_socket(socket), SOL_SOCKET, SO_REUSEADDR,
                       &enabled, sizeof(enabled));
}

inline void idp_socket_set_no_delay(idp_socket_t socket)
{
    const int enabled = 1;
    (void)::setsockopt(idp_native_socket(socket), IPPROTO_TCP, TCP_NODELAY,
                       &enabled, sizeof(enabled));
}

#endif
