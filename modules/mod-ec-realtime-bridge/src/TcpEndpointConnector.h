/*
 * mod-ec-realtime-bridge - resolved POSIX TCP connections.
 * Copyright (C) 2026 EC. Original implementation, 2026-09-27.
 * Evidence: POSIX getaddrinfo/connect/poll; see doc/operations/Z790_BRIDGE_REPAIR.md.
 */
#ifndef EC_REALTIME_TCP_CONNECTOR_H
#define EC_REALTIME_TCP_CONNECTOR_H

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <memory>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace ECRealtimeBridge
{
/** Owns a candidate descriptor until a complete connection is handed to the caller. */
class CandidateSocket
{
public:
    explicit CandidateSocket(int fd) : _fd(fd) { }
    ~CandidateSocket() { if (_fd >= 0) close(_fd); }
    CandidateSocket(CandidateSocket const&) = delete;
    CandidateSocket& operator=(CandidateSocket const&) = delete;
    int Get() const { return _fd; }
    int Release() { int fd = _fd; _fd = -1; return fd; }
private:
    int _fd;
};

/**
 * Resolve on EVERY attempt and connect to a supported IPv4/IPv6 endpoint.
 * @return Owned nonblocking descriptor, or -1 with a non-secret diagnostic.
 * @note The deadline bounds socket connection attempts after resolution. POSIX
 *       getaddrinfo itself follows the system resolver's timeout policy.
 * @note Called by the connection worker, never by the world update thread.
 */
inline int ConnectResolvedTcp(std::string const& host, uint16_t port, uint32_t timeoutMs,
    std::atomic<bool> const& running, std::string& error)
{
    error.clear();
    if (host.empty() || !port || !running.load())
    {
        error = "empty endpoint, invalid port, or shutdown requested";
        return -1;
    }

    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_NUMERICSERV;
    addrinfo* rawAddresses = nullptr;
    std::string service = std::to_string(port);
    int result = getaddrinfo(host.c_str(), service.c_str(), &hints, &rawAddresses);
    if (result != 0)
    {
        error = std::string("name resolution failed: ") + gai_strerror(result);
        return -1;
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> addresses(rawAddresses, freeaddrinfo);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    error = "resolver returned no usable TCP endpoints";

    for (addrinfo const* address = addresses.get(); address; address = address->ai_next)
    {
        if (!running.load())
        {
            error = "shutdown requested";
            return -1;
        }
        if (std::chrono::steady_clock::now() >= deadline)
        {
            error = "connection deadline expired";
            return -1;
        }
        CandidateSocket candidate(socket(address->ai_family, address->ai_socktype, address->ai_protocol));
        int fd = candidate.Get();
        if (fd < 0)
        {
            error = std::strerror(errno);
            continue;
        }
        int flags = fcntl(fd, F_GETFL, 0);
        int descriptorFlags = fcntl(fd, F_GETFD, 0);
        int noDelay = 1;
        if (flags < 0 || descriptorFlags < 0 ||
            fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
            fcntl(fd, F_SETFD, descriptorFlags | FD_CLOEXEC) < 0 ||
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &noDelay, sizeof(noDelay)) < 0)
        {
            error = std::strerror(errno);
            continue;
        }
        result = connect(fd, address->ai_addr, address->ai_addrlen);
        if (result == 0)
        {
            error.clear();
            return candidate.Release();
        }
        if (errno != EINPROGRESS && errno != EINTR)
        {
            error = std::strerror(errno);
            continue;
        }
        while (running.load())
        {
            auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0)
            {
                error = "connection deadline expired";
                return -1;
            }
            int waitMs = remaining > 100 ? 100 : static_cast<int>(remaining);
            pollfd pending{fd, POLLOUT, 0};
            result = poll(&pending, 1, waitMs);
            if (result < 0 && errno == EINTR)
                continue;
            if (result == 0)
                continue;
            if (result < 0)
            {
                error = std::strerror(errno);
                break;
            }
            int socketError = 0;
            socklen_t length = sizeof(socketError);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) < 0)
            {
                error = std::strerror(errno);
                break;
            }
            if (socketError == 0 && (pending.revents & POLLOUT))
            {
                error.clear();
                return candidate.Release();
            }
            error = socketError ? std::strerror(socketError) : "socket readiness error";
            break;
        }
    }
    if (!running.load())
        error = "shutdown requested";
    return -1;
}
}
#endif
