// Leonida Lights — multiplayer client (OpenGL 3.3 + GLFW + GLEW)
//
//   ./gta6_clone                         # connect 127.0.0.1:9043
//   ./gta6_clone <host> [port]           # connect remote (Cloudflare TCP tunnel host)
//   ./gta6_clone --offline               # single-player, no server
//
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <string>
#include <iostream>
#include <algorithm>
#include <unordered_map>
#include <cstring>

#include "math.hpp"
#include "shader.hpp"
#include "mesh.hpp"
#include "camera.hpp"
#include "world.hpp"
#include "vehicle.hpp"
#include "player.hpp"
#include "protocol.hpp"
#include "net.hpp"

static Camera gCam;
static bool gKeys[512] = {};
static bool gMouseCaptured = true;
static bool gPaused = false;
static bool gEWasDown = false;
static bool gHWasDown = false;
static bool gEscWasDown = false;
static bool gShowHelp = true;
static bool gMouseLeftClick = false;
static int gWinW = 1920, gWinH = 1080;

static const char* VERT_SRC = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec3 aCol;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProj;
out vec3 vCol;
out vec3 vNrm;
out vec3 vWorld;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vWorld = w.xyz;
    vNrm = mat3(uModel) * aNrm;
    vCol = aCol;
    gl_Position = uProj * uView * w;
}
)";

static const char* FRAG_SRC = R"(
#version 330 core
in vec3 vCol;
in vec3 vNrm;
in vec3 vWorld;
uniform vec3 uLightDir;
uniform vec3 uLightColor;
uniform vec3 uAmbient;
uniform vec3 uCamPos;
uniform float uTime;
uniform int uMode;
out vec4 FragColor;
void main() {
    vec3 n = normalize(vNrm);
    float ndl = max(dot(n, normalize(-uLightDir)), 0.0);
    float wrap = ndl * 0.7 + 0.3;
    vec3 col = vCol;
    if (uMode == 1) {
        float w = sin(vWorld.x * 0.15 + uTime * 1.2) * cos(vWorld.z * 0.12 + uTime * 0.9);
        col = mix(col, vec3(0.2, 0.75, 0.9), 0.25 + 0.15 * w);
        wrap = 0.7 + 0.3 * wrap;
    }
    float rim = pow(1.0 - max(dot(n, normalize(uCamPos - vWorld)), 0.0), 2.5);
    vec3 rimCol = vec3(1.0, 0.45, 0.55) * rim * 0.18;
    float dist = length(uCamPos - vWorld);
    float fog = clamp((dist - 90.0) / 280.0, 0.0, 0.55);
    vec3 fogCol = vec3(0.95, 0.68, 0.58);
    vec3 lit = col * (uAmbient + uLightColor * wrap) + rimCol;
    lit = mix(lit, fogCol, fog);
    FragColor = vec4(lit, uMode == 1 ? 0.92 : 1.0);
}
)";

static const char* HUD_VERT = R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec3 aCol;
out vec3 vCol;
void main() {
    vCol = aCol;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";

static const char* HUD_FRAG = R"(
#version 330 core
in vec3 vCol;
uniform float uAlpha;
out vec4 FragColor;
void main() { FragColor = vec4(vCol, uAlpha); }
)";

