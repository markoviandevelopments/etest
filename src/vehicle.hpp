#pragma once
#include "math.hpp"
#include "mesh.hpp"
#include "world.hpp"
#include <vector>
#include <cmath>
#include <algorithm>

struct Vehicle {
    Vec3 pos{10.f, 0.f, 10.f};
    float yaw = 0.f;
    float speed = 0.f;
    float maxSpeed = 28.f;
    float accel = 18.f;
    float brake = 30.f;
    float friction = 8.f;
    float turnRate = 2.0f;
    bool occupied = false;
    bool ai = false;
    float r = 0.9f, g = 0.2f, b = 0.25f;
    float damage = 0.f;       // 0..1 permanent until repair
    float visualDamage = -1.f; // last baked mesh damage
    Mesh mesh;
    bool hasMesh = false;

    float waypointTimer = 0.f;
    float targetSteer = 0.f;
    float targetThrottle = 0.4f;

    void rebuildMesh() {
        if (hasMesh) mesh.destroy();
        mesh = makeCarMesh(r, g, b, damage);
        hasMesh = true;
        visualDamage = damage;
    }

    void init(const Vec3& p, float y, float cr, float cg, float cb, bool isAi = false) {
        pos = p; yaw = y; r = cr; g = cg; b = cb;
        damage = 0.f;
        rebuildMesh();
        speed = 0;
        occupied = false;
        ai = isAi;
    }

    void applyDamage(float amount) {
        float prev = damage;
        damage = clampf(damage + amount, 0.f, 1.f);
        // rebuild when crossing visual thresholds
        if (std::fabs(damage - visualDamage) > 0.08f ||
            (int)(damage * 5.f) != (int)(prev * 5.f))
            rebuildMesh();
    }

    void repair() {
        damage = 0.f;
        rebuildMesh();
    }

    // max speed drops with damage
    float effectiveMaxSpeed() const {
        return maxSpeed * (1.f - damage * 0.55f);
    }

    Vec3 forward() const {
        return Vec3(std::sin(yaw), 0.f, std::cos(yaw));
    }
    Vec3 right() const {
        return Vec3(-std::cos(yaw), 0.f, std::sin(yaw));
    }

    // returns impact severity this frame (0 if none)
    float update(float dt, float throttle, float steer, World& world,
                 const std::vector<Vehicle>* others = nullptr, int selfIndex = -1) {
        float impact = 0.f;
        float maxSp = effectiveMaxSpeed();

        if (std::abs(throttle) > 0.01f) {
            if (throttle * speed >= 0.f || std::abs(speed) < 1.f)
                speed += throttle * accel * (1.f - damage * 0.3f) * dt;
            else
                speed += throttle * brake * dt;
        } else {
            if (speed > 0) speed = std::max(0.f, speed - friction * dt);
            else if (speed < 0) speed = std::min(0.f, speed + friction * dt);
        }
        speed = clampf(speed, -maxSp * 0.4f, maxSp);

        float turnScale = clampf(std::abs(speed) / 6.f, 0.f, 1.f);
        float s = (speed < 0.f) ? -steer : steer;
        yaw -= s * turnRate * turnScale * (1.f - damage * 0.25f) * dt;

        if (world.collides(pos.x, pos.z, 1.3f, true)) {
            pos = world.resolveSolid(pos.x, pos.z, 1.3f);
            if (std::abs(speed) > 4.f) {
                impact = std::max(impact, std::abs(speed) / maxSpeed);
                applyDamage(0.08f + std::abs(speed) * 0.012f);
            }
            speed *= 0.15f;
        }

        Vec3 next = pos + forward() * (speed * dt);
        next.y = world.groundY(next.x, next.z);
        bool hitBuilding = world.collides(next.x, next.z, 1.3f, true);
        if (!hitBuilding) {
            pos = world.clampPos(next);
        } else {
            float pre = std::abs(speed);
            Vec3 nx = pos; nx.x = next.x;
            Vec3 nz = pos; nz.z = next.z;
            if (!world.collides(nx.x, nx.z, 1.3f, true)) pos = world.clampPos(nx);
            else if (!world.collides(nz.x, nz.z, 1.3f, true)) pos = world.clampPos(nz);
            else pos = world.resolveSolid(pos.x, pos.z, 1.3f);
            if (pre > 3.5f) {
                impact = std::max(impact, pre / maxSpeed);
                applyDamage(0.06f + pre * 0.015f);
            }
            speed *= -0.15f; // bounce back a bit
        }

        // Car-car collisions
        if (others) {
            for (int i = 0; i < (int)others->size(); ++i) {
                if (i == selfIndex) continue;
                Vehicle& o = const_cast<Vehicle&>((*others)[i]);
                float d = Vec3::distance(pos, o.pos);
                if (d < 3.2f && d > 0.01f) {
                    float rel = std::abs(speed) + std::abs(o.speed);
                    if (rel > 2.f) {
                        float sev = clampf(rel / 40.f, 0.05f, 0.45f);
                        applyDamage(sev * 0.55f);
                        o.applyDamage(sev * 0.45f);
                        impact = std::max(impact, sev);
                        // separate
                        Vec3 away = (pos - o.pos).normalized();
                        pos += away * 0.15f;
                        o.pos -= away * 0.15f;
                        float bounce = rel * 0.25f;
                        speed = -std::copysign(std::min(std::abs(speed) * 0.4f + 1.f, bounce), speed);
                        o.speed = -std::copysign(std::min(std::abs(o.speed) * 0.4f + 1.f, bounce), o.speed);
                        if (rel > 10.f) o.yaw += 0.35f * (speed >= 0 ? 1.f : -1.f);
                    } else if (d < 2.6f) {
                        Vec3 away = (pos - o.pos).normalized();
                        pos += away * 0.08f;
                    }
                }
            }
        }

        pos.y = world.groundY(pos.x, pos.z);
        return impact;
    }

    void updateAI(float dt, World& world, const std::vector<Vehicle>* others = nullptr, int selfIndex = -1) {
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
        update(dt, targetThrottle, targetSteer, world, others, selfIndex);
        speed = clampf(speed, 0.f, 14.f * (1.f - damage * 0.5f));
    }

    Mat4 modelMatrix() const {
        // damaged cars list slightly
        float list = damage * 0.08f;
        return Mat4::translate(pos + Vec3(0, 0.02f, 0)) *
               Mat4::rotateY(yaw) *
               Mat4::rotateZ(list) *
               Mat4::rotateX(-list * 0.5f);
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
