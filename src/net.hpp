#pragma once
// Minimal portable TCP helpers (Linux)
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>

#include "protocol.hpp"

namespace llmp {

inline bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

inline bool setBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) == 0;
}

inline void setNoDelay(int fd) {
    int yes = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof(yes));
}

inline void setReuseAddr(int fd) {
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
}

inline int createListenSocket(const char* host, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    setReuseAddr(fd);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));
    if (!host || std::strcmp(host, "0.0.0.0") == 0)
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    else
        inet_pton(AF_INET, host, &addr.sin_addr);

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::perror("bind");
        ::close(fd);
        return -1;
    }
    if (listen(fd, 32) < 0) {
        std::perror("listen");
        ::close(fd);
        return -1;
    }
    setNonBlocking(fd);
    return fd;
}

// Non-blocking connect with timeout (seconds). Prefers IPv4 to avoid long IPv6 hangs.
inline int connectTcp(const char* host, int port, std::string& err, int timeoutSec = 5) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    // Prefer results that work; we try IPv4 first by sorting
    addrinfo* res = nullptr;
    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%d", port);
    int rc = getaddrinfo(host, portStr, &hints, &res);
    if (rc != 0) {
        err = std::string("DNS: ") + gai_strerror(rc);
        return -1;
    }

    // Build list: IPv4 first, then others
    std::vector<addrinfo*> order;
    for (addrinfo* p = res; p; p = p->ai_next)
        if (p->ai_family == AF_INET) order.push_back(p);
    for (addrinfo* p = res; p; p = p->ai_next)
        if (p->ai_family != AF_INET) order.push_back(p);

    int fd = -1;
    for (addrinfo* p : order) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        setNonBlocking(fd);

        int cr = ::connect(fd, p->ai_addr, p->ai_addrlen);
        if (cr == 0) {
            // connected immediately
            setNoDelay(fd);
            freeaddrinfo(res);
            return fd;
        }
        if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
            ::close(fd);
            fd = -1;
            continue;
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = POLLOUT;
        int pr = ::poll(&pfd, 1, timeoutSec * 1000);
        if (pr <= 0) {
            ::close(fd);
            fd = -1;
            if (pr == 0) err = "connect timed out";
            continue;
        }
        int soerr = 0;
        socklen_t sl = sizeof(soerr);
        getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
        if (soerr != 0) {
            ::close(fd);
            fd = -1;
            err = std::string("connect: ") + std::strerror(soerr);
            continue;
        }
        setNoDelay(fd);
        freeaddrinfo(res);
        return fd; // stays non-blocking
    }

    freeaddrinfo(res);
    if (err.empty()) err = "connect failed";
    return -1;
}

inline bool sendAll(int fd, const uint8_t* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = ::send(fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                pollfd pfd{};
                pfd.fd = fd;
                pfd.events = POLLOUT;
                if (poll(&pfd, 1, 2000) <= 0) return false;
                continue;
            }
            return false;
        }
        if (r == 0) return false;
        sent += static_cast<size_t>(r);
    }
    return true;
}

inline bool sendPacket(int fd, uint16_t type, const void* payload, uint16_t size) {
    std::vector<uint8_t> pkt;
    writeHeader(pkt, type, size);
    if (size && payload) appendBytes(pkt, payload, size);
    return sendAll(fd, pkt.data(), pkt.size());
}

inline bool sendPacket(int fd, uint16_t type, const std::vector<uint8_t>& payload) {
    return sendPacket(fd, type, payload.empty() ? nullptr : payload.data(),
                      static_cast<uint16_t>(payload.size()));
}

// Non-blocking read into PacketBuffer; returns false on hard disconnect
inline bool recvInto(int fd, PacketBuffer& pb) {
    uint8_t tmp[4096];
    for (;;) {
        ssize_t r = ::recv(fd, tmp, sizeof(tmp), 0);
        if (r > 0) {
            pb.push(tmp, static_cast<size_t>(r));
            continue;
        }
        if (r == 0) return false; // peer closed
        if (errno == EAGAIN || errno == EWOULDBLOCK) return true;
        if (errno == EINTR) continue;
        return false;
    }
}

} // namespace llmp