static void setCursorMode(GLFWwindow* win, bool capture) {
    gMouseCaptured = capture;
    glfwSetInputMode(win, GLFW_CURSOR,
        capture ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}
static void enterPause(GLFWwindow* win) { gPaused = true; setCursorMode(win, false); }
static void leavePause(GLFWwindow* win) { gPaused = false; setCursorMode(win, true); }

static void framebuffer_size_callback(GLFWwindow*, int w, int h) {
    if (w <= 0 || h <= 0) return;
    gWinW = w; gWinH = h;
    glViewport(0, 0, w, h);
}
static void key_callback(GLFWwindow* win, int key, int, int action, int) {
    if (key >= 0 && key < 512) {
        if (action == GLFW_PRESS) gKeys[key] = true;
        if (action == GLFW_RELEASE) gKeys[key] = false;
    }
    if (key == GLFW_KEY_TAB && action == GLFW_PRESS && !gPaused)
        setCursorMode(win, !gMouseCaptured);
}
static void cursor_callback(GLFWwindow*, double xpos, double ypos) {
    static double lastX = xpos, lastY = ypos;
    static bool first = true;
    if (first) { lastX = xpos; lastY = ypos; first = false; return; }
    double dx = xpos - lastX, dy = ypos - lastY;
    lastX = xpos; lastY = ypos;
    if (gMouseCaptured && !gPaused)
        gCam.orbit(static_cast<float>(dx), static_cast<float>(dy));
}
static void scroll_callback(GLFWwindow*, double, double yoff) {
    if (!gPaused) gCam.zoom(static_cast<float>(yoff));
}
static void mouse_button_callback(GLFWwindow*, int button, int action, int) {
    if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
        gMouseLeftClick = true;
}

struct HudMesh {
    GLuint vao = 0, vbo = 0;
    int count = 0;
    void ensure() {
        if (!vao) { glGenVertexArrays(1, &vao); glGenBuffers(1, &vbo); }
    }
    void upload(const std::vector<float>& data) {
        ensure();
        count = static_cast<int>(data.size() / 5);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
    void draw() const {
        glBindVertexArray(vao);
        glDrawArrays(GL_TRIANGLES, 0, count);
        glBindVertexArray(0);
    }
};

static void pushQuad(std::vector<float>& d,
                     float x0, float y0, float x1, float y1,
                     float r, float g, float b) {
    auto v = [&](float x, float y) {
        d.push_back(x); d.push_back(y);
        d.push_back(r); d.push_back(g); d.push_back(b);
    };
    v(x0,y0); v(x1,y0); v(x1,y1);
    v(x0,y0); v(x1,y1); v(x0,y1);
}

static void cursorToNdc(GLFWwindow* win, float& nx, float& ny) {
    double cx, cy;
    glfwGetCursorPos(win, &cx, &cy);
    int fbw, fbh, ww, wh;
    glfwGetFramebufferSize(win, &fbw, &fbh);
    glfwGetWindowSize(win, &ww, &wh);
    if (ww <= 0 || wh <= 0 || fbw <= 0 || fbh <= 0) { nx = 0; ny = 0; return; }
    double fx = cx * (double)fbw / (double)ww;
    double fy = cy * (double)fbh / (double)wh;
    nx = (float)(fx / fbw) * 2.f - 1.f;
    ny = 1.f - (float)(fy / fbh) * 2.f;
}
static bool pointInRect(float px, float py, float x0, float y0, float x1, float y1) {
    return px >= x0 && px <= x1 && py >= y0 && py <= y1;
}

// ---- Multiplayer client state ----
struct RemotePlayer {
    uint32_t id = 0;
    Vec3 pos{};
    float yaw = 0;
    bool inVehicle = false;
    int32_t vehicleId = -1;
    char name[llmp::MAX_NAME]{};
    float cr = 1, cg = 1, cb = 1;
    Mesh mesh;
    bool hasMesh = false;

    void ensureMesh() {
        if (hasMesh) return;
        mesh = makePedMesh(cr, cg, cb);
        hasMesh = true;
    }
    void destroy() {
        if (hasMesh) mesh.destroy();
        hasMesh = false;
    }
};

struct NetClient {
    bool offline = false;
    bool connected = false;
    bool welcomed = false;
    int fd = -1;
    uint32_t localId = 0;
    llmp::PacketBuffer in;
    double lastSend = 0;
    double lastRecv = 0;
    std::string status = "offline";
    std::string host;
    int port = llmp::DEFAULT_PORT;
    std::string playerName = "Player";

    bool connect() {
        if (offline) { status = "offline"; return true; }
        std::string err;
        fd = llmp::connectTcp(host.c_str(), port, err);
        if (fd < 0) {
            status = "connect failed: " + err;
            connected = false;
            return false;
        }
        connected = true;
        status = "connected, handshaking...";
        llmp::ClientHello hello{};
        llmp::setName(hello.name, playerName);
        if (!llmp::sendPacket(fd, llmp::C_HELLO, &hello, sizeof(hello))) {
            status = "hello send failed";
            ::close(fd); fd = -1; connected = false;
            return false;
        }
        lastRecv = glfwGetTime();
        return true;
    }

    void disconnect() {
        if (fd >= 0) { ::close(fd); fd = -1; }
        connected = false;
        welcomed = false;
    }

    void sendState(const Player& player, const std::vector<Vehicle>& cars) {
        if (!connected || !welcomed) return;
        llmp::ClientState st{};
        st.x = player.pos.x; st.y = player.pos.y; st.z = player.pos.z;
        st.yaw = player.yaw;
        st.in_vehicle = player.inVehicle ? 1 : 0;
        st.vehicle_id = player.inVehicle ? player.vehicleIndex : -1;
        // vehicleIndex on client is array index; map to server vehicle id
        if (player.inVehicle && player.vehicleIndex >= 0 &&
            player.vehicleIndex < (int)cars.size()) {
            // We store server id in Vehicle via a parallel - use index as id (same spawn order)
            st.vehicle_id = player.vehicleIndex;
            const Vehicle& v = cars[player.vehicleIndex];
            st.vx = v.pos.x; st.vy = v.pos.y; st.vz = v.pos.z;
            st.v_yaw = v.yaw; st.v_speed = v.speed;
        }
        llmp::sendPacket(fd, llmp::C_STATE, &st, sizeof(st));
    }

    void sendEnter(int32_t vehicleId) {
        if (!connected || !welcomed) return;
        llmp::sendPacket(fd, llmp::C_ENTER, &vehicleId, sizeof(vehicleId));
    }
    void sendExit() {
        if (!connected || !welcomed) return;
        llmp::sendPacket(fd, llmp::C_EXIT, nullptr, 0);
    }
};

static int findCarIndexById(const std::vector<Vehicle>& cars, int32_t id) {
    // vehicle id == index in our spawn scheme
    if (id >= 0 && id < (int)cars.size()) return id;
    return -1;
}

static void applyVehicleSnap(Vehicle& car, const llmp::VehicleSnap& s, bool forceTransform) {
    car.r = s.r; car.g = s.g; car.b = s.b;
    if (forceTransform) {
        car.pos = Vec3(s.x, s.y, s.z);
        car.yaw = s.yaw;
        car.speed = s.speed;
    }
    car.occupied = (s.owner >= 0);
    car.ai = (s.owner < 0);
}

int main(int argc, char** argv) {
    NetClient net;
    net.host = "127.0.0.1";
    net.port = llmp::DEFAULT_PORT;

    // Args: --offline | --name X | host [port]
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--offline" || a == "-o") {
            net.offline = true;
        } else if ((a == "--name" || a == "-n") && i + 1 < argc) {
            net.playerName = argv[++i];
        } else if (a == "--help" || a == "-h") {
            std::cout << "Usage: " << argv[0] << " [--offline] [--name Name] [host] [port]\n"
                      << "  Default: connect to 127.0.0.1:" << llmp::DEFAULT_PORT << "\n"
                      << "  Server:  ./gta6_server   (0.0.0.0:" << llmp::DEFAULT_PORT << ")\n";
            return 0;
        } else if (a[0] != '-') {
            net.host = a;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                net.port = std::atoi(argv[++i]);
                if (net.port <= 0) net.port = llmp::DEFAULT_PORT;
            }
        }
    }
    if (const char* envName = std::getenv("LEONIDA_NAME"))
        net.playerName = envName;

    if (!glfwInit()) {
        std::cerr << "Failed to init GLFW\n";
        return 1;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 8);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    int monW = 1920, monH = 1080;
    if (monitor) {
        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        if (mode) { monW = mode->width; monH = mode->height; }
    }
    gWinW = std::max(1280, monW - 80);
    gWinH = std::max(720, monH - 100);

    std::string winTitle = "Leonida Lights";
    GLFWwindow* window = glfwCreateWindow(gWinW, gWinH, winTitle.c_str(), nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window\n";
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1);
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetCursorPosCallback(window, cursor_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    setCursorMode(window, true);

    int fbw = 0, fbh = 0;
    glfwGetFramebufferSize(window, &fbw, &fbh);
    if (fbw > 0 && fbh > 0) {
        gWinW = fbw; gWinH = fbh;
        glViewport(0, 0, fbw, fbh);
    }

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to init GLEW\n";
        return 1;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.55f, 0.62f, 0.85f, 1.f);

    Shader worldSh; worldSh.id = makeProgram(VERT_SRC, FRAG_SRC);
    Shader hudSh; hudSh.id = makeProgram(HUD_VERT, HUD_FRAG);
    HudMesh hud;

    World world;
    world.generate(2026);

    Player player;
    // Always spawn on the origin road intersection (never inside a lot/building)
    {
        Vec3 spawn = world.findClearSpawn(0.f, 0.f, 0.5f);
        player.init(spawn);
        player.ensureFree(world);
    }
    gCam.yaw = 0.f;
    gCam.update(player.pos + Vec3(0, 1, 0), 0.f);

    std::vector<Vehicle> cars;
    auto addCar = [&](float x, float z, float yaw, float r, float g, float b, bool ai) {
        Vehicle v;
        // Snap to road, then resolve any leftover solid overlap
        Vec3 p = world.findClearSpawn(x, z, 1.5f);
        if (world.collides(p.x, p.z, 1.5f))
            p = world.resolveSolid(p.x, p.z, 1.5f);
        v.init(p, yaw, r, g, b, ai);
        cars.push_back(std::move(v));
    };

    // Offline default fleet — all on road centerlines (same layout as server)
    auto spawnDefaultCars = [&]() {
        cars.clear();
        // Parked along the N/S road through origin (x=0)
        addCar(0.f, 10.f, 0.f, 0.95f, 0.15f, 0.28f, false);
        addCar(0.f, 16.f, 3.14159f, 0.15f, 0.75f, 0.85f, false);
        addCar(World::BLOCK, 0.f, 1.57f, 1.f, 0.85f, 0.2f, false);
        addCar(-World::BLOCK, 0.f, -1.57f, 0.95f, 0.95f, 0.98f, false);
        addCar(0.f, -World::BLOCK, 0.f, 0.4f, 0.2f, 0.7f, false);
        const float carColors[][3] = {
            {0.8f,0.2f,0.2f},{0.2f,0.4f,0.8f},{0.2f,0.7f,0.3f},
            {0.9f,0.6f,0.1f},{0.6f,0.6f,0.65f},{0.1f,0.1f,0.12f}
        };
        for (int i = 0; i < 20; ++i) {
            int ri = (i % 5) - 2;
            float z = ((i / 5) - 2) * World::BLOCK; // on grid roads
            int ci = i % 6;
            float yaw = (i % 2) ? 0.f : 3.14159f;
            // Offset slightly within the road lane, not into lots
            addCar(ri * World::BLOCK + 1.2f, z, yaw,
                   carColors[ci][0], carColors[ci][1], carColors[ci][2], true);
        }
    };
    spawnDefaultCars();

    // Connect multiplayer
    if (!net.offline) {
        std::cout << "Connecting to " << net.host << ":" << net.port << " ...\n";
        if (!net.connect()) {
            std::cerr << "WARN: " << net.status << " — falling back to offline\n";
            net.offline = true;
            net.status = "offline (connect failed)";
        }
    } else {
        net.status = "offline";
    }

    // Wait briefly for WELCOME if online
    if (net.connected) {
        double tWait = glfwGetTime();
        while (!net.welcomed && glfwGetTime() - tWait < 3.0) {
            if (!llmp::recvInto(net.fd, net.in)) break;
            uint16_t type; std::vector<uint8_t> payload;
            while (net.in.pop(type, payload)) {
                if (type == llmp::S_WELCOME && payload.size() >= sizeof(llmp::ServerWelcome)) {
                    llmp::ServerWelcome w{};
                    std::memcpy(&w, payload.data(), sizeof(w));
                    net.localId = w.player_id;
                    net.welcomed = true;
                    net.status = "online id=" + std::to_string(net.localId);

                    size_t off = sizeof(llmp::ServerWelcome);
                    // Rebuild cars from server
                    for (auto& c : cars) c.destroy();
                    cars.clear();
                    for (uint16_t i = 0; i < w.vehicle_count; ++i) {
                        if (off + sizeof(llmp::VehicleSnap) > payload.size()) break;
                        llmp::VehicleSnap s{};
                        std::memcpy(&s, payload.data() + off, sizeof(s));
                        off += sizeof(s);
                        Vehicle v;
                        v.init(Vec3(s.x, s.y, s.z), s.yaw, s.r, s.g, s.b, s.owner < 0);
                        v.speed = s.speed;
                        v.occupied = s.owner >= 0;
                        // Ensure vector index matches server vehicle id
                        while ((int)cars.size() < s.id) {
                            Vehicle pad;
                            pad.init(Vec3(0, -100, 0), 0, 0.2f, 0.2f, 0.2f, false);
                            cars.push_back(std::move(pad));
                        }
                        if ((int)cars.size() == s.id)
                            cars.push_back(std::move(v));
                        else if (s.id >= 0 && s.id < (int)cars.size()) {
                            cars[s.id].destroy();
                            cars[s.id] = std::move(v);
                        }
                    }
                    // Spawn on road, spread by player id, never inside a building
                    float sx = ((int)(net.localId % 5) - 2) * 3.f;
                    float sz = ((int)(net.localId / 5) % 5) * 3.f;
                    player.pos = world.findClearSpawn(sx, sz, 0.5f);
                    player.ensureFree(world);
                    std::cout << "Joined as player " << net.localId
                              << " (" << net.playerName << ") vehicles=" << cars.size()
                              << " spawn (" << player.pos.x << ", " << player.pos.z << ")\n";
                } else if (type == llmp::S_REJECT) {
                    std::string reason(payload.begin(), payload.end());
                    std::cerr << "Rejected: " << reason << "\n";
                    net.disconnect();
                    net.offline = true;
                    net.status = "rejected";
                    spawnDefaultCars();
                }
            }
            // tiny sleep via glfw
            glfwPollEvents();
        }
        if (!net.welcomed && net.connected) {
            std::cerr << "No WELCOME from server — offline mode\n";
            net.disconnect();
            net.offline = true;
            net.status = "offline (no welcome)";
            spawnDefaultCars();
        }
    }

    Mesh pedMeshes[4];
    pedMeshes[0] = makePedMesh(0.9f, 0.4f, 0.5f);
    pedMeshes[1] = makePedMesh(0.3f, 0.6f, 0.8f);
    pedMeshes[2] = makePedMesh(0.95f, 0.9f, 0.4f);
    pedMeshes[3] = makePedMesh(0.4f, 0.8f, 0.5f);
    std::vector<Pedestrian> peds;
    if (net.offline) {
        for (int i = 0; i < 40; ++i) {
            Pedestrian p;
            p.pos = Vec3(((std::rand() % 200) - 100) * 0.9f, 0, ((std::rand() % 200) - 100) * 0.9f);
            p.yaw = (std::rand() % 628) / 100.f;
            p.mesh = &pedMeshes[i % 4];
            if (!world.collides(p.pos.x, p.pos.z, 0.4f))
                peds.push_back(p);
        }
    }

    std::unordered_map<uint32_t, RemotePlayer> remotes;
    // ownership cache from last snapshot
    std::vector<int32_t> vehOwner; // per car index

    double lastTime = glfwGetTime();
    Vec3 missionTarget(0.f, 0.f, -World::CITY_HALF + 4.f);
    bool missionDone = false;
    int stars = 0;

    const float btnResume[4] = {-0.22f, 0.08f, 0.22f, 0.22f};
    const float btnQuit[4]   = {-0.22f, -0.18f, 0.22f, -0.04f};

    std::cout << "\n=== Leonida Lights Multiplayer ===\n"
              << "Mode: " << net.status << "\n"
              << "WASD move | Shift run | Mouse look | E enter/exit | Esc menu\n"
              << "R unstuck (teleport to nearest road) | Online players = colored peds\n\n";

    while (!glfwWindowShouldClose(window)) {
        double now = glfwGetTime();
        float dt = static_cast<float>(now - lastTime);
        lastTime = now;
        if (dt > 0.05f) dt = 0.05f;

        glfwPollEvents();
        bool click = gMouseLeftClick;
        gMouseLeftClick = false;

        // Network pump
        if (net.connected) {
            if (!llmp::recvInto(net.fd, net.in)) {
                std::cerr << "Disconnected from server\n";
                net.disconnect();
                net.status = "disconnected";
                // keep playing offline with current world
                net.offline = true;
                for (auto& c : cars) { c.ai = !c.occupied; }
            } else {
                uint16_t type; std::vector<uint8_t> payload;
                while (net.in.pop(type, payload)) {
                    net.lastRecv = now;
                    if (type == llmp::S_SNAPSHOT && payload.size() >= sizeof(llmp::Snapshot)) {
                        llmp::Snapshot snap{};
                        std::memcpy(&snap, payload.data(), sizeof(snap));
                        size_t off = sizeof(llmp::Snapshot);

                        std::unordered_map<uint32_t, bool> seen;
                        for (uint16_t i = 0; i < snap.player_count; ++i) {
                            if (off + sizeof(llmp::PlayerSnap) > payload.size()) break;
                            llmp::PlayerSnap ps{};
                            std::memcpy(&ps, payload.data() + off, sizeof(ps));
                            off += sizeof(ps);
                            if (ps.id == net.localId) {
                                // authority soft-correct if desynced badly
                                continue;
                            }
                            seen[ps.id] = true;
                            auto& rp = remotes[ps.id];
                            if (rp.id != ps.id) {
                                rp.destroy();
                                rp.id = ps.id;
                                rp.hasMesh = false;
                            }
                            rp.pos = Vec3(ps.x, ps.y, ps.z);
                            rp.yaw = ps.yaw;
                            rp.inVehicle = ps.in_vehicle != 0;
                            rp.vehicleId = ps.vehicle_id;
                            std::memcpy(rp.name, ps.name, llmp::MAX_NAME);
                            rp.cr = ps.cr; rp.cg = ps.cg; rp.cb = ps.cb;
                            rp.ensureMesh();
                        }
                        // drop missing
                        for (auto it = remotes.begin(); it != remotes.end(); ) {
                            if (!seen.count(it->first)) {
                                it->second.destroy();
                                it = remotes.erase(it);
                            } else ++it;
                        }

                        vehOwner.assign(cars.size(), -1);
                        for (uint16_t i = 0; i < snap.vehicle_count; ++i) {
                            if (off + sizeof(llmp::VehicleSnap) > payload.size()) break;
                            llmp::VehicleSnap vs{};
                            std::memcpy(&vs, payload.data() + off, sizeof(vs));
                            off += sizeof(vs);
                            int idx = findCarIndexById(cars, vs.id);
                            if (idx < 0) continue;
                            if (idx >= (int)vehOwner.size()) vehOwner.resize(idx + 1, -1);
                            vehOwner[idx] = vs.owner;

                            bool localOwns = (vs.owner == (int32_t)net.localId);
                            // Apply remote / AI vehicles from server; keep local prediction if we own
                            applyVehicleSnap(cars[idx], vs, !localOwns);
                            cars[idx].ai = false; // server simulates AI
                            if (!localOwns && player.inVehicle && player.vehicleIndex == idx) {
                                // lost ownership
                                player.inVehicle = false;
                                player.vehicleIndex = -1;
                                cars[idx].occupied = vs.owner >= 0;
                            }
                            if (localOwns) {
                                cars[idx].occupied = true;
                                cars[idx].ai = false;
                            }
                        }
                    }
                }
            }
        }

        bool escDown = gKeys[GLFW_KEY_ESCAPE];
        if (escDown && !gEscWasDown) {
            if (gPaused) leavePause(window);
            else enterPause(window);
        }
        gEscWasDown = escDown;

        if (gPaused) {
            float nx, ny;
            cursorToNdc(window, nx, ny);
            bool hoverResume = pointInRect(nx, ny, btnResume[0], btnResume[1], btnResume[2], btnResume[3]);
            bool hoverQuit   = pointInRect(nx, ny, btnQuit[0], btnQuit[1], btnQuit[2], btnQuit[3]);
            if (click) {
                if (hoverResume) leavePause(window);
                if (hoverQuit) glfwSetWindowShouldClose(window, 1);
            }
            if (gKeys[GLFW_KEY_ENTER] || gKeys[GLFW_KEY_KP_ENTER])
                leavePause(window);
        }

        if (!gPaused) {
            if (gKeys[GLFW_KEY_H] && !gHWasDown) gShowHelp = !gShowHelp;
            gHWasDown = gKeys[GLFW_KEY_H];

            bool eDown = gKeys[GLFW_KEY_E];
            if (eDown && !gEWasDown) {
                if (player.inVehicle) {
                    int idx = player.vehicleIndex;
                    player.exitVehicle(cars, world);
                    net.sendExit();
                    if (idx >= 0 && idx < (int)cars.size()) {
                        cars[idx].occupied = false;
                        cars[idx].ai = net.offline;
                    }
                } else {
                    // Prefer free vehicle (owner < 0 online)
                    int best = -1;
                    float bestD = 7.f;
                    for (int i = 0; i < (int)cars.size(); ++i) {
                        bool free = true;
                        if (!net.offline && i < (int)vehOwner.size())
                            free = (vehOwner[i] < 0);
                        else
                            free = !cars[i].occupied;
                        if (!free) continue;
                        float d = Vec3::distance(player.pos, cars[i].pos);
                        if (d < bestD) { bestD = d; best = i; }
                    }
                    if (best >= 0) {
                        player.inVehicle = true;
                        player.vehicleIndex = best;
                        cars[best].occupied = true;
                        cars[best].ai = false;
                        cars[best].speed = 0;
                        net.sendEnter(best); // vehicle id == index
                        std::cout << "Entered vehicle " << best << "\n";
                    } else {
                        std::cout << "No free vehicle nearby\n";
                    }
                }
            }
            gEWasDown = eDown;

            bool canDrive = player.inVehicle && player.vehicleIndex >= 0 &&
                            player.vehicleIndex < (int)cars.size();
            if (canDrive && !net.offline) {
                int idx = player.vehicleIndex;
                if (idx < (int)vehOwner.size() && vehOwner[idx] >= 0 &&
                    vehOwner[idx] != (int32_t)net.localId && net.welcomed) {
                    // not confirmed owner yet — still allow optimistic control briefly
                }
            }

            if (player.inVehicle && player.vehicleIndex >= 0 &&
                player.vehicleIndex < (int)cars.size()) {
                Vehicle& v = cars[player.vehicleIndex];
                float throttle = 0.f, steer = 0.f;
                if (gKeys[GLFW_KEY_W] || gKeys[GLFW_KEY_UP]) throttle += 1.f;
                if (gKeys[GLFW_KEY_S] || gKeys[GLFW_KEY_DOWN]) throttle -= 1.f;
                if (gKeys[GLFW_KEY_A] || gKeys[GLFW_KEY_LEFT]) steer -= 1.f;
                if (gKeys[GLFW_KEY_D] || gKeys[GLFW_KEY_RIGHT]) steer += 1.f;
                if (gKeys[GLFW_KEY_SPACE]) v.speed *= (1.f - 3.f * dt);
                v.update(dt, throttle, steer, world);
                player.pos = v.pos;
                player.yaw = v.yaw;

                for (auto& other : cars) {
                    if (&other == &v || other.occupied) continue;
                    float d = Vec3::distance(v.pos, other.pos);
                    if (d < 3.f && std::abs(v.speed) > 10.f) {
                        stars = std::min(5, stars + 1);
                        other.speed = -v.speed * 0.3f;
                        other.yaw += 0.5f;
                    }
                }
            } else {
                player.updateOnFoot(dt, gCam,
                    gKeys[GLFW_KEY_W] || gKeys[GLFW_KEY_UP],
                    gKeys[GLFW_KEY_S] || gKeys[GLFW_KEY_DOWN],
                    gKeys[GLFW_KEY_A] || gKeys[GLFW_KEY_LEFT],
                    gKeys[GLFW_KEY_D] || gKeys[GLFW_KEY_RIGHT],
                    gKeys[GLFW_KEY_LEFT_SHIFT] || gKeys[GLFW_KEY_RIGHT_SHIFT],
                    world);
                // R = emergency unstuck to nearest road
                if (gKeys[GLFW_KEY_R]) {
                    player.pos = world.findClearSpawn(player.pos.x, player.pos.z, 0.5f);
                    player.ensureFree(world);
                }
            }

            // Local AI only offline (online: server drives free cars)
            if (net.offline) {
                for (auto& c : cars) c.updateAI(dt, world);
                for (auto& p : peds) p.update(dt, world);
            }

            if (!missionDone) {
                Vec3 fp = player.focusPoint(cars);
                if (Vec3::distance(Vec3(fp.x, 0, fp.z), missionTarget) < 10.f) {
                    missionDone = true;
                    std::cout << "*** Mission complete: Reach South Beach! ***\n";
                }
            }

            Vec3 focus = player.focusPoint(cars);
            gCam.update(focus, player.focusYaw(cars));
            if (player.inVehicle && player.vehicleIndex >= 0 &&
                player.vehicleIndex < (int)cars.size()) {
                float ty = cars[player.vehicleIndex].yaw;
                float blend = 2.5f * dt;
                float dy = wrapAngle(ty - gCam.yaw);
                if (std::abs(cars[player.vehicleIndex].speed) > 2.f)
                    gCam.yaw += dy * blend * 0.4f;
            }

            // Send state ~20Hz
            if (net.connected && net.welcomed && now - net.lastSend >= 0.05) {
                net.lastSend = now;
                net.sendState(player, cars);
            }
        }

        float aspect = gWinH > 0 ? static_cast<float>(gWinW) / gWinH : 1.777f;
        Mat4 proj = Mat4::perspective(deg2rad(65.f), aspect, 0.12f, 500.f);
        Mat4 view = gCam.view();

        Vec3 lightDir = Vec3(0.35f, -0.75f, 0.4f).normalized();
        Vec3 lightColor(1.0f, 0.88f, 0.75f);
        Vec3 ambient(0.38f, 0.36f, 0.42f);

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        worldSh.use();
        worldSh.setVec3("uLightDir", lightDir);
        worldSh.setVec3("uLightColor", lightColor);
        worldSh.setVec3("uAmbient", ambient);
        worldSh.setVec3("uCamPos", gCam.position);
        worldSh.setFloat("uTime", static_cast<float>(now));
        worldSh.setInt("uMode", 0);
        worldSh.setMat4("uView", view);
        worldSh.setMat4("uProj", proj);

        world.drawStatic(worldSh, view, proj);
        worldSh.setInt("uMode", 1);
        Mat4 id;
        worldSh.setMat4("uModel", id);
        world.waterMesh.draw();
        worldSh.setInt("uMode", 0);

        for (const auto& c : cars) c.draw(worldSh);
        player.draw(worldSh);

        for (const auto& p : peds) {
            if (!p.mesh) continue;
            Mat4 m = Mat4::translate(p.pos) * Mat4::rotateY(p.yaw);
            worldSh.setMat4("uModel", m);
            p.mesh->draw();
        }

        // Remote players (on foot only — in-vehicle they are the car)
        for (auto& kv : remotes) {
            RemotePlayer& rp = kv.second;
            if (rp.inVehicle) continue;
            rp.ensureMesh();
            Mat4 m = Mat4::translate(rp.pos) * Mat4::rotateY(rp.yaw);
            worldSh.setMat4("uModel", m);
            rp.mesh.draw();
        }

        if (!missionDone) {
            static Mesh marker = []() {
                std::vector<Vertex> v; std::vector<unsigned> i;
                pushBox(v, i, -0.6f, 0, -0.6f, 0.6f, 8.f, 0.6f, 1.f, 0.3f, 0.8f);
                pushBox(v, i, -1.2f, 0, -1.2f, 1.2f, 0.3f, 1.2f, 1.f, 0.85f, 0.2f);
                Mesh m; m.upload(v, i); return m;
            }();
            float bob = 0.3f * std::sin(static_cast<float>(now) * 3.f);
            Mat4 m = Mat4::translate(missionTarget + Vec3(0, bob, 0));
            worldSh.setMat4("uModel", m);
            marker.draw();
        }

        // HUD
        glDisable(GL_DEPTH_TEST);
        std::vector<float> hd;

        pushQuad(hd, 0.62f, -0.95f, 0.95f, -0.55f, 0.05f, 0.08f, 0.12f);
        pushQuad(hd, 0.62f, -0.95f, 0.95f, -0.93f, 0.95f, 0.4f, 0.6f);
        pushQuad(hd, 0.62f, -0.57f, 0.95f, -0.55f, 0.95f, 0.4f, 0.6f);

        Vec3 focus = player.focusPoint(cars);
        {
            float mx = focus.x / World::CITY_HALF;
            float mz = focus.z / World::CITY_HALF;
            float px = clampf(0.785f + mx * 0.14f, 0.64f, 0.93f);
            float py = clampf(-0.75f - mz * 0.16f, -0.93f, -0.58f);
            pushQuad(hd, px - 0.012f, py - 0.012f, px + 0.012f, py + 0.012f, 0.2f, 1.f, 0.4f);
        }
        // remote blips
        for (auto& kv : remotes) {
            float mx = kv.second.pos.x / World::CITY_HALF;
            float mz = kv.second.pos.z / World::CITY_HALF;
            float px = clampf(0.785f + mx * 0.14f, 0.64f, 0.93f);
            float py = clampf(-0.75f - mz * 0.16f, -0.93f, -0.58f);
            pushQuad(hd, px - 0.008f, py - 0.008f, px + 0.008f, py + 0.008f,
                     kv.second.cr, kv.second.cg, kv.second.cb);
        }

        if (!missionDone) {
            float mx = missionTarget.x / World::CITY_HALF;
            float mz = missionTarget.z / World::CITY_HALF;
            float px = 0.785f + mx * 0.14f;
            float py = -0.75f - mz * 0.16f;
            pushQuad(hd, px - 0.01f, py - 0.01f, px + 0.01f, py + 0.01f, 1.f, 0.3f, 0.85f);
        }

        if (player.inVehicle && player.vehicleIndex >= 0 &&
            player.vehicleIndex < (int)cars.size()) {
            float sp = std::abs(cars[player.vehicleIndex].speed) / cars[player.vehicleIndex].maxSpeed;
            pushQuad(hd, -0.95f, -0.92f, -0.55f, -0.85f, 0.1f, 0.1f, 0.12f);
            pushQuad(hd, -0.95f, -0.92f, -0.95f + 0.4f * sp, -0.85f, 0.2f, 0.85f, 1.f);
        }

        for (int i = 0; i < 5; ++i) {
            float x0 = 0.55f + i * 0.07f;
            float lit = (i < stars) ? 1.f : 0.25f;
            pushQuad(hd, x0, 0.88f, x0 + 0.05f, 0.95f, lit, lit * 0.85f, 0.1f);
        }

        // MP status bar
        bool online = net.connected && net.welcomed;
        pushQuad(hd, -0.95f, 0.88f, -0.45f, 0.96f,
                 online ? 0.05f : 0.15f, online ? 0.2f : 0.08f, online ? 0.1f : 0.08f);
        pushQuad(hd, -0.93f, 0.90f, -0.47f, 0.94f,
                 online ? 0.3f : 0.6f, online ? 0.9f : 0.4f, online ? 0.5f : 0.3f);

        int nearCar = -1;
        if (!player.inVehicle) {
            float bestD = 7.f;
            for (int i = 0; i < (int)cars.size(); ++i) {
                bool free = net.offline ? !cars[i].occupied
                                        : (i >= (int)vehOwner.size() || vehOwner[i] < 0);
                if (!free) continue;
                float d = Vec3::distance(player.pos, cars[i].pos);
                if (d < bestD) { bestD = d; nearCar = i; }
            }
        }
        if (nearCar >= 0) {
            pushQuad(hd, -0.28f, -0.55f, 0.28f, -0.42f, 0.05f, 0.12f, 0.08f);
            pushQuad(hd, -0.26f, -0.52f, 0.26f, -0.45f, 0.35f, 0.95f, 0.5f);
        }

        if (gShowHelp && !gPaused) {
            pushQuad(hd, -0.98f, 0.35f, -0.42f, 0.85f, 0.02f, 0.04f, 0.08f);
            for (int i = 0; i < 7; ++i) {
                float y = 0.78f - i * 0.06f;
                float w = 0.42f - (i % 3) * 0.06f;
                pushQuad(hd, -0.95f, y, -0.95f + w, y + 0.022f, 0.75f, 0.8f, 0.9f);
            }
        }

        pushQuad(hd, -0.95f, -0.82f, -0.55f, -0.76f, 0.15f, 0.05f, 0.05f);
        pushQuad(hd, -0.95f, -0.82f, -0.58f, -0.76f, 0.2f, 0.85f, 0.35f);

        if (gPaused) {
            pushQuad(hd, -1.f, -1.f, 1.f, 1.f, 0.02f, 0.03f, 0.06f);
            pushQuad(hd, -0.38f, -0.42f, 0.38f, 0.42f, 0.08f, 0.1f, 0.16f);
            pushQuad(hd, -0.36f, 0.28f, 0.36f, 0.38f, 0.95f, 0.4f, 0.65f);
            float nx, ny;
            cursorToNdc(window, nx, ny);
            bool hoverResume = pointInRect(nx, ny, btnResume[0], btnResume[1], btnResume[2], btnResume[3]);
            bool hoverQuit   = pointInRect(nx, ny, btnQuit[0], btnQuit[1], btnQuit[2], btnQuit[3]);
            pushQuad(hd, btnResume[0], btnResume[1], btnResume[2], btnResume[3],
                     hoverResume ? 0.25f : 0.15f, hoverResume ? 0.75f : 0.45f, hoverResume ? 0.4f : 0.3f);
            pushQuad(hd, btnQuit[0], btnQuit[1], btnQuit[2], btnQuit[3],
                     hoverQuit ? 0.85f : 0.5f, hoverQuit ? 0.25f : 0.15f, hoverQuit ? 0.3f : 0.2f);
            pushQuad(hd, -0.14f, 0.12f, 0.14f, 0.18f, 0.95f, 0.95f, 0.98f);
            pushQuad(hd, -0.10f, -0.14f, 0.10f, -0.08f, 0.95f, 0.9f, 0.9f);
            pushQuad(hd, -0.28f, -0.32f, 0.28f, -0.28f, 0.5f, 0.55f, 0.65f);
            pushQuad(hd, -0.22f, -0.38f, 0.22f, -0.34f, 0.4f, 0.45f, 0.55f);
        }

        hud.upload(hd);
        hudSh.use();
        glUniform1f(glGetUniformLocation(hudSh.id, "uAlpha"), gPaused ? 0.92f : 0.88f);
        hud.draw();
        glEnable(GL_DEPTH_TEST);

        char title[384];
        float spd = 0.f;
        if (player.inVehicle && player.vehicleIndex >= 0 &&
            player.vehicleIndex < (int)cars.size())
            spd = std::abs(cars[player.vehicleIndex].speed) * 3.6f;

        if (gPaused) {
            std::snprintf(title, sizeof(title),
                "PAUSED — mouse free | Esc/Enter resume | Quit button to exit | %s",
                net.status.c_str());
        } else {
            std::snprintf(title, sizeof(title),
                "Leonida | %s | players:%zu | (%.0f,%.0f) | %s | %.0f km/h%s",
                net.status.c_str(),
                remotes.size() + (online ? 1 : 0),
                focus.x, focus.z,
                player.inVehicle ? "DRIVING" : "ON FOOT",
                spd,
                nearCar >= 0 ? " | [E] Enter" : "");
        }
        glfwSetWindowTitle(window, title);
        glfwSwapBuffers(window);
    }

    net.disconnect();
    player.destroy();
    for (auto& c : cars) c.destroy();
    for (auto& kv : remotes) kv.second.destroy();
    for (auto& m : pedMeshes) m.destroy();
    world.cityMesh.destroy();
    world.waterMesh.destroy();
    world.sandMesh.destroy();
    world.palmMesh.destroy();
    world.lightMesh.destroy();
    glDeleteProgram(worldSh.id);
    glDeleteProgram(hudSh.id);
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
