#pragma once
// Cloudflare Tunnel TCP for clients:
//
//   Origin (server PC):  cloudflared ingress → tcp://localhost:9043
//   Public hostname:     robot.example.com   (NO public :9043 — edge is 443)
//   Client:              cloudflared access tcp --hostname robot.example.com
//                        --url 127.0.0.1:<localPort>
//   Game connects to:    127.0.0.1:<localPort> only
//
// Never dial robot.example.com:9043 — that port is not open on Cloudflare.
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <iostream>
#include <fstream>
#include <chrono>

struct TunnelProxy {
    pid_t pid = -1;
    int localPort = 0;
    std::string hostname;
    std::string logPath;

    bool running() const { return pid > 0; }

    void stop() {
        if (pid > 0) {
            kill(pid, SIGTERM);
            int status = 0;
            for (int i = 0; i < 30; ++i) {
                if (waitpid(pid, &status, WNOHANG) != 0) break;
                usleep(50000);
            }
            if (waitpid(pid, &status, WNOHANG) == 0) {
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
            }
            pid = -1;
        }
    }

    ~TunnelProxy() { stop(); }

    static int findFreePort() {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return 19043;
        int yes = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            return 19043;
        }
        socklen_t len = sizeof(addr);
        getsockname(fd, reinterpret_cast<sockaddr*>(&addr), &len);
        int port = ntohs(addr.sin_port);
        ::close(fd);
        return port > 0 ? port : 19043;
    }

    static bool portOpen(int port) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return false;
        // short connect timeout via nonblock + poll
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<uint16_t>(port));
        inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
        int cr = ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
        bool ok = false;
        if (cr == 0) ok = true;
        else if (errno == EINPROGRESS) {
            fd_set wfds;
            FD_ZERO(&wfds);
            FD_SET(fd, &wfds);
            timeval tv{0, 200000}; // 200ms
            if (select(fd + 1, nullptr, &wfds, nullptr, &tv) > 0) {
                int soerr = 0;
                socklen_t sl = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soerr, &sl);
                ok = (soerr == 0);
            }
        }
        ::close(fd);
        return ok;
    }

    // host = public hostname only (no :port). Origin port stays 9043 on the server.
    bool start(const std::string& host) {
        stop();
        // strip accidental :port from hostname
        hostname = host;
        auto colon = hostname.rfind(':');
        if (colon != std::string::npos && hostname.find(']') == std::string::npos) {
            // host:port form — drop port (public edge does not use game port)
            hostname = hostname.substr(0, colon);
        }

        localPort = findFreePort();
        logPath = "/tmp/leonida-cf-access-" + std::to_string(getpid()) + ".log";

        const char* bin = "cloudflared";
        if (access("/usr/local/bin/cloudflared", X_OK) == 0)
            bin = "/usr/local/bin/cloudflared";
        else if (access("/usr/bin/cloudflared", X_OK) == 0)
            bin = "/usr/bin/cloudflared";

        // Local listener only — cloudflared talks to Cloudflare on 443, not host:9043
        std::string url = "127.0.0.1:" + std::to_string(localPort);

        std::cout << "\n=== Cloudflare Tunnel (TCP) ===\n"
                  << "  Public hostname : " << hostname << "  (edge HTTPS/443 — not :"
                  << "9043)\n"
                  << "  Access proxy    : " << bin << " access tcp \\\n"
                  << "                      --hostname " << hostname << " \\\n"
                  << "                      --url " << url << "\n"
                  << "  Game connects to: " << url << "\n"
                  << "  Origin (server) : tcp://localhost:9043  (gta6_server)\n"
                  << "  Log             : " << logPath << "\n"
                  << "If a browser opens, finish the Access login, then wait.\n\n"
                  << std::flush;

        pid = fork();
        if (pid < 0) {
            std::cerr << "fork failed for cloudflared\n";
            return false;
        }
        if (pid == 0) {
            int logfd = open(logPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (logfd >= 0) {
                dup2(logfd, STDOUT_FILENO);
                dup2(logfd, STDERR_FILENO);
                close(logfd);
            }
            execl(bin, bin, "access", "tcp",
                  "--hostname", hostname.c_str(),
                  "--url", url.c_str(),
                  (char*)nullptr);
            execlp("cloudflared", "cloudflared", "access", "tcp",
                   "--hostname", hostname.c_str(),
                   "--url", url.c_str(),
                   (char*)nullptr);
            _exit(127);
        }

        // Wait for local proxy (up to 20s). Do not touch hostname:9043.
        for (int i = 0; i < 200; ++i) {
            int status = 0;
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid) {
                std::cerr << "cloudflared access exited early.\n";
                std::ifstream in(logPath);
                if (in) {
                    std::cerr << "--- cloudflared log ---\n" << in.rdbuf() << "\n";
                }
                std::cerr << "Install cloudflared or run manually:\n"
                          << "  cloudflared access tcp --hostname " << hostname
                          << " --url 127.0.0.1:19043\n"
                          << "  ./gta6_clone 127.0.0.1 19043\n";
                pid = -1;
                return false;
            }
            if (portOpen(localPort)) {
                // Give the proxy a moment to finish handshake plumbing
                usleep(200000);
                std::cout << "Access proxy listening on 127.0.0.1:" << localPort << "\n"
                          << std::flush;
                return true;
            }
            if (i == 30)
                std::cout << "Waiting for cloudflared access proxy...\n" << std::flush;
            usleep(100000);
        }
        std::cerr << "Timed out waiting for local Access proxy on port " << localPort << "\n";
        std::ifstream in(logPath);
        if (in) std::cerr << "--- cloudflared log ---\n" << in.rdbuf() << "\n";
        stop();
        return false;
    }
};

// Hostnames that must use Access TCP (never dial host:9043)
inline bool needsCloudflaredAccess(const std::string& host) {
    if (host == "127.0.0.1" || host == "localhost" || host == "::1")
        return false;
    // bare IP → direct
    bool allDigitDot = !host.empty();
    for (char c : host) {
        if (!(c == '.' || (c >= '0' && c <= '9'))) { allDigitDot = false; break; }
    }
    if (allDigitDot) return false;

    // Only Cloudflare Tunnel / our published apps need Access TCP.
    // A plain VPS with open :9043 should connect directly.
    if (host.find("immenseaccumulationonline.online") != std::string::npos)
        return true;
    if (host.find("cfargotunnel.com") != std::string::npos)
        return true;
    if (const char* force = std::getenv("LEONIDA_FORCE_ACCESS")) {
        if (force[0] == '1') return true;
    }
    return false;
}

// Parse "host" or "host:port" — for tunnel hosts, ignore public port
inline void parseHostPort(const std::string& arg, std::string& host, int& port,
                          int defaultPort, bool tunnelHost) {
    host = arg;
    auto colon = arg.rfind(':');
    if (colon != std::string::npos && arg.find(']') == std::string::npos) {
        std::string maybePort = arg.substr(colon + 1);
        bool digits = !maybePort.empty();
        for (char c : maybePort)
            if (c < '0' || c > '9') digits = false;
        if (digits) {
            host = arg.substr(0, colon);
            if (!tunnelHost)
                port = std::atoi(maybePort.c_str());
            // if tunnelHost: keep defaultPort unused; Access will set local port later
            (void)defaultPort;
        }
    }
}
