// Leonida Lights — multiplayer server
// Binds 0.0.0.0:9043 by default (override with argv)
//
//   ./gta6_server
//   ./gta6_server 9043
//   ./gta6_server 0.0.0.0 9043
//
// Cloudflare Tunnel: service url tcp://localhost:9043
// Clients: ./gta6_clone <your-tunnel-host> 9043

#include "protocol.hpp"
#include "net.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>

using namespace llmp;

struct Vehicle {
    int32_t id = 0;
    float x = 0, y = 0, z = 0;
    float yaw = 0, speed = 0;
    int32_t owner = -1;
    float r = 0.8f, g = 0.2f, b = 0.2f;
    bool ai = true;
    float aiTimer = 0.f;
    float aiSteer = 0.f;
    float aiThrottle = 0.4f;
};

struct Client {
    int fd = -1;
    uint32_t id = 0;
    bool alive = false;
    bool welcomed = false;
    PacketBuffer in;
    std::string name = "Player";
    float x = 0, y = 0, z = 0, yaw = 0;
    uint8_t in_vehicle = 0;
    int32_t vehicle_id = -1;
    float cr = 0.95f, cg = 0.35f, cb = 0.4f;
    double lastHeard = 0.0;
};

static constexpr float CITY_HALF = 280.f;
static constexpr float BLOCK = 32.f;

static float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}
static void spawnVehicles(std::vector<Vehicle>& vehs) {
    vehs.clear();
    int id = 0;
    auto add = [&](float x, float z, float yaw, float r, float g, float b, bool ai) {
        Vehicle v;
        v.id = id++;
        // Keep on road centerlines (x or z near k*BLOCK)
        v.x = x; v.y = 0; v.z = z;
        v.yaw = yaw; v.speed = 0;
        v.owner = -1;
        v.r = r; v.g = g; v.b = b;
        v.ai = ai;
        vehs.push_back(v);
    };
    // Parked on the N/S road through origin (x≈0) and E/W cross streets
    add(0.f, 10.f, 0.f, 0.95f, 0.15f, 0.28f, false);
    add(0.f, 16.f, 3.14159f, 0.15f, 0.75f, 0.85f, false);
    add(BLOCK, 0.f, 1.57f, 1.f, 0.85f, 0.2f, false);
    add(-BLOCK, 0.f, -1.57f, 0.95f, 0.95f, 0.98f, false);
    add(0.f, -BLOCK, 0.f, 0.4f, 0.2f, 0.7f, false);

    const float colors[][3] = {
        {0.8f,0.2f,0.2f},{0.2f,0.4f,0.8f},{0.2f,0.7f,0.3f},
        {0.9f,0.6f,0.1f},{0.6f,0.6f,0.65f},{0.1f,0.1f,0.12f}
    };
    // More traffic across the expanded grid
    for (int i = 0; i < 36; ++i) {
        int ri = (i % 7) - 3;
        float z = ((i / 7) - 2) * BLOCK;
        int ci = i % 6;
        float yaw = (i % 2) ? 0.f : 3.14159f;
        add(ri * BLOCK + 1.2f, z, yaw, colors[ci][0], colors[ci][1], colors[ci][2], true);
    }
}

static void updateAIVehicle(Vehicle& v, float dt) {
    if (v.owner >= 0 || !v.ai) return;
    v.aiTimer -= dt;
    if (v.aiTimer <= 0.f) {
        v.aiTimer = 1.5f + (std::rand() % 100) / 50.f;
        v.aiSteer = ((std::rand() % 200) - 100) / 100.f * 0.45f;
        v.aiThrottle = 0.3f + (std::rand() % 50) / 100.f;
    }
    const float accel = 12.f, friction = 6.f, maxSp = 12.f, turn = 1.6f;
    v.speed += v.aiThrottle * accel * dt;
    v.speed = clampf(v.speed, 0.f, maxSp);
    v.speed = std::max(0.f, v.speed - friction * dt * 0.15f);
    float turnScale = clampf(v.speed / 6.f, 0.f, 1.f);
    v.yaw += v.aiSteer * turn * turnScale * dt;
    float fx = std::sin(v.yaw);
    float fz = std::cos(v.yaw);
    v.x += fx * v.speed * dt;
    v.z += fz * v.speed * dt;
    // soft bounds
    if (std::abs(v.x) > CITY_HALF - 4.f || std::abs(v.z) > CITY_HALF - 4.f) {
        v.yaw += 1.2f;
        v.x = clampf(v.x, -CITY_HALF + 4.f, CITY_HALF - 4.f);
        v.z = clampf(v.z, -CITY_HALF + 4.f, CITY_HALF - 4.f);
        v.speed *= 0.5f;
    }
}

