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
    static constexpr float CITY_HALF = 120.f;
    static constexpr float BLOCK = 24.f;
    static constexpr float ROAD = 8.f;

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

        // Base grass
        for (int iz = -n; iz < n; ++iz) {
            for (int ix = -n; ix < n; ++ix) {
                float cx = (ix + 0.5f) * BLOCK;
                float cz = (iz + 0.5f) * BLOCK;
                float lot = BLOCK - ROAD;
                float halfLot = lot * 0.5f;
                // grass block interior
                float gx0 = cx - halfLot, gx1 = cx + halfLot;
                float gz0 = cz - halfLot, gz1 = cz + halfLot;
                pushBox(verts, idx, gx0, -0.05f, gz0, gx1, 0.02f, gz1, 0.22f, 0.55f, 0.28f, 0.04f);

                // building chance — keep a sidewalk gap so footprints never eat the road
                if (rnd() > 0.18f) {
                    Building b;
                    const float inset = 2.2f; // gap from grass edge / road
                    float maxW = std::max(4.f, lot - inset * 2.f);
                    float maxD = std::max(4.f, lot - inset * 2.f);
                    b.w = rndRange(5.f, maxW);
                    b.d = rndRange(5.f, maxD);
                    // Center in lot with only tiny jitter; clamp so building stays inside lot
                    float maxOffX = std::max(0.f, (lot - b.w) * 0.5f - inset);
                    float maxOffZ = std::max(0.f, (lot - b.d) * 0.5f - inset);
                    b.x = cx + rndRange(-maxOffX, maxOffX);
                    b.z = cz + rndRange(-maxOffZ, maxOffZ);
                    b.h = rndRange(6.f, 28.f + rnd() * 20.f);
                    // taller near "downtown" (center)
                    float dist = std::sqrt(b.x * b.x + b.z * b.z);
                    if (dist < 40.f) b.h += rndRange(10.f, 35.f);
                    int pi = std::rand() % 8;
                    b.r = palette[pi][0];
                    b.g = palette[pi][1];
                    b.b = palette[pi][2];
                    buildings.push_back(b);

                    // building body
                    pushBox(verts, idx,
                            b.x - b.w * 0.5f, 0.f, b.z - b.d * 0.5f,
                            b.x + b.w * 0.5f, b.h, b.z + b.d * 0.5f,
                            b.r, b.g, b.b, 0.1f);

                    // darker roof
                    pushBox(verts, idx,
                            b.x - b.w * 0.5f - 0.2f, b.h, b.z - b.d * 0.5f - 0.2f,
                            b.x + b.w * 0.5f + 0.2f, b.h + 0.4f, b.z + b.d * 0.5f + 0.2f,
                            b.r * 0.55f, b.g * 0.55f, b.b * 0.6f, 0.05f);

                    // window bands
                    int floors = static_cast<int>(b.h / 3.2f);
                    for (int f = 1; f < floors; ++f) {
                        float y = f * 3.2f;
                        float wr = 0.55f + 0.35f * rnd();
                        float wg = 0.65f + 0.25f * rnd();
                        float wb = 0.75f + 0.2f * rnd();
                        // front strip
                        pushBox(verts, idx,
                                b.x - b.w * 0.45f, y, b.z + b.d * 0.5f - 0.05f,
                                b.x + b.w * 0.45f, y + 1.2f, b.z + b.d * 0.5f + 0.08f,
                                wr, wg, wb, 0.f);
                    }
                }

                // palm on corners of some blocks
                if (rnd() > 0.55f) {
                    props.push_back({cx - halfLot + 1.5f, cz - halfLot + 1.5f, rndRange(0.8f, 1.2f), 0});
                }
                if (rnd() > 0.7f) {
                    props.push_back({cx + halfLot - 1.5f, cz + halfLot - 1.5f, rndRange(0.9f, 1.3f), 0});
                }
            }
        }

        // Roads (horizontal and vertical strips)
        for (int i = -n; i <= n; ++i) {
            float c = i * BLOCK;
            // vertical road (along Z, centered on x=c)
            pushBox(verts, idx,
                    c - ROAD * 0.5f, -0.02f, -CITY_HALF,
                    c + ROAD * 0.5f, 0.03f, CITY_HALF,
                    0.18f, 0.18f, 0.2f, 0.02f);
            // horizontal road
            pushBox(verts, idx,
                    -CITY_HALF, -0.02f, c - ROAD * 0.5f,
                    CITY_HALF, 0.03f, c + ROAD * 0.5f,
                    0.18f, 0.18f, 0.2f, 0.02f);
            // yellow center lines
            for (float z = -CITY_HALF; z < CITY_HALF; z += 4.f) {
                pushBox(verts, idx, c - 0.08f, 0.035f, z, c + 0.08f, 0.04f, z + 2.f,
                        0.95f, 0.85f, 0.2f, 0.f);
            }
            for (float x = -CITY_HALF; x < CITY_HALF; x += 4.f) {
                pushBox(verts, idx, x, 0.035f, c - 0.08f, x + 2.f, 0.04f, c + 0.08f,
                        0.95f, 0.85f, 0.2f, 0.f);
            }
        }

        // Beach boardwalk / sand strip on south edge
        pushBox(verts, idx,
                -CITY_HALF, -0.1f, -CITY_HALF - 2.f,
                CITY_HALF, 0.05f, -CITY_HALF + 6.f,
                0.9f, 0.8f, 0.55f, 0.05f);

        // Beach palms
        for (float x = -CITY_HALF + 5.f; x < CITY_HALF; x += 12.f) {
            props.push_back({x + rndRange(-2.f, 2.f), -CITY_HALF + 2.f + rndRange(0.f, 3.f), rndRange(1.f, 1.4f), 0});
        }

        cityMesh.upload(verts, idx);

        // Water (south of beach)
        {
            std::vector<Vertex> wv;
            std::vector<unsigned> wi;
            pushBox(wv, wi,
                    -CITY_HALF - 40.f, -0.4f, -CITY_HALF - 80.f,
                    CITY_HALF + 40.f, -0.15f, -CITY_HALF + 2.f,
                    0.1f, 0.55f, 0.75f, 0.03f);
            // further ocean deeper teal
            pushBox(wv, wi,
                    -CITY_HALF - 80.f, -0.5f, -CITY_HALF - 160.f,
                    CITY_HALF + 80.f, -0.2f, -CITY_HALF - 70.f,
                    0.05f, 0.35f, 0.65f, 0.02f);
            waterMesh.upload(wv, wi);
        }

        // Outer sand ring
        {
            std::vector<Vertex> sv;
            std::vector<unsigned> si;
            pushBox(sv, si,
                    -CITY_HALF - 5.f, -0.2f, -CITY_HALF - 35.f,
                    CITY_HALF + 5.f, 0.0f, -CITY_HALF - 1.f,
                    0.92f, 0.82f, 0.58f, 0.04f);
            sandMesh.upload(sv, si);
        }

        palmMesh = makePalmMesh();

        // Street light mesh
        {
            std::vector<Vertex> lv;
            std::vector<unsigned> li;
            pushBox(lv, li, -0.08f, 0, -0.08f, 0.08f, 5.f, 0.08f, 0.25f, 0.25f, 0.28f);
            pushBox(lv, li, -0.5f, 4.8f, -0.15f, 0.5f, 5.1f, 0.15f, 0.3f, 0.3f, 0.32f);
            pushBox(lv, li, -0.25f, 4.7f, -0.25f, 0.25f, 5.0f, 0.25f, 1.f, 0.95f, 0.7f);
            lightMesh.upload(lv, li);
        }

        // Street lights along major roads
        for (int i = -n; i <= n; i += 2) {
            float c = i * BLOCK;
            for (float z = -CITY_HALF + 10.f; z < CITY_HALF; z += 28.f) {
                props.push_back({c + ROAD * 0.45f, z, 1.f, 1});
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
