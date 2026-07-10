#pragma once
#include <vector>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <algorithm>
#include "mesh.hpp"
#include "math.hpp"
#include "shader.hpp"

struct Building {
    float x = 0, z = 0, w = 8, d = 8, h = 12;
    float r = 0.8f, g = 0.8f, b = 0.8f;
    bool enterable = false;
    int floors = 1;       // interior levels
    int doorSide = 0;     // 0=+Z facade
    static constexpr float FLOOR_H = 3.2f;
    static constexpr float WALL = 0.35f;
    static constexpr float DOOR_W = 2.4f;
};

struct Prop {
    float x, z;
    float scale;
    int type; // 0 palm, 1 streetlight
};

struct World {
    static constexpr float CITY_HALF = 280.f;
    static constexpr float BLOCK = 32.f;
    static constexpr float ROAD = 10.f;

    Mesh cityMesh;
    Mesh waterMesh;
    Mesh sandMesh;
    Mesh palmMesh;
    Mesh lightMesh;
    Mesh interiorMesh; // all enterable interiors baked
    std::vector<Building> buildings;
    std::vector<Prop> props;

    // Player interior state (set by player movement)
    // -1 = outdoors
    int playerInterior = -1;
    int playerFloor = 0;

    bool inFootprint(const Building& b, float x, float z, float radius) const {
        float hx = b.w * 0.5f + radius;
        float hz = b.d * 0.5f + radius;
        return std::abs(x - b.x) < hx && std::abs(z - b.z) < hz;
    }

    bool inDoorPortal(const Building& b, float x, float z, float radius) const {
        if (!b.enterable) return false;
        float x0 = b.x - b.w * 0.5f, x1 = b.x + b.w * 0.5f;
        float z0 = b.z - b.d * 0.5f, z1 = b.z + b.d * 0.5f;
        float halfDoor = Building::DOOR_W * 0.5f + radius;
        // +Z face door
        if (z > z1 - 1.2f - radius && z < z1 + 1.5f + radius &&
            std::abs(x - b.x) < halfDoor)
            return true;
        // small inside threshold past door
        if (z < z1 && z > z1 - 2.5f && std::abs(x - b.x) < halfDoor &&
            x > x0 + Building::WALL && x < x1 - Building::WALL)
            return true;
        (void)z0;
        return false;
    }

    // Wall shells for interior (true if hitting wall)
    bool hitsInteriorWall(const Building& b, float x, float z, float radius) const {
        float x0 = b.x - b.w * 0.5f + Building::WALL;
        float x1 = b.x + b.w * 0.5f - Building::WALL;
        float z0 = b.z - b.d * 0.5f + Building::WALL;
        float z1 = b.z + b.d * 0.5f - Building::WALL;
        // outside free space box = wall
        bool inInner = x > x0 - radius && x < x1 + radius &&
                       z > z0 - radius && z < z1 + radius;
        if (!inInner) return true; // outside inner free → wall/shell
        // door gap on +Z inner wall — allow exit
        float halfDoor = Building::DOOR_W * 0.5f;
        if (z > z1 - radius - 0.15f && std::abs(x - b.x) < halfDoor)
            return false;
        // stairs shaft free (SW corner)
        float sx0 = x0, sx1 = x0 + 2.2f;
        float sz0 = z0, sz1 = z0 + 2.2f;
        if (x > sx0 && x < sx1 && z > sz0 && z < sz1) return false;
        return false; // open floor plan interior
    }

    // forCar: solid footprints always. for player: door holes + interiors
    bool collides(float x, float z, float radius, bool forCar = false) const {
        for (int i = 0; i < (int)buildings.size(); ++i) {
            const Building& b = buildings[i];
            if (!inFootprint(b, x, z, radius)) continue;

            if (forCar) return true; // cars never enter

            if (playerInterior == i) {
                // inside this building
                if (hitsInteriorWall(b, x, z, radius)) return true;
                continue;
            }

            // outdoors: solid unless door portal of enterable
            if (b.enterable && inDoorPortal(b, x, z, radius))
                continue;
            return true;
        }
        return false;
    }

