#pragma once
// Leonida Lights multiplayer protocol (TCP, length-prefixed binary, little-endian)
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>

namespace llmp {

static constexpr uint32_t MAGIC = 0x504D4C4Cu; // 'LLMP'
static constexpr uint16_t MAX_NAME = 16;
static constexpr uint16_t MAX_PLAYERS = 32;
static constexpr uint16_t MAX_VEHICLES = 48;
static constexpr int DEFAULT_PORT = 9043;

enum MsgType : uint16_t {
    C_HELLO    = 1,  // ClientHello
    S_WELCOME  = 2,  // ServerWelcome
    C_STATE    = 3,  // ClientState
    S_SNAPSHOT = 4,  // Snapshot
    C_ENTER    = 5,  // int32 vehicle_id
    C_EXIT     = 6,  // empty
    S_REJECT   = 7,  // char reason[64]
    C_PING     = 8,
    S_PONG     = 9,
};

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint16_t type;
    uint16_t size; // payload bytes following header
};

struct ClientHello {
    char name[MAX_NAME];
};

struct ServerWelcome {
    uint32_t player_id;
    uint16_t vehicle_count;
    // followed by vehicle_count * VehicleSnap
};

struct VehicleSnap {
    int32_t id;
    float x, y, z;
    float yaw, speed;
    int32_t owner; // -1 free, else player_id
    float r, g, b;
};

struct PlayerSnap {
    uint32_t id;
    float x, y, z;
    float yaw;
    uint8_t in_vehicle;
    int32_t vehicle_id; // -1 none
    char name[MAX_NAME];
    float cr, cg, cb; // body color
};

struct ClientState {
    float x, y, z;
    float yaw;
    uint8_t in_vehicle;
    int32_t vehicle_id;
    // If in vehicle and we own it, include vehicle transform:
    float vx, vy, vz;
    float v_yaw, v_speed;
};

struct Snapshot {
    uint32_t tick;
    uint16_t player_count;
    uint16_t vehicle_count;
    // followed by player_count * PlayerSnap + vehicle_count * VehicleSnap
};
#pragma pack(pop)

inline void writeHeader(std::vector<uint8_t>& out, uint16_t type, uint16_t payloadSize) {
    Header h{};
    h.magic = MAGIC;
    h.type = type;
    h.size = payloadSize;
    const uint8_t* p = reinterpret_cast<const uint8_t*>(&h);
    out.insert(out.end(), p, p + sizeof(h));
}

inline void appendBytes(std::vector<uint8_t>& out, const void* data, size_t n) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + n);
}

template <typename T>
inline void appendPod(std::vector<uint8_t>& out, const T& v) {
    appendBytes(out, &v, sizeof(T));
}

// Packet reader: accumulates TCP stream bytes
struct PacketBuffer {
    std::vector<uint8_t> buf;

    void push(const uint8_t* data, size_t n) {
        buf.insert(buf.end(), data, data + n);
    }

    // Returns true if a full packet was popped into type/payload
    bool pop(uint16_t& type, std::vector<uint8_t>& payload) {
        if (buf.size() < sizeof(Header)) return false;
        Header h{};
        std::memcpy(&h, buf.data(), sizeof(h));
        if (h.magic != MAGIC) {
            // resync: drop one byte
            buf.erase(buf.begin());
            return false;
        }
        if (h.size > 65500) {
            buf.erase(buf.begin());
            return false;
        }
        size_t total = sizeof(Header) + h.size;
        if (buf.size() < total) return false;
        type = h.type;
        payload.assign(buf.begin() + sizeof(Header), buf.begin() + total);
        buf.erase(buf.begin(), buf.begin() + total);
        return true;
    }
};

inline void setName(char* dst, const std::string& name, size_t n = MAX_NAME) {
    std::memset(dst, 0, n);
    if (name.empty()) {
        std::strncpy(dst, "Player", n - 1);
        return;
    }
    std::strncpy(dst, name.c_str(), n - 1);
}

} // namespace llmp
