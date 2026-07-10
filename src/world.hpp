#pragma once
#include <vector>
#include <cmath>
#include <cstdlib>
#include "mesh.hpp"
#include "math.hpp"

struct Building {
    float x, z, w, d, h;
    float r, g, b;
};

struct Prop {
    float x, z;
    float scale;
    int type; // 0 palm, 1 streetlight
};

struct World {
    // ~560m across city + beach/ocean beyond south edge
    static constexpr float CITY_HALF = 280.f;
    static constexpr float BLOCK = 32.f;
    static constexpr float ROAD = 10.f;

    Mesh cityMesh;
    Mesh waterMesh;
    Mesh sandMesh;
    Mesh palmMesh;
    Mesh lightMesh;
    std::vector<Building> buildings;
    std::vector<Prop> props;

    // Simple collision: axis-aligned building footprints
    bool collides(float x, float z, float radius) const {
        for (const auto& b : buildings) {
            float hx = b.w * 0.5f + radius;
            float hz = b.d * 0.5f + radius;
            if (std::abs(x - b.x) < hx && std::abs(z - b.z) < hz)
                return true;
        }
        return false;
    }

    // Keep player/car in world bounds; beach is south (negative Z edge)
    Vec3 clampPos(const Vec3& p) const {
        return {
            clampf(p.x, -CITY_HALF + 2.f, CITY_HALF - 2.f),
            p.y,
            clampf(p.z, -CITY_HALF - 30.f, CITY_HALF - 2.f)
        };
    }

    float groundY(float /*x*/, float z) const {
        // Beach slope into water south of city
        if (z < -CITY_HALF + 5.f) {
            float t = (-CITY_HALF + 5.f - z) / 25.f;
            if (t > 1.f) return -0.5f; // underwater bed
            return lerpf(0.f, -0.3f, t);
        }
        return 0.f;
    }

    // True if point is on a road strip (safe spawn corridor)
    bool onRoad(float x, float z, float margin = 0.f) const {
        float half = ROAD * 0.5f - margin;
        if (half < 0.5f) half = 0.5f;
        // Nearest vertical road (constant x = k*BLOCK)
        float nearestVx = std::round(x / BLOCK) * BLOCK;
        if (std::abs(x - nearestVx) <= half) return true;
        // Nearest horizontal road (constant z = k*BLOCK)
        float nearestHz = std::round(z / BLOCK) * BLOCK;
        if (std::abs(z - nearestHz) <= half) return true;
        return false;
    }