static VehicleSnap toSnap(const Vehicle& v) {
    VehicleSnap s{};
    s.id = v.id;
    s.x = v.x; s.y = v.y; s.z = v.z;
    s.yaw = v.yaw; s.speed = v.speed;
    s.owner = v.owner;
    s.r = v.r; s.g = v.g; s.b = v.b;
    return s;
}

static PlayerSnap toSnap(const Client& c) {
    PlayerSnap s{};
    s.id = c.id;
    s.x = c.x; s.y = c.y; s.z = c.z;
    s.yaw = c.yaw;
    s.in_vehicle = c.in_vehicle;
    s.vehicle_id = c.vehicle_id;
    setName(s.name, c.name);
    s.cr = c.cr; s.cg = c.cg; s.cb = c.cb;
    return s;
}

static std::vector<uint8_t> buildWelcome(uint32_t playerId, const std::vector<Vehicle>& vehs) {
    std::vector<uint8_t> payload;
    ServerWelcome w{};
    w.player_id = playerId;
    w.vehicle_count = static_cast<uint16_t>(vehs.size());
    appendPod(payload, w);
    for (const auto& v : vehs) appendPod(payload, toSnap(v));
    return payload;
}

static std::vector<uint8_t> buildSnapshot(uint32_t tick,
                                          const std::vector<Client>& clients,
                                          const std::vector<Vehicle>& vehs) {
    std::vector<uint8_t> payload;
    Snapshot snap{};
    snap.tick = tick;
    uint16_t pc = 0;
    for (const auto& c : clients) if (c.alive && c.welcomed) pc++;
    snap.player_count = pc;
    snap.vehicle_count = static_cast<uint16_t>(vehs.size());
    appendPod(payload, snap);
    for (const auto& c : clients) {
        if (!c.alive || !c.welcomed) continue;
        appendPod(payload, toSnap(c));
    }
    for (const auto& v : vehs) appendPod(payload, toSnap(v));
    return payload;
}

static void freeVehiclesOwnedBy(std::vector<Vehicle>& vehs, int32_t playerId) {
    for (auto& v : vehs) {
        if (v.owner == playerId) {
            v.owner = -1;
            v.speed = 0;
        }
    }
}

static int findVehicle(std::vector<Vehicle>& vehs, int32_t id) {
    for (size_t i = 0; i < vehs.size(); ++i)
        if (vehs[i].id == id) return static_cast<int>(i);
    return -1;
}

static uint32_t nextPlayerId = 1;