    int buildingIndexAt(float x, float z) const {
        for (int i = 0; i < (int)buildings.size(); ++i)
            if (inFootprint(buildings[i], x, z, 0.f)) return i;
        return -1;
    }

    Vec3 clampPos(const Vec3& p) const {
        return {
            clampf(p.x, -CITY_HALF + 2.f, CITY_HALF - 2.f),
            p.y,
            clampf(p.z, -CITY_HALF - 30.f, CITY_HALF - 2.f)
        };
    }

    float groundY(float x, float z) const {
        if (playerInterior >= 0 && playerInterior < (int)buildings.size()) {
            return playerFloor * Building::FLOOR_H;
        }
        if (z < -CITY_HALF + 5.f) {
            float t = (-CITY_HALF + 5.f - z) / 25.f;
            if (t > 1.f) return -0.5f;
            return lerpf(0.f, -0.3f, t);
        }
        (void)x;
        return 0.f;
    }

    // Update interior/floor from player position (call after move)
    void updatePlayerInterior(float x, float z, float /*y*/) {
        int bi = buildingIndexAt(x, z);
        if (bi < 0) {
            playerInterior = -1;
            playerFloor = 0;
            return;
        }
        const Building& b = buildings[bi];
        if (!b.enterable) {
            playerInterior = -1;
            playerFloor = 0;
            return;
        }
        // Enter if in door or already inside
        float x0 = b.x - b.w * 0.5f + Building::WALL * 0.5f;
        float x1 = b.x + b.w * 0.5f - Building::WALL * 0.5f;
        float z0 = b.z - b.d * 0.5f + Building::WALL * 0.5f;
        float z1 = b.z + b.d * 0.5f - Building::WALL * 0.5f;
        bool deepInside = x > x0 && x < x1 && z > z0 && z < z1 - 0.3f;
        if (playerInterior == bi || deepInside || inDoorPortal(b, x, z, 0.3f)) {
            playerInterior = bi;
            // stairs zone — SW corner
            float sx0 = b.x - b.w * 0.5f + Building::WALL;
            float sz0 = b.z - b.d * 0.5f + Building::WALL;
            if (x > sx0 && x < sx0 + 2.2f && z > sz0 && z < sz0 + 2.2f) {
                // sub-zones: lower half down, upper half up when moving
                // continuous: map local z to floor via discrete steps on timer-less walk
                // use position within stair: closer to outer = floor based on sub-rect
            }
        } else {
            playerInterior = -1;
            playerFloor = 0;
        }
    }

    // Call when player is in stairs (returns true if floor changed)
    bool tryUseStairs(float x, float z, bool wantUp) {
        if (playerInterior < 0) return false;
        const Building& b = buildings[playerInterior];
        float sx0 = b.x - b.w * 0.5f + Building::WALL;
        float sz0 = b.z - b.d * 0.5f + Building::WALL;
        if (x < sx0 || x > sx0 + 2.4f || z < sz0 || z > sz0 + 2.4f) return false;
        if (wantUp && playerFloor + 1 < b.floors) {
            playerFloor++;
            return true;
        }
        if (!wantUp && playerFloor > 0) {
            playerFloor--;
            return true;
        }
        return false;
    }

    bool onRoad(float x, float z, float margin = 0.f) const {
        float half = ROAD * 0.5f - margin;
        if (half < 0.5f) half = 0.5f;
        float nearestVx = std::round(x / BLOCK) * BLOCK;
        if (std::abs(x - nearestVx) <= half) return true;
        float nearestHz = std::round(z / BLOCK) * BLOCK;
        if (std::abs(z - nearestHz) <= half) return true;
        return false;
    }

