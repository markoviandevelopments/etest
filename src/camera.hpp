#pragma once
#include "math.hpp"

// Yaw: 0 → +Z, +π/2 → +X
struct Camera {
    float yaw = 0.f;
    float pitch = 0.28f; // 3rd: orbit elevation; 1st: look pitch (radians)
    float distance = 9.f;
    float minDist = 3.f;
    float maxDist = 22.f;
    float mouseSens = 0.0022f;
    bool firstPerson = false;
    Vec3 target{0, 1.2f, 0};
    Vec3 position{0, 5, -9};

    void orbit(float dx, float dy) {
        // Mouse left → look left
        yaw -= dx * mouseSens;
        if (firstPerson) {
            pitch += dy * mouseSens;
            pitch = clampf(pitch, -1.25f, 1.25f);
        } else {
            pitch += dy * mouseSens;
            pitch = clampf(pitch, 0.05f, 1.25f);
        }
    }

    void zoom(float scroll) {
        if (firstPerson) return;
        distance = clampf(distance - scroll * 1.2f, minDist, maxDist);
    }

    void toggleFirstPerson() {
        firstPerson = !firstPerson;
        if (firstPerson) pitch = 0.f;
        else pitch = 0.28f;
    }

    Vec3 forwardFlat() const {
        return Vec3(std::sin(yaw), 0.f, std::cos(yaw));
    }
    Vec3 rightFlat() const {
        return Vec3(-std::cos(yaw), 0.f, std::sin(yaw));
    }

    // Full 3D look direction (for FP)
    Vec3 lookDir() const {
        float cp = std::cos(pitch);
        float sp = std::sin(pitch);
        return Vec3(std::sin(yaw) * cp, -sp, std::cos(yaw) * cp);
    }

    void update(const Vec3& focus, float /*focusYaw*/, bool inVehicle = false) {
        if (firstPerson) {
            float eyeH = inVehicle ? 1.15f : 1.6f;
            Vec3 eye = focus;
            eye.y = focus.y - (inVehicle ? 0.2f : 0.f) + (inVehicle ? 0.f : 0.7f);
            // focus already near body center; lift to eyes
            if (!inVehicle) eye = Vec3(focus.x, focus.y + 0.65f, focus.z);
            else eye = Vec3(focus.x, focus.y + 0.55f, focus.z);
            (void)eyeH;
            position = eye;
            target = position + lookDir() * 8.f;
            return;
        }
        target = Vec3::lerp(target, focus + Vec3(0, 1.25f, 0), 0.28f);
        float cp = std::cos(pitch);
        float sp = std::sin(pitch);
        float sy = std::sin(yaw);
        float cy = std::cos(yaw);
        Vec3 offset(-sy * cp * distance, sp * distance, -cy * cp * distance);
        position = target + offset;
    }

    Mat4 view() const {
        return Mat4::lookAt(position, target, Vec3(0, 1, 0));
    }
};