    // If inside a building, push out along the shortest axis (+ padding).
    // If free, returns the point unchanged.
    Vec3 resolveSolid(float x, float z, float radius) const {
        if (!collides(x, z, radius))
            return Vec3(x, groundY(x, z), z);

        // Push out of every overlapping building (usually one)
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
                if (penX < penZ) {
                    x = b.x + (dx >= 0.f ? hx + pad : -(hx + pad));
                } else {
                    z = b.z + (dz >= 0.f ? hz + pad : -(hz + pad));
                }
            }
            if (!hit) break;
        }

        // Still stuck? Search outward for a free cell (roads first)
        if (collides(x, z, radius)) {
            const float steps[] = {2.f, 4.f, 6.f, 8.f, 12.f, 16.f, 24.f};
            const float dirs[8][2] = {
                {1,0},{-1,0},{0,1},{0,-1},{1,1},{-1,1},{1,-1},{-1,-1}
            };
            bool found = false;
            float bx = x, bz = z;
            for (float s : steps) {
                for (auto& d : dirs) {
                    float nx = x + d[0] * s;
                    float nz = z + d[1] * s;
                    if (!collides(nx, nz, radius)) {
                        bx = nx; bz = nz; found = true; break;
                    }
                }
                if (found) break;
            }
            if (found) { x = bx; z = bz; }
            else {
                // Last resort: origin intersection (always a road cross)
                x = 0.f; z = 0.f;
            }
        }

        Vec3 p = clampPos(Vec3(x, 0.f, z));
        p.y = groundY(p.x, p.z);
        return p;
    }

    // Prefer a clear spot on the road grid near a preferred position
    Vec3 findClearSpawn(float preferX, float preferZ, float radius = 0.5f) const {
        // Snap toward nearest road centerline
        float vx = std::round(preferX / BLOCK) * BLOCK;
        float hz = std::round(preferZ / BLOCK) * BLOCK;
        // Try intersection, then along both roads
        const float candidates[][2] = {
            {vx, hz},
            {vx, preferZ},
            {preferX, hz},
            {0.f, 0.f},
            {0.f, BLOCK},
            {BLOCK, 0.f},
            {-BLOCK, 0.f},
            {0.f, -BLOCK},
            {vx, hz + ROAD},
            {vx, hz - ROAD},
            {vx + ROAD, hz},
            {vx - ROAD, hz},
        };
        for (auto& c : candidates) {
            if (!collides(c[0], c[1], radius)) {
                Vec3 p = clampPos(Vec3(c[0], 0.f, c[1]));
                p.y = groundY(p.x, p.z);
                return p;
            }
        }
        return resolveSolid(0.f, 0.f, radius);
    }

    void generate(unsigned seed = 42) {
        std::srand(seed);
        auto rnd = []() { return (std::rand() % 10000) / 10000.f; };
        auto rndRange = [&](float a, float b) { return a + rnd() * (b - a); };

        // Vice City / Leonida palette
        const float palette[][3] = {
            {0.95f, 0.55f, 0.65f}, // coral pink
            {0.35f, 0.75f, 0.85f}, // teal
            {0.98f, 0.85f, 0.45f}, // sunny yellow
            {0.55f, 0.45f, 0.85f}, // lavender
            {0.95f, 0.7f, 0.4f},   // peach
            {0.4f, 0.55f, 0.7f},   // art deco blue
            {0.9f, 0.9f, 0.92f},   // white stucco
            {0.3f, 0.65f, 0.55f},  // mint
        };

        std::vector<Vertex> verts;
        std::vector<unsigned> idx;

        // Asphalt roads + sidewalks + grass lots
        // Grid: road every BLOCK
        int n = static_cast<int>((CITY_HALF * 2.f) / BLOCK);

        // Sidewalk width between asphalt and lots
        const float curb = 1.4f;

        // Base grass + buildings
        for (int iz = -n; iz < n; ++iz) {
            for (int ix = -n; ix < n; ++ix) {
                float cx = (ix + 0.5f) * BLOCK;
                float cz = (iz + 0.5f) * BLOCK;
                float lot = BLOCK - ROAD;
                float halfLot = lot * 0.5f;
                float gx0 = cx - halfLot, gx1 = cx + halfLot;
                float gz0 = cz - halfLot, gz1 = cz + halfLot;

                // Checker grass tiles for more ground detail
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

                // Concrete sidewalk ring around lot (visual only)
                pushBox(verts, idx,
                        gx0 - curb, 0.02f, gz0 - curb, gx1 + curb, 0.05f, gz0,
                        0.72f, 0.72f, 0.7f, 0.02f);
                pushBox(verts, idx,
                        gx0 - curb, 0.02f, gz1, gx1 + curb, 0.05f, gz1 + curb,
                        0.72f, 0.72f, 0.7f, 0.02f);
                pushBox(verts, idx,
                        gx0 - curb, 0.02f, gz0, gx0, 0.05f, gz1,
                        0.7f, 0.7f, 0.68f, 0.02f);
                pushBox(verts, idx,
                        gx1, 0.02f, gz0, gx1 + curb, 0.05f, gz1,
                        0.7f, 0.7f, 0.68f, 0.02f);

                // Buildings — keep sidewalk gap so footprints never eat the road
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
                    if (dist < 60.f) b.h += rndRange(12.f, 45.f);       // downtown towers
                    else if (dist < 140.f) b.h += rndRange(4.f, 18.f);  // midtown
                    int pi = std::rand() % 8;
                    b.r = palette[pi][0];
                    b.g = palette[pi][1];
                    b.b = palette[pi][2];
                    buildings.push_back(b);

                    float x0 = b.x - b.w * 0.5f, x1 = b.x + b.w * 0.5f;
                    float z0 = b.z - b.d * 0.5f, z1 = b.z + b.d * 0.5f;

                    // Main body
                    pushBox(verts, idx, x0, 0.f, z0, x1, b.h, z1, b.r, b.g, b.b, 0.1f);

                    // Ground floor darker plinth
                    pushBox(verts, idx, x0 - 0.08f, 0.f, z0 - 0.08f, x1 + 0.08f, 3.2f, z1 + 0.08f,
                            b.r * 0.75f, b.g * 0.75f, b.b * 0.78f, 0.06f);

                    // Roof parapet
                    pushBox(verts, idx, x0 - 0.25f, b.h, z0 - 0.25f, x1 + 0.25f, b.h + 0.55f, z1 + 0.25f,
                            b.r * 0.5f, b.g * 0.5f, b.b * 0.55f, 0.04f);
                    // Roof deck
                    pushBox(verts, idx, x0 + 0.3f, b.h + 0.55f, z0 + 0.3f, x1 - 0.3f, b.h + 0.7f, z1 - 0.3f,
                            0.35f, 0.35f, 0.38f, 0.02f);

                    // Window grid (every other floor + 2 facades to keep mesh budget sane)
                    int floors = std::max(2, static_cast<int>(b.h / 3.1f));
                    int colsX = std::max(2, static_cast<int>(b.w / 2.6f));
                    int colsZ = std::max(2, static_cast<int>(b.d / 2.6f));
                    for (int f = 1; f < floors; f += 1) {
                        float y0 = f * 3.1f + 0.35f;
                        float y1 = y0 + 1.25f;
                        float lit = (rnd() > 0.5f) ? 1.f : 0.5f;
                        float wr = (0.45f + 0.4f * rnd()) * lit;
                        float wg = (0.55f + 0.3f * rnd()) * lit;
                        float wb = (0.7f + 0.25f * rnd()) * lit;
                        // +Z and +X primary facades
                        for (int c = 0; c < colsX; ++c) {
                            float u0 = x0 + 0.45f + c * (b.w - 0.9f) / colsX;
                            float u1 = u0 + (b.w - 0.9f) / colsX * 0.5f;
                            pushBox(verts, idx, u0, y0, z1 - 0.04f, u1, y1, z1 + 0.09f, wr, wg, wb, 0.f);
                        }
                        for (int c = 0; c < colsZ; ++c) {
                            float u0 = z0 + 0.45f + c * (b.d - 0.9f) / colsZ;
                            float u1 = u0 + (b.d - 0.9f) / colsZ * 0.5f;
                            pushBox(verts, idx, x1 - 0.04f, y0, u0, x1 + 0.09f, y1, u1, wr * 0.95f, wg * 0.95f, wb, 0.f);
                        }
                        // Alternate floors get the other two facades
                        if ((f & 1) == 0) {
                            for (int c = 0; c < colsX; ++c) {
                                float u0 = x0 + 0.45f + c * (b.w - 0.9f) / colsX;
                                float u1 = u0 + (b.w - 0.9f) / colsX * 0.5f;
                                pushBox(verts, idx, u0, y0, z0 - 0.09f, u1, y1, z0 + 0.04f,
                                        wr * 0.9f, wg * 0.9f, wb * 0.95f, 0.f);
                            }
                        }
                    }

                    // Occasional rooftop billboard / AC unit
                    if (rnd() > 0.65f) {
                        pushBox(verts, idx,
                                b.x - 1.2f, b.h + 0.7f, b.z - 0.4f,
                                b.x + 1.2f, b.h + 3.2f, b.z + 0.4f,
                                rndRange(0.8f, 1.f), rndRange(0.2f, 0.6f), rndRange(0.5f, 1.f), 0.05f);
                    }
                }

                // Palms on lot corners
                if (rnd() > 0.4f)
                    props.push_back({cx - halfLot + 1.8f, cz - halfLot + 1.8f, rndRange(0.9f, 1.35f), 0});
                if (rnd() > 0.55f)
                    props.push_back({cx + halfLot - 1.8f, cz + halfLot - 1.8f, rndRange(0.9f, 1.35f), 0});
                if (rnd() > 0.75f)
                    props.push_back({cx - halfLot + 1.8f, cz + halfLot - 1.8f, rndRange(0.85f, 1.2f), 0});
            }
        }

        // Roads: asphalt + curbs + continuous lane markings (cheap & sharp)
        for (int i = -n; i <= n; ++i) {
            float c = i * BLOCK;
            float rh = ROAD * 0.5f;
            // Vertical road
            pushBox(verts, idx, c - rh, -0.02f, -CITY_HALF, c + rh, 0.03f, CITY_HALF,
                    0.16f, 0.16f, 0.18f, 0.02f);
            // Horizontal road
            pushBox(verts, idx, -CITY_HALF, -0.02f, c - rh, CITY_HALF, 0.03f, c + rh,
                    0.16f, 0.16f, 0.18f, 0.02f);
            // White edge lines
            pushBox(verts, idx, c - rh + 0.15f, 0.035f, -CITY_HALF, c - rh + 0.35f, 0.042f, CITY_HALF,
                    0.92f, 0.92f, 0.9f, 0.f);
            pushBox(verts, idx, c + rh - 0.35f, 0.035f, -CITY_HALF, c + rh - 0.15f, 0.042f, CITY_HALF,
                    0.92f, 0.92f, 0.9f, 0.f);
            pushBox(verts, idx, -CITY_HALF, 0.035f, c - rh + 0.15f, CITY_HALF, 0.042f, c - rh + 0.35f,
                    0.92f, 0.92f, 0.9f, 0.f);
            pushBox(verts, idx, -CITY_HALF, 0.035f, c + rh - 0.35f, CITY_HALF, 0.042f, c + rh - 0.15f,
                    0.92f, 0.92f, 0.9f, 0.f);
            // Double yellow center (two thin strips)
            pushBox(verts, idx, c - 0.18f, 0.036f, -CITY_HALF, c - 0.06f, 0.044f, CITY_HALF,
                    0.95f, 0.82f, 0.15f, 0.f);
            pushBox(verts, idx, c + 0.06f, 0.036f, -CITY_HALF, c + 0.18f, 0.044f, CITY_HALF,
                    0.95f, 0.82f, 0.15f, 0.f);
            pushBox(verts, idx, -CITY_HALF, 0.036f, c - 0.18f, CITY_HALF, 0.044f, c - 0.06f,
                    0.95f, 0.82f, 0.15f, 0.f);
            pushBox(verts, idx, -CITY_HALF, 0.036f, c + 0.06f, CITY_HALF, 0.044f, c + 0.18f,
                    0.95f, 0.82f, 0.15f, 0.f);
        }

        // Beach boardwalk / multi-band sand
        pushBox(verts, idx,
                -CITY_HALF - 10.f, -0.08f, -CITY_HALF - 2.f,
                CITY_HALF + 10.f, 0.06f, -CITY_HALF + 8.f,
                0.92f, 0.82f, 0.58f, 0.04f);
        pushBox(verts, idx,
                -CITY_HALF - 10.f, -0.12f, -CITY_HALF - 18.f,
                CITY_HALF + 10.f, 0.02f, -CITY_HALF - 2.f,
                0.88f, 0.76f, 0.5f, 0.04f);
        // Boardwalk planks strip
        pushBox(verts, idx,
                -CITY_HALF, 0.06f, -CITY_HALF + 5.5f,
                CITY_HALF, 0.12f, -CITY_HALF + 7.5f,
                0.55f, 0.38f, 0.22f, 0.03f);

        // Dense beach palms
        for (float x = -CITY_HALF + 4.f; x < CITY_HALF; x += 8.f) {
            props.push_back({x + rndRange(-1.5f, 1.5f), -CITY_HALF + 3.f + rndRange(0.f, 4.f),
                             rndRange(1.0f, 1.5f), 0});
            if (rnd() > 0.5f)
                props.push_back({x + rndRange(-2.f, 2.f), -CITY_HALF + 1.f + rndRange(0.f, 2.f),
                                 rndRange(0.85f, 1.2f), 0});
        }

        cityMesh.upload(verts, idx);

        // Layered ocean (near + mid + far)
        {
            std::vector<Vertex> wv;
            std::vector<unsigned> wi;
            pushBox(wv, wi,
                    -CITY_HALF - 60.f, -0.35f, -CITY_HALF - 100.f,
                    CITY_HALF + 60.f, -0.12f, -CITY_HALF + 2.f,
                    0.12f, 0.58f, 0.78f, 0.03f);
            pushBox(wv, wi,
                    -CITY_HALF - 120.f, -0.45f, -CITY_HALF - 220.f,
                    CITY_HALF + 120.f, -0.18f, -CITY_HALF - 90.f,
                    0.06f, 0.4f, 0.68f, 0.02f);
            pushBox(wv, wi,
                    -CITY_HALF - 200.f, -0.55f, -CITY_HALF - 360.f,
                    CITY_HALF + 200.f, -0.22f, -CITY_HALF - 200.f,
                    0.03f, 0.28f, 0.55f, 0.02f);
            waterMesh.upload(wv, wi);
        }

        {
            std::vector<Vertex> sv;
            std::vector<unsigned> si;
            pushBox(sv, si,
                    -CITY_HALF - 8.f, -0.18f, -CITY_HALF - 45.f,
                    CITY_HALF + 8.f, 0.0f, -CITY_HALF - 1.f,
                    0.9f, 0.8f, 0.55f, 0.04f);
            sandMesh.upload(sv, si);
        }

        palmMesh = makePalmMesh();

        {
            std::vector<Vertex> lv;
            std::vector<unsigned> li;
            pushBox(lv, li, -0.1f, 0, -0.1f, 0.1f, 5.4f, 0.1f, 0.22f, 0.22f, 0.25f);
            pushBox(lv, li, -0.55f, 5.1f, -0.12f, 0.55f, 5.45f, 0.12f, 0.28f, 0.28f, 0.3f);
            pushBox(lv, li, -0.28f, 5.0f, -0.28f, 0.28f, 5.35f, 0.28f, 1.f, 0.95f, 0.72f);
            lightMesh.upload(lv, li);
        }

        // Street lights on every road, denser spacing
        for (int i = -n; i <= n; ++i) {
            float c = i * BLOCK;
            for (float z = -CITY_HALF + 8.f; z < CITY_HALF; z += 22.f) {
                props.push_back({c + ROAD * 0.42f, z, 1.f, 1});
                if ((i & 1) == 0)
                    props.push_back({c - ROAD * 0.42f, z + 11.f, 1.f, 1});
            }
        }
    }

    void drawStatic(const Shader& sh, const Mat4& view, const Mat4& proj) const {
        Mat4 model;
        sh.setMat4("uModel", model);
        sh.setMat4("uView", view);
        sh.setMat4("uProj", proj);
        cityMesh.draw();
        sandMesh.draw();
        // waterMesh drawn separately with animated shader mode

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