    Vec3 resolveSolid(float x, float z, float radius) const {
        // Always resolve as vehicle (push out of any footprint)
        auto solidCollide = [&](float px, float pz) {
            for (const auto& b : buildings) {
                float hx = b.w * 0.5f + radius;
                float hz = b.d * 0.5f + radius;
                if (std::abs(px - b.x) < hx && std::abs(pz - b.z) < hz) return true;
            }
            return false;
        };
        if (!solidCollide(x, z))
            return Vec3(x, 0.f, z);

        for (int iter = 0; iter < 4; ++iter) {
            bool hit = false;
            for (const auto& b : buildings) {
                float hx = b.w * 0.5f + radius;
                float hz = b.d * 0.5f + radius;
                float dx = x - b.x;
                float dz = z - b.z;
                if (std::abs(dx) >= hx || std::abs(dz) >= hz) continue;
                hit = true;
                float penX = hx - std::abs(dx);
                float penZ = hz - std::abs(dz);
                const float pad = 0.35f;
                if (penX < penZ)
                    x = b.x + (dx >= 0.f ? hx + pad : -(hx + pad));
                else
                    z = b.z + (dz >= 0.f ? hz + pad : -(hz + pad));
            }
            if (!hit) break;
        }
        if (solidCollide(x, z)) {
            x = 0.f; z = 0.f;
        }
        Vec3 p = clampPos(Vec3(x, 0.f, z));
        p.y = 0.f;
        return p;
    }

    Vec3 findClearSpawn(float preferX, float preferZ, float radius = 0.5f) const {
        float vx = std::round(preferX / BLOCK) * BLOCK;
        float hz = std::round(preferZ / BLOCK) * BLOCK;
        const float candidates[][2] = {
            {vx, hz}, {0.f, 0.f}, {0.f, BLOCK}, {BLOCK, 0.f}, {-BLOCK, 0.f},
        };
        for (auto& c : candidates) {
            if (!collides(c[0], c[1], radius, true)) {
                Vec3 p = clampPos(Vec3(c[0], 0.f, c[1]));
                p.y = 0.f;
                return p;
            }
        }
        return resolveSolid(0.f, 0.f, radius);
    }

