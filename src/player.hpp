#pragma once
#include "math.hpp"
#include "mesh.hpp"
#include "world.hpp"
#include "vehicle.hpp"
#include "camera.hpp"
#include <vector>
#include <cmath>

struct Player {
    Vec3 pos{0.f, 0.f, 0.f};
    float yaw = 0.f;
    float walkSpeed = 7.5f;
    float runSpeed = 12.f;
    bool inVehicle = false;
    int vehicleIndex = -1;
    Mesh mesh;
    bool hasMesh = false;
    float stairCooldown = 0.f;

    void init(const Vec3& p) {
        pos = p;
        mesh = makePlayerMesh();
        hasMesh = true;
    }

    static constexpr float kRadius = 0.4f;

    void ensureFree(World& world) {
        if (inVehicle) return;
        if (world.collides(pos.x, pos.z, kRadius, false)) {
            // if outdoors stuck in solid, push out as car would
            if (world.playerInterior < 0)
                pos = world.resolveSolid(pos.x, pos.z, kRadius);
        }
        pos.y = world.groundY(pos.x, pos.z);
    }

    void updateOnFoot(float dt, const Camera& cam, bool forward, bool back,
                      bool left, bool right, bool run, World& world,
                      bool stairUp, bool stairDown) {
        if (inVehicle) return;
        stairCooldown = std::max(0.f, stairCooldown - dt);

        Vec3 dir(0, 0, 0);
        Vec3 f = cam.forwardFlat();
        Vec3 r = cam.rightFlat();
        if (forward) dir += f;
        if (back)    dir -= f;
        if (right)   dir += r;
        if (left)    dir -= r;
        if (dir.lengthSq() > 1e-6f) {
            dir = dir.normalized();
            yaw = std::atan2(dir.x, dir.z);
            float sp = run ? runSpeed : walkSpeed;
            Vec3 next = pos + dir * (sp * dt);
            if (!world.collides(next.x, next.z, kRadius, false)) {
                pos = world.clampPos(next);
            } else {
                Vec3 nx = pos; nx.x = next.x;
                Vec3 nz = pos; nz.z = next.z;
                if (!world.collides(nx.x, nx.z, kRadius, false)) pos = world.clampPos(nx);
                else if (!world.collides(nz.x, nz.z, kRadius, false)) pos = world.clampPos(nz);
            }
        }

        world.updatePlayerInterior(pos.x, pos.z, pos.y);

        // Stairs: PageUp/PageDown or Q/Z while in stairwell
        if (stairCooldown <= 0.f) {
            if (stairUp && world.tryUseStairs(pos.x, pos.z, true)) {
                stairCooldown = 0.45f;
            } else if (stairDown && world.tryUseStairs(pos.x, pos.z, false)) {
                stairCooldown = 0.45f;
            }
        }

        pos.y = world.groundY(pos.x, pos.z);
    }

    int nearestVehicle(const std::vector<Vehicle>& cars, float maxDist = 7.f) const {
        float best = maxDist;
        int besti = -1;
        for (int i = 0; i < (int)cars.size(); ++i) {
            if (cars[i].occupied) continue;
            float d = Vec3::distance(pos, cars[i].pos);
            if (d < best) { best = d; besti = i; }
        }
        return besti;
    }

    bool tryEnterVehicle(std::vector<Vehicle>& cars) {
        if (inVehicle) return false;
        int besti = nearestVehicle(cars, 7.f);
        if (besti < 0) return false;
        inVehicle = true;
        vehicleIndex = besti;
        cars[besti].occupied = true;
        cars[besti].ai = false;
        cars[besti].speed = 0.f;
        return true;
    }

    void exitVehicle(std::vector<Vehicle>& cars, World& world) {
        if (!inVehicle || vehicleIndex < 0 || vehicleIndex >= (int)cars.size()) return;
        Vehicle& v = cars[vehicleIndex];
        v.occupied = false;
        v.speed = 0.f;
        Vec3 candidates[8] = {
            v.pos - v.right() * 2.6f, v.pos + v.right() * 2.6f,
            v.pos - v.forward() * 3.2f, v.pos + v.forward() * 3.2f,
            v.pos - v.right() * 3.5f - v.forward() * 2.f,
            v.pos + v.right() * 3.5f - v.forward() * 2.f,
            v.pos - v.right() * 3.5f + v.forward() * 2.f,
            v.pos + v.right() * 3.5f + v.forward() * 2.f,
        };
        bool placed = false;
        for (auto& c : candidates) {
            c.y = 0.f;
            if (!world.collides(c.x, c.z, kRadius, false)) {
                pos = world.clampPos(c);
                placed = true;
                break;
            }
        }
        if (!placed) pos = world.resolveSolid(v.pos.x, v.pos.z, kRadius);
        world.playerInterior = -1;
        world.playerFloor = 0;
        pos.y = world.groundY(pos.x, pos.z);
        yaw = v.yaw;
        inVehicle = false;
        vehicleIndex = -1;
        ensureFree(world);
    }

    Vec3 focusPoint(const std::vector<Vehicle>& cars) const {
        if (inVehicle && vehicleIndex >= 0 && vehicleIndex < (int)cars.size())
            return cars[vehicleIndex].pos + Vec3(0, 0.8f, 0);
        return pos + Vec3(0, 0.9f, 0);
    }

    float focusYaw(const std::vector<Vehicle>& cars) const {
        if (inVehicle && vehicleIndex >= 0 && vehicleIndex < (int)cars.size())
            return cars[vehicleIndex].yaw;
        return yaw;
    }

    void draw(const Shader& sh, bool hide = false) const {
        if (inVehicle || !hasMesh || hide) return;
        Mat4 m = Mat4::translate(pos) * Mat4::rotateY(yaw);
        sh.setMat4("uModel", m);
        mesh.draw();
    }

    void destroy() {
        if (hasMesh) mesh.destroy();
        hasMesh = false;
    }
};

struct Pedestrian {
    Vec3 pos;
    float yaw = 0;
    float speed = 1.5f;
    float timer = 0;
    Mesh* mesh = nullptr;

    void update(float dt, World& world) {
        timer -= dt;
        if (timer <= 0) {
            timer = 2.f + (std::rand() % 100) / 40.f;
            yaw += ((std::rand() % 200) - 100) / 100.f * 1.5f;
            speed = 1.2f + (std::rand() % 80) / 100.f;
        }
        Vec3 f(std::sin(yaw), 0, std::cos(yaw));
        Vec3 next = pos + f * (speed * dt);
        if (!world.collides(next.x, next.z, 0.3f, true))
            pos = world.clampPos(next);
        else
            yaw += 1.5f;
        pos.y = 0.f;
    }
};
