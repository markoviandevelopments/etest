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

#include "protocol.hpp"

namespace llmp {

inline bool setNonBlocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
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

inline int connectTcp(const char* host, int port, std::string& err) {
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* res = nullptr;
    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%d", port);
    int rc = getaddrinfo(host, portStr, &hints, &res);
    if (rc != 0) {
        err = gai_strerror(rc);
        return -1;
    }
    int fd = -1;
    for (addrinfo* p = res; p; p = p->ai_next) {
        fd = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, p->ai_addr, p->ai_addrlen) == 0) break;
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (fd < 0) {
        err = "connect failed";
        return -1;
    }
    setNoDelay(fd);
    setNonBlocking(fd);
    return fd;
}

inline bool sendAll(int fd, const uint8_t* data, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = ::send(fd, data + sent, n - sent, MSG_NOSIGNAL);
        if (r < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // brief spin / wait
                fd_set wfds;
                FD_ZERO(&wfds);
                FD_SET(fd, &wfds);
                timeval tv{0, 50000};
                if (select(fd + 1, nullptr, &wfds, nullptr, &tv) <= 0) return false;
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