static float playerColor(uint32_t id, int channel) {
    // stable distinct-ish colors per id
    float h = std::fmod(id * 0.37f, 1.f);
    float s = 0.65f, v = 0.95f;
    float c = v * s;
    float x = c * (1.f - std::fabs(std::fmod(h * 6.f, 2.f) - 1.f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    int sector = static_cast<int>(h * 6.f) % 6;
    switch (sector) {
        case 0: r=c; g=x; b=0; break;
        case 1: r=x; g=c; b=0; break;
        case 2: r=0; g=c; b=x; break;
        case 3: r=0; g=x; b=c; break;
        case 4: r=x; g=0; b=c; break;
        default: r=c; g=0; b=x; break;
    }
    float rgb[3] = {r + m, g + m, b + m};
    return rgb[channel];
}

int main(int argc, char** argv) {
    const char* host = "0.0.0.0";
    int port = DEFAULT_PORT;
    if (argc == 2) {
        port = std::atoi(argv[1]);
    } else if (argc >= 3) {
        host = argv[1];
        port = std::atoi(argv[2]);
    }
    if (port <= 0) port = DEFAULT_PORT;

    std::srand(static_cast<unsigned>(std::time(nullptr)));

    int listenFd = createListenSocket(host, port);
    if (listenFd < 0) {
        std::cerr << "Failed to listen on " << host << ":" << port << "\n";
        return 1;
    }

    std::vector<Vehicle> vehicles;
    spawnVehicles(vehicles);
    std::vector<Client> clients;
    clients.reserve(MAX_PLAYERS);

    uint32_t tick = 0;
    auto t0 = std::chrono::steady_clock::now();
    double lastSnap = 0.0;
    double lastAi = 0.0;

    std::cout << "Leonida Lights server listening on " << host << ":" << port << "\n"
              << "Vehicles: " << vehicles.size() << "\n"
              << "Cloudflare TCP tunnel should target tcp://localhost:" << port << "\n"
              << "Clients: ./gta6_clone <host> " << port << "\n";

    for (;;) {
        auto nowTp = std::chrono::steady_clock::now();
        double now = std::chrono::duration<double>(nowTp - t0).count();

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(listenFd, &rfds);
        int maxfd = listenFd;
        for (auto& c : clients) {
            if (!c.alive) continue;
            FD_SET(c.fd, &rfds);
            if (c.fd > maxfd) maxfd = c.fd;
        }
        timeval tv{0, 2000}; // 2ms
        int sel = select(maxfd + 1, &rfds, nullptr, nullptr, &tv);
        (void)sel;

        // Accept
        if (FD_ISSET(listenFd, &rfds)) {
            for (;;) {
                sockaddr_in ca{};
                socklen_t cl = sizeof(ca);
                int cfd = ::accept(listenFd, reinterpret_cast<sockaddr*>(&ca), &cl);
                if (cfd < 0) break;
                if (clients.size() >= MAX_PLAYERS) {
                    char msg[64]{};
                    std::strncpy(msg, "Server full", sizeof(msg) - 1);
                    sendPacket(cfd, S_REJECT, msg, sizeof(msg));
                    ::close(cfd);
                    continue;
                }
                setNonBlocking(cfd);
                setNoDelay(cfd);
                Client clt;
                clt.fd = cfd;
                clt.id = nextPlayerId++;
                clt.alive = true;
                clt.welcomed = false;
                clt.lastHeard = now;
                // spawn on origin road intersection, spread along the asphalt
                int slot = static_cast<int>(clients.size());
                clt.x = (slot % 5 - 2) * 3.f;          // stay within road width
                clt.z = (slot / 5) * 3.f;              // along N/S road
                clt.y = 0;
                clt.yaw = 0;
                clt.cr = playerColor(clt.id, 0);
                clt.cg = playerColor(clt.id, 1);
                clt.cb = playerColor(clt.id, 2);
                char ip[64];
                inet_ntop(AF_INET, &ca.sin_addr, ip, sizeof(ip));
                std::cout << "[+] connection fd=" << cfd << " id=" << clt.id
                          << " from " << ip << ":" << ntohs(ca.sin_port) << "\n";
                clients.push_back(std::move(clt));
            }
        }

        // Read clients
        for (auto& c : clients) {
            if (!c.alive) continue;
            if (FD_ISSET(c.fd, &rfds) || true) {
                // always try recv (non-blocking)
                if (!recvInto(c.fd, c.in)) {
                    std::cout << "[-] disconnect id=" << c.id << " name=" << c.name << "\n";
                    freeVehiclesOwnedBy(vehicles, static_cast<int32_t>(c.id));
                    ::close(c.fd);
                    c.alive = false;
                    continue;
                }
            }

            uint16_t type = 0;
            std::vector<uint8_t> payload;
            while (c.in.pop(type, payload)) {
                c.lastHeard = now;
                if (type == C_HELLO) {
                    ClientHello hello{};
                    if (payload.size() >= sizeof(ClientHello))
                        std::memcpy(&hello, payload.data(), sizeof(hello));
                    hello.name[MAX_NAME - 1] = 0;
                    if (hello.name[0] && std::strcmp(hello.name, "Player") != 0)
                        c.name = hello.name;
                    else {
                        // Assign a random Vice-style name
                        static const char* adj[] = {
                            "Neon","Palm","Coral","Vice","Sunset","Turbo","Chrome","Miami"
                        };
                        static const char* noun[] = {
                            "Rider","Ace","Kid","Wolf","Fox","Drift","Pulse","Wave"
                        };
                        char gen[MAX_NAME];
                        std::snprintf(gen, sizeof(gen), "%s%s%u",
                                      adj[c.id % 8], noun[(c.id * 3) % 8],
                                      10u + (c.id * 7u) % 90u);
                        c.name = gen;
                    }
                    auto wel = buildWelcome(c.id, vehicles);
                    if (!sendPacket(c.fd, S_WELCOME, wel)) {
                        c.alive = false;
                        ::close(c.fd);
                        freeVehiclesOwnedBy(vehicles, static_cast<int32_t>(c.id));
                        break;
                    }
                    c.welcomed = true;
                    std::cout << "[=] welcome id=" << c.id << " name=" << c.name << "\n";
                } else if (type == C_STATE && c.welcomed) {
                    if (payload.size() < sizeof(ClientState)) continue;
                    ClientState st{};
                    std::memcpy(&st, payload.data(), sizeof(st));
                    c.x = st.x; c.y = st.y; c.z = st.z;
                    c.yaw = st.yaw;
                    c.in_vehicle = st.in_vehicle ? 1 : 0;
                    c.vehicle_id = st.vehicle_id;

                    if (c.in_vehicle && c.vehicle_id >= 0) {
                        int vi = findVehicle(vehicles, c.vehicle_id);
                        if (vi >= 0) {
                            // Auto-claim if free (ENTER may have been lost / raced with snapshot)
                            if (vehicles[vi].owner < 0) {
                                freeVehiclesOwnedBy(vehicles, static_cast<int32_t>(c.id));
                                vehicles[vi].owner = static_cast<int32_t>(c.id);
                                std::cout << "[=] player " << c.id
                                          << " auto-claimed vehicle " << c.vehicle_id << "\n";
                            }
                            if (vehicles[vi].owner == static_cast<int32_t>(c.id)) {
                                vehicles[vi].x = st.vx;
                                vehicles[vi].y = st.vy;
                                vehicles[vi].z = st.vz;
                                vehicles[vi].yaw = st.v_yaw;
                                vehicles[vi].speed = st.v_speed;
                            } else {
                                // Someone else has this car — clear client's claim
                                c.in_vehicle = 0;
                                c.vehicle_id = -1;
                            }
                        }
                    } else if (!c.in_vehicle) {
                        // Ensure we don't keep ownership after client exited without C_EXIT
                        // (only clear if they were sole owner of something — handled by C_EXIT)
                    }
                } else if (type == C_ENTER && c.welcomed) {
                    if (payload.size() < sizeof(int32_t)) continue;
                    int32_t vid = 0;
                    std::memcpy(&vid, payload.data(), sizeof(vid));
                    int vi = findVehicle(vehicles, vid);
                    if (vi < 0) continue;
                    // Distance vs last known client pos OR vehicle (lenient — client is authority)
                    float dx = vehicles[vi].x - c.x;
                    float dz = vehicles[vi].z - c.z;
                    float dist = std::sqrt(dx * dx + dz * dz);
                    bool free = vehicles[vi].owner < 0;
                    bool alreadyUs = vehicles[vi].owner == static_cast<int32_t>(c.id);
                    if ((free || alreadyUs) && dist < 40.f) {
                        freeVehiclesOwnedBy(vehicles, static_cast<int32_t>(c.id));
                        vehicles[vi].owner = static_cast<int32_t>(c.id);
                        vehicles[vi].speed = 0;
                        c.in_vehicle = 1;
                        c.vehicle_id = vid;
                        // Don't snap player on server — client drives
                        std::cout << "[=] player " << c.id << " entered vehicle " << vid << "\n";
                    } else {
                        std::cout << "[!] enter denied id=" << c.id << " veh=" << vid
                                  << " free=" << free << " dist=" << dist << "\n";
                    }
                } else if (type == C_EXIT && c.welcomed) {
                    freeVehiclesOwnedBy(vehicles, static_cast<int32_t>(c.id));
                    c.in_vehicle = 0;
                    c.vehicle_id = -1;
                } else if (type == C_PING) {
                    sendPacket(c.fd, S_PONG, nullptr, 0);
                }
            }
        }

        // Timeout idle (120s without packets after welcome)
        for (auto& c : clients) {
            if (!c.alive) continue;
            if (now - c.lastHeard > 120.0) {
                std::cout << "[-] timeout id=" << c.id << "\n";
                freeVehiclesOwnedBy(vehicles, static_cast<int32_t>(c.id));
                ::close(c.fd);
                c.alive = false;
            }
        }

        // Compact dead clients occasionally
        clients.erase(std::remove_if(clients.begin(), clients.end(),
            [](const Client& c) { return !c.alive; }), clients.end());

        // AI tick ~30Hz
        if (now - lastAi >= 1.0 / 30.0) {
            float dt = static_cast<float>(now - lastAi);
            if (lastAi == 0.0) dt = 1.f / 30.f;
            lastAi = now;
            for (auto& v : vehicles) updateAIVehicle(v, dt);
        }

        // Broadcast snapshot ~20Hz
        if (now - lastSnap >= 1.0 / 20.0) {
            lastSnap = now;
            ++tick;
            auto snap = buildSnapshot(tick, clients, vehicles);
            for (auto& c : clients) {
                if (!c.alive || !c.welcomed) continue;
                if (!sendPacket(c.fd, S_SNAPSHOT, snap)) {
                    std::cout << "[-] send fail id=" << c.id << "\n";
                    freeVehiclesOwnedBy(vehicles, static_cast<int32_t>(c.id));
                    ::close(c.fd);
                    c.alive = false;
                }
            }
        }
    }

    ::close(listenFd);
    return 0;
}
