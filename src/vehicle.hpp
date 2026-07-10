#pragma once
#include "math.hpp"
#include "mesh.hpp"
#include "world.hpp"

struct Vehicle {
    Vec3 pos{10.f, 0.f, 10.f};
    float yaw = 0.f; // 0 = +Z (matches mesh +Z front after rotateY)
    float speed = 0.f;
    float maxSpeed = 28.f;
    float accel = 18.f;
    float brake = 30.f;
    float friction = 8.f;
    float turnRate = 2.0f;
    bool occupied = false;
    bool ai = false;
    float r = 0.9f, g = 0.2f, b = 0.25f;
    Mesh mesh;
    bool hasMesh = false;

    // AI state
    float waypointTimer = 0.f;
    float targetSteer = 0.f;
    float targetThrottle = 0.4f;

    void init(const Vec3& p, float y, float cr, float cg, float cb, bool isAi = false) {
        pos = p; yaw = y; r = cr; g = cg; b = cb;
        mesh = makeCarMesh(r, g, b);
        hasMesh = true;
        speed = 0;
        occupied = false;
        ai = isAi;
    }

    // World forward for yaw=0 → +Z, yaw=+π/2 → +X (matches rotateY on mesh)
    Vec3 forward() const {
        return Vec3(std::sin(yaw), 0.f, std::cos(yaw));
    }

    Vec3 right() const {
        // Match camera/player right: forward=(sin,0,cos) → right=(-cos,0,sin)
        return Vec3(-std::cos(yaw), 0.f, std::sin(yaw));
    }

    void update(float dt, float throttle, float steer, World& world) {
        if (std::abs(throttle) > 0.01f) {
            if (throttle * speed >= 0.f || std::abs(speed) < 1.f)
                speed += throttle * accel * dt;
            else
                speed += throttle * brake * dt;
        } else {
            if (speed > 0) speed = std::max(0.f, speed - friction * dt);
            else if (speed < 0) speed = std::min(0.f, speed + friction * dt);
        }
        speed = clampf(speed, -maxSpeed * 0.4f, maxSpeed);

        float turnScale = clampf(std::abs(speed) / 6.f, 0.f, 1.f);
        // Steer convention: positive = turn right (D). Flip when reversing.
        // Yaw increases toward +X; facing +Z, right is -X → decrease yaw to turn right.
        float s = (speed < 0.f) ? -steer : steer;
        yaw -= s * turnRate * turnScale * dt;

        // Unstick if already inside a building (bad spawn / desync)
        if (world.collides(pos.x, pos.z, 1.3f)) {
            pos = world.resolveSolid(pos.x, pos.z, 1.3f);
            speed *= 0.2f;
        }

        Vec3 next = pos + forward() * (speed * dt);
        next.y = world.groundY(next.x, next.z);
        if (!world.collides(next.x, next.z, 1.3f)) {
            pos = world.clampPos(next);
        } else {
            Vec3 nx = pos; nx.x = next.x;
            Vec3 nz = pos; nz.z = next.z;
            if (!world.collides(nx.x, nx.z, 1.3f)) pos = world.clampPos(nx);
            else if (!world.collides(nz.x, nz.z, 1.3f)) pos = world.clampPos(nz);
            else pos = world.resolveSolid(pos.x, pos.z, 1.3f);
            speed *= 0.3f;
        }
        pos.y = world.groundY(pos.x, pos.z);
    }

    void updateAI(float dt, World& world) {
        if (occupied || !ai) return;
        waypointTimer -= dt;
        if (waypointTimer <= 0.f) {
            waypointTimer = 1.5f + (std::rand() % 100) / 50.f;
            targetSteer = ((std::rand() % 200) - 100) / 100.f * 0.45f;
            targetThrottle = 0.3f + (std::rand() % 50) / 100.f;
            float bx = std::fmod(std::abs(pos.x), World::BLOCK);
            float bz = std::fmod(std::abs(pos.z), World::BLOCK);
            if (bx > World::ROAD && bz > World::ROAD)
                targetSteer = (bx < bz) ? 0.65f : -0.65f;
        }
        update(dt, targetThrottle, targetSteer, world);
        speed = clampf(speed, 0.f, 14.f);
    }

    Mat4 modelMatrix() const {
        return Mat4::translate(pos + Vec3(0, 0.02f, 0)) * Mat4::rotateY(yaw);
    }

    void draw(const Shader& sh) const {
        if (!hasMesh) return;
        sh.setMat4("uModel", modelMatrix());
        mesh.draw();
    }

    void destroy() {
        if (hasMesh) mesh.destroy();
        hasMesh = false;
    }
};