    void generate(unsigned seed = 42) {
        std::srand(seed);
        auto rnd = []() { return (std::rand() % 10000) / 10000.f; };
        auto rndRange = [&](float a, float b) { return a + rnd() * (b - a); };

        const float palette[][3] = {
            {0.95f, 0.55f, 0.65f}, {0.35f, 0.75f, 0.85f}, {0.98f, 0.85f, 0.45f},
            {0.55f, 0.45f, 0.85f}, {0.95f, 0.7f, 0.4f}, {0.4f, 0.55f, 0.7f},
            {0.9f, 0.9f, 0.92f}, {0.3f, 0.65f, 0.55f},
        };

        std::vector<Vertex> verts, iverts;
        std::vector<unsigned> idx, iidx;
        int n = static_cast<int>((CITY_HALF * 2.f) / BLOCK);
        const float curb = 1.4f;
        int enterableCount = 0;

        for (int iz = -n; iz < n; ++iz) {
            for (int ix = -n; ix < n; ++ix) {
                float cx = (ix + 0.5f) * BLOCK;
                float cz = (iz + 0.5f) * BLOCK;
                float lot = BLOCK - ROAD;
                float halfLot = lot * 0.5f;
                float gx0 = cx - halfLot, gx1 = cx + halfLot;
                float gz0 = cz - halfLot, gz1 = cz + halfLot;

                const int gdiv = 2;
                float gstepX = (gx1 - gx0) / gdiv;
                float gstepZ = (gz1 - gz0) / gdiv;
                for (int gz = 0; gz < gdiv; ++gz) {
                    for (int gx = 0; gx < gdiv; ++gx) {
                        float t = ((ix + iz + gx + gz) & 1) ? 1.f : 0.88f;
                        pushBox(verts, idx,
                                gx0 + gx * gstepX, -0.05f, gz0 + gz * gstepZ,
                                gx0 + (gx + 1) * gstepX, 0.02f, gz0 + (gz + 1) * gstepZ,
                                0.20f * t, 0.52f * t, 0.26f * t, 0.03f);
                    }
                }
                pushBox(verts, idx, gx0 - curb, 0.02f, gz0 - curb, gx1 + curb, 0.05f, gz0, 0.72f, 0.72f, 0.7f, 0.02f);
                pushBox(verts, idx, gx0 - curb, 0.02f, gz1, gx1 + curb, 0.05f, gz1 + curb, 0.72f, 0.72f, 0.7f, 0.02f);
                pushBox(verts, idx, gx0 - curb, 0.02f, gz0, gx0, 0.05f, gz1, 0.7f, 0.7f, 0.68f, 0.02f);
                pushBox(verts, idx, gx1, 0.02f, gz0, gx1 + curb, 0.05f, gz1, 0.7f, 0.7f, 0.68f, 0.02f);

                if (rnd() > 0.14f) {
                    Building b;
                    const float inset = 2.4f;
                    float maxW = std::max(4.f, lot - inset * 2.f);
                    float maxD = std::max(4.f, lot - inset * 2.f);
                    b.w = rndRange(5.5f, maxW);
                    b.d = rndRange(5.5f, maxD);
                    float maxOffX = std::max(0.f, (lot - b.w) * 0.5f - inset);
                    float maxOffZ = std::max(0.f, (lot - b.d) * 0.5f - inset);
                    b.x = cx + rndRange(-maxOffX, maxOffX);
                    b.z = cz + rndRange(-maxOffZ, maxOffZ);
                    b.h = rndRange(8.f, 32.f + rnd() * 22.f);
                    float dist = std::sqrt(b.x * b.x + b.z * b.z);
                    if (dist < 60.f) b.h += rndRange(12.f, 45.f);
                    else if (dist < 140.f) b.h += rndRange(4.f, 18.f);
                    int pi = std::rand() % 8;
                    b.r = palette[pi][0]; b.g = palette[pi][1]; b.b = palette[pi][2];

                    // Enterable mid-size buildings near center (limit count for perf)
                    int maxFloors = std::max(2, std::min(5, (int)(b.h / Building::FLOOR_H)));
                    if (enterableCount < 18 && dist < 90.f && b.w > 8.f && b.d > 8.f && rnd() > 0.35f) {
                        b.enterable = true;
                        b.floors = std::min(maxFloors, 2 + (std::rand() % 3)); // 2–4 floors
                        b.doorSide = 0;
                        enterableCount++;
                    }

                    buildings.push_back(b);
                    float x0 = b.x - b.w * 0.5f, x1 = b.x + b.w * 0.5f;
                    float z0 = b.z - b.d * 0.5f, z1 = b.z + b.d * 0.5f;

                    if (!b.enterable) {
                        pushBox(verts, idx, x0, 0.f, z0, x1, b.h, z1, b.r, b.g, b.b, 0.1f);
                    } else {
                        // Hollow exterior shell + door cut on +Z
                        float wt = Building::WALL;
                        float doorHalf = Building::DOOR_W * 0.5f;
                        // -Z wall
                        pushBox(verts, idx, x0, 0.f, z0, x1, b.h, z0 + wt, b.r, b.g, b.b, 0.1f);
                        // +Z wall with door gap (two sides + lintel)
                        pushBox(verts, idx, x0, 0.f, z1 - wt, b.x - doorHalf, 2.4f, z1, b.r, b.g, b.b, 0.1f);
                        pushBox(verts, idx, b.x + doorHalf, 0.f, z1 - wt, x1, 2.4f, z1, b.r, b.g, b.b, 0.1f);
                        pushBox(verts, idx, x0, 2.4f, z1 - wt, x1, b.h, z1, b.r, b.g, b.b, 0.1f);
                        // door frame accent
                        pushBox(verts, idx, b.x - doorHalf - 0.12f, 0.f, z1 - 0.05f,
                                b.x - doorHalf, 2.4f, z1 + 0.12f, 0.35f, 0.25f, 0.15f, 0.f);
                        pushBox(verts, idx, b.x + doorHalf, 0.f, z1 - 0.05f,
                                b.x + doorHalf + 0.12f, 2.4f, z1 + 0.12f, 0.35f, 0.25f, 0.15f, 0.f);
                        // ±X walls
                        pushBox(verts, idx, x0, 0.f, z0, x0 + wt, b.h, z1, b.r * 0.95f, b.g * 0.95f, b.b * 0.95f, 0.1f);
                        pushBox(verts, idx, x1 - wt, 0.f, z0, x1, b.h, z1, b.r * 0.95f, b.g * 0.95f, b.b * 0.95f, 0.1f);
                        // roof
                        pushBox(verts, idx, x0 - 0.2f, b.h, z0 - 0.2f, x1 + 0.2f, b.h + 0.5f, z1 + 0.2f,
                                b.r * 0.5f, b.g * 0.5f, b.b * 0.55f, 0.04f);

                        // ---- Interiors per floor ----
                        for (int f = 0; f < b.floors; ++f) {
                            float y0 = f * Building::FLOOR_H;
                            float y1 = y0 + Building::FLOOR_H;
                            // floor slab
                            pushBox(iverts, iidx, x0 + wt, y0, z0 + wt, x1 - wt, y0 + 0.12f, z1 - wt,
                                    0.55f, 0.5f, 0.45f, 0.03f);
                            // ceiling (except top)
                            if (f + 1 < b.floors)
                                pushBox(iverts, iidx, x0 + wt, y1 - 0.12f, z0 + wt, x1 - wt, y1, z1 - wt,
                                        0.75f, 0.72f, 0.7f, 0.02f);
                            // interior wall color
                            float ir = b.r * 0.85f + 0.1f, ig = b.g * 0.85f + 0.1f, ib = b.b * 0.85f + 0.12f;
                            pushBox(iverts, iidx, x0 + wt, y0, z0 + wt, x1 - wt, y1, z0 + wt + 0.08f, ir, ig, ib, 0.05f);
                            // +Z interior wall with door cut on ground only
                            if (f == 0) {
                                pushBox(iverts, iidx, x0 + wt, y0, z1 - wt - 0.08f, b.x - doorHalf, y1, z1 - wt, ir, ig, ib, 0.05f);
                                pushBox(iverts, iidx, b.x + doorHalf, y0, z1 - wt - 0.08f, x1 - wt, y1, z1 - wt, ir, ig, ib, 0.05f);
                                pushBox(iverts, iidx, x0 + wt, 2.4f, z1 - wt - 0.08f, x1 - wt, y1, z1 - wt, ir, ig, ib, 0.05f);
                            } else {
                                pushBox(iverts, iidx, x0 + wt, y0, z1 - wt - 0.08f, x1 - wt, y1, z1 - wt, ir, ig, ib, 0.05f);
                            }
                            pushBox(iverts, iidx, x0 + wt, y0, z0 + wt, x0 + wt + 0.08f, y1, z1 - wt, ir * 0.95f, ig * 0.95f, ib * 0.95f, 0.05f);
                            pushBox(iverts, iidx, x1 - wt - 0.08f, y0, z0 + wt, x1 - wt, y1, z1 - wt, ir * 0.95f, ig * 0.95f, ib * 0.95f, 0.05f);

                            // stairs in SW corner
                            float sx = x0 + wt + 0.3f;
                            float sz = z0 + wt + 0.3f;
                            for (int step = 0; step < 8; ++step) {
                                float sy = y0 + step * (Building::FLOOR_H / 8.f);
                                pushBox(iverts, iidx, sx, sy, sz + step * 0.22f,
                                        sx + 1.6f, sy + 0.18f, sz + step * 0.22f + 0.22f,
                                        0.5f, 0.45f, 0.4f, 0.02f);
                            }
                            // rail
                            pushBox(iverts, iidx, sx + 1.55f, y0, sz, sx + 1.65f, y1, sz + 1.8f, 0.3f, 0.3f, 0.32f, 0.f);

                            // furniture layout varies by floor
                            int kindA = f % 5;
                            int kindB = (f + 2) % 5;
                            pushFurniture(iverts, iidx, b.x + 1.5f, y0 + 0.12f, b.z, kindA);
                            pushFurniture(iverts, iidx, b.x - 1.8f, y0 + 0.12f, b.z - 1.2f, kindB);
                            pushFurniture(iverts, iidx, b.x + 0.5f, y0 + 0.12f, b.z + b.d * 0.15f, 4);
                            // rug
                            pushBox(iverts, iidx, b.x - 1.5f, y0 + 0.13f, b.z - 1.0f,
                                    b.x + 1.5f, y0 + 0.16f, b.z + 1.0f,
                                    0.6f + 0.1f * (f % 3), 0.25f, 0.35f, 0.f);
                            // ceiling light
                            pushBox(iverts, iidx, b.x - 0.3f, y1 - 0.25f, b.z - 0.3f,
                                    b.x + 0.3f, y1 - 0.05f, b.z + 0.3f, 1.f, 0.95f, 0.7f, 0.f);
                        }
                    }

                    // windows on solid buildings
                    if (!b.enterable) {
                        pushBox(verts, idx, x0 - 0.08f, 0.f, z0 - 0.08f, x1 + 0.08f, 3.2f, z1 + 0.08f,
                                b.r * 0.75f, b.g * 0.75f, b.b * 0.78f, 0.06f);
                        pushBox(verts, idx, x0 - 0.25f, b.h, z0 - 0.25f, x1 + 0.25f, b.h + 0.55f, z1 + 0.25f,
                                b.r * 0.5f, b.g * 0.5f, b.b * 0.55f, 0.04f);
                        int floors = std::max(2, static_cast<int>(b.h / 3.1f));
                        int colsX = std::max(2, static_cast<int>(b.w / 2.6f));
                        for (int f = 1; f < floors; f++) {
                            float y0 = f * 3.1f + 0.35f, y1 = y0 + 1.25f;
                            float wr = 0.55f + 0.3f * rnd();
                            for (int c = 0; c < colsX; ++c) {
                                float u0 = x0 + 0.45f + c * (b.w - 0.9f) / colsX;
                                float u1 = u0 + (b.w - 0.9f) / colsX * 0.5f;
                                pushBox(verts, idx, u0, y0, z1 - 0.04f, u1, y1, z1 + 0.09f, wr, wr + 0.1f, wr + 0.2f, 0.f);
                            }
                        }
                    } else {
                        // windows above door floor
                        float dHalf = Building::DOOR_W * 0.5f;
                        int colsX = std::max(2, static_cast<int>(b.w / 2.6f));
                        for (int f = 1; f < b.floors + 2; f++) {
                            float y0 = f * 3.1f + 0.35f, y1 = y0 + 1.15f;
                            if (y1 > b.h) break;
                            for (int c = 0; c < colsX; ++c) {
                                float u0 = x0 + 0.45f + c * (b.w - 0.9f) / colsX;
                                float u1 = u0 + (b.w - 0.9f) / colsX * 0.5f;
                                if (std::abs((u0 + u1) * 0.5f - b.x) < dHalf + 0.5f && f == 1) continue;
                                pushBox(verts, idx, u0, y0, z1 - 0.02f, u1, y1, z1 + 0.08f, 0.5f, 0.65f, 0.8f, 0.f);
                            }
                        }
                    }
                }

                if (rnd() > 0.4f)
                    props.push_back({cx - halfLot + 1.8f, cz - halfLot + 1.8f, rndRange(0.9f, 1.35f), 0});
                if (rnd() > 0.55f)
                    props.push_back({cx + halfLot - 1.8f, cz + halfLot - 1.8f, rndRange(0.9f, 1.35f), 0});
            }
        }

        for (int i = -n; i <= n; ++i) {
            float c = i * BLOCK;
            float rh = ROAD * 0.5f;
            pushBox(verts, idx, c - rh, -0.02f, -CITY_HALF, c + rh, 0.03f, CITY_HALF, 0.16f, 0.16f, 0.18f, 0.02f);
            pushBox(verts, idx, -CITY_HALF, -0.02f, c - rh, CITY_HALF, 0.03f, c + rh, 0.16f, 0.16f, 0.18f, 0.02f);
            pushBox(verts, idx, c - 0.18f, 0.036f, -CITY_HALF, c - 0.06f, 0.044f, CITY_HALF, 0.95f, 0.82f, 0.15f, 0.f);
            pushBox(verts, idx, c + 0.06f, 0.036f, -CITY_HALF, c + 0.18f, 0.044f, CITY_HALF, 0.95f, 0.82f, 0.15f, 0.f);
        }

        pushBox(verts, idx, -CITY_HALF - 10.f, -0.08f, -CITY_HALF - 2.f, CITY_HALF + 10.f, 0.06f, -CITY_HALF + 8.f,
                0.92f, 0.82f, 0.58f, 0.04f);
        for (float x = -CITY_HALF + 4.f; x < CITY_HALF; x += 8.f)
            props.push_back({x + rndRange(-1.5f, 1.5f), -CITY_HALF + 3.f, rndRange(1.0f, 1.5f), 0});

        cityMesh.upload(verts, idx);
        interiorMesh.upload(iverts, iidx);

        {
            std::vector<Vertex> wv; std::vector<unsigned> wi;
            pushBox(wv, wi, -CITY_HALF - 60.f, -0.35f, -CITY_HALF - 100.f, CITY_HALF + 60.f, -0.12f, -CITY_HALF + 2.f,
                    0.12f, 0.58f, 0.78f, 0.03f);
            pushBox(wv, wi, -CITY_HALF - 120.f, -0.45f, -CITY_HALF - 220.f, CITY_HALF + 120.f, -0.18f, -CITY_HALF - 90.f,
                    0.06f, 0.4f, 0.68f, 0.02f);
            waterMesh.upload(wv, wi);
        }
        {
            std::vector<Vertex> sv; std::vector<unsigned> si;
            pushBox(sv, si, -CITY_HALF - 8.f, -0.18f, -CITY_HALF - 45.f, CITY_HALF + 8.f, 0.f, -CITY_HALF - 1.f,
                    0.9f, 0.8f, 0.55f, 0.04f);
            sandMesh.upload(sv, si);
        }
        palmMesh = makePalmMesh();
        {
            std::vector<Vertex> lv; std::vector<unsigned> li;
            pushBox(lv, li, -0.1f, 0, -0.1f, 0.1f, 5.4f, 0.1f, 0.22f, 0.22f, 0.25f);
            pushBox(lv, li, -0.55f, 5.1f, -0.12f, 0.55f, 5.45f, 0.12f, 0.28f, 0.28f, 0.3f);
            pushBox(lv, li, -0.28f, 5.0f, -0.28f, 0.28f, 5.35f, 0.28f, 1.f, 0.95f, 0.72f);
            lightMesh.upload(lv, li);
        }
        for (int i = -n; i <= n; ++i) {
            float c = i * BLOCK;
            for (float z = -CITY_HALF + 8.f; z < CITY_HALF; z += 22.f)
                props.push_back({c + ROAD * 0.42f, z, 1.f, 1});
        }
        std::fprintf(stderr, "World: %d enterable buildings with interiors\n", enterableCount);
    }

    void drawStatic(const Shader& sh, const Mat4& view, const Mat4& proj) const {
        Mat4 model;
        sh.setMat4("uModel", model);
        sh.setMat4("uView", view);
        sh.setMat4("uProj", proj);
        cityMesh.draw();
        sandMesh.draw();
        // interiors always drawn (inside hollow shells)
        interiorMesh.draw();

        for (const auto& p : props) {
            if (p.type == 0) {
                Mat4 m = Mat4::translate(Vec3(p.x, 0, p.z)) * Mat4::scale(Vec3(p.scale, p.scale, p.scale));
                sh.setMat4("uModel", m);
                palmMesh.draw();
            } else {
                Mat4 m = Mat4::translate(Vec3(p.x, 0, p.z));
                sh.setMat4("uModel", m);
                lightMesh.draw();
            }
        }
    }
};
