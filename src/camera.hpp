#pragma once
#include "math.hpp"

// Yaw convention (shared with vehicles/player):
//   yaw = 0     → face +Z
//   yaw = +π/2  → face +X
// Camera sits behind the look direction and orbits with the mouse.
struct Camera {
    float yaw = 0.f;
    float pitch = 0.28f;
    float distance = 9.f;
    float minDist = 3.f;
    float maxDist = 22.f;
    float mouseSens = 0.0022f;
    Vec3 target{0, 1.2f, 0};
    Vec3 position{0, 5, -9};

    void orbit(float dx, float dy) {
        // mouse right → look right (increase yaw toward +X)
        yaw += dx * mouseSens;
        pitch += dy * mouseSens;
        pitch = clampf(pitch, 0.05f, 1.25f);
    }

    void zoom(float scroll) {
        distance = clampf(distance - scroll * 1.2f, minDist, maxDist);
    }

    // Flat look direction (where W should walk / where the camera faces on XZ)
    Vec3 forwardFlat() const {
        return Vec3(std::sin(yaw), 0.f, std::cos(yaw));
    }

    // Strafe right (D) — perpendicular to forward, pointing to the player's right
    Vec3 rightFlat() const {
        // forward = (sin(yaw), 0, cos(yaw)); right = (-forward.z, 0, forward.x) was inverted
        return Vec3(-std::cos(yaw), 0.f, std::sin(yaw));
    }

    void update(const Vec3& focus, float /*focusYaw*/) {
        target = Vec3::lerp(target, focus + Vec3(0, 1.25f, 0), 0.28f);
        float cp = std::cos(pitch);
        float sp = std::sin(pitch);
        // Sit behind the look vector: -forward * dist on XZ, raised by pitch
        float sy = std::sin(yaw);
        float cy = std::cos(yaw);
        Vec3 offset(-sy * cp * distance, sp * distance, -cy * cp * distance);
        position = target + offset;
    }

    Mat4 view() const {
        return Mat4::lookAt(position, target, Vec3(0, 1, 0));
    }
};
