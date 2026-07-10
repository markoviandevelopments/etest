#pragma once
#include <GL/glew.h>
#include <vector>
#include <cmath>
#include "math.hpp"

struct Vertex {
    float px, py, pz;
    float nx, ny, nz;
    float r, g, b;
};

struct Mesh {
    GLuint vao = 0, vbo = 0, ebo = 0;
    GLsizei indexCount = 0;

    void destroy() {
        if (ebo) glDeleteBuffers(1, &ebo);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        vao = vbo = ebo = 0;
        indexCount = 0;
    }

    void upload(const std::vector<Vertex>& verts, const std::vector<unsigned>& idx) {
        destroy();
        indexCount = static_cast<GLsizei>(idx.size());
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(Vertex), verts.data(), GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned), idx.data(), GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)(6 * sizeof(float)));
        glBindVertexArray(0);
    }

    void draw() const {
        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }
};

inline void pushBox(std::vector<Vertex>& v, std::vector<unsigned>& idx,
                    float x0, float y0, float z0, float x1, float y1, float z1,
                    float r, float g, float b, float shadeVar = 0.08f) {
    auto addFace = [&](float nx, float ny, float nz,
                       float ax, float ay, float az,
                       float bx, float by, float bz,
                       float cx, float cy, float cz,
                       float dx, float dy, float dz,
                       float mult) {
        unsigned base = static_cast<unsigned>(v.size());
        float rr = clampf(r * mult, 0.f, 1.f);
        float gg = clampf(g * mult, 0.f, 1.f);
        float bb = clampf(b * mult, 0.f, 1.f);
        v.push_back({ax, ay, az, nx, ny, nz, rr, gg, bb});
        v.push_back({bx, by, bz, nx, ny, nz, rr, gg, bb});
        v.push_back({cx, cy, cz, nx, ny, nz, rr, gg, bb});
        v.push_back({dx, dy, dz, nx, ny, nz, rr, gg, bb});
        idx.push_back(base); idx.push_back(base + 1); idx.push_back(base + 2);
        idx.push_back(base); idx.push_back(base + 2); idx.push_back(base + 3);
    };
    // +Y top
    addFace(0, 1, 0, x0,y1,z1, x1,y1,z1, x1,y1,z0, x0,y1,z0, 1.f + shadeVar);
    // -Y bottom
    addFace(0,-1, 0, x0,y0,z0, x1,y0,z0, x1,y0,z1, x0,y0,z1, 1.f - shadeVar * 2.f);
    // +Z
    addFace(0, 0, 1, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1, 1.f);
    // -Z
    addFace(0, 0,-1, x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0, 1.f - shadeVar);
    // +X
    addFace(1, 0, 0, x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1, 1.f + shadeVar * 0.5f);
    // -X
    addFace(-1,0, 0, x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0, 1.f - shadeVar * 0.5f);
}

inline Mesh makeCube(float r, float g, float b) {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    pushBox(verts, idx, -0.5f, -0.5f, -0.5f, 0.5f, 0.5f, 0.5f, r, g, b);
    Mesh m; m.upload(verts, idx); return m;
}

inline Mesh makeGroundPlane(float size, float r, float g, float b, int subdiv = 1) {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    float half = size * 0.5f;
    float step = size / subdiv;
    for (int iz = 0; iz < subdiv; ++iz) {
        for (int ix = 0; ix < subdiv; ++ix) {
            float x0 = -half + ix * step;
            float z0 = -half + iz * step;
            float x1 = x0 + step;
            float z1 = z0 + step;
            // checkerboard tint
            float t = ((ix + iz) & 1) ? 1.f : 0.92f;
            unsigned base = static_cast<unsigned>(verts.size());
            verts.push_back({x0, 0, z0, 0,1,0, r*t, g*t, b*t});
            verts.push_back({x1, 0, z0, 0,1,0, r*t, g*t, b*t});
            verts.push_back({x1, 0, z1, 0,1,0, r*t, g*t, b*t});
            verts.push_back({x0, 0, z1, 0,1,0, r*t, g*t, b*t});
            idx.push_back(base); idx.push_back(base+1); idx.push_back(base+2);
            idx.push_back(base); idx.push_back(base+2); idx.push_back(base+3);
        }
    }
    Mesh m; m.upload(verts, idx); return m;
}

// Car body with more silhouette detail (hood / cabin / bumper / wheels)
inline Mesh makeCarMesh(float r, float g, float b) {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    // lower body
    pushBox(verts, idx, -1.15f, 0.18f, -2.25f, 1.15f, 0.72f, 2.05f, r, g, b, 0.07f);
    // hood (front)
    pushBox(verts, idx, -1.05f, 0.72f, 0.55f, 1.05f, 0.92f, 1.95f, r * 0.95f, g * 0.95f, b * 0.95f, 0.05f);
    // trunk (rear)
    pushBox(verts, idx, -1.05f, 0.72f, -2.1f, 1.05f, 0.95f, -0.55f, r * 0.92f, g * 0.92f, b * 0.92f, 0.05f);
    // cabin
    pushBox(verts, idx, -0.95f, 0.9f, -0.55f, 0.95f, 1.52f, 1.05f, r * 0.82f, g * 0.82f, b * 0.88f, 0.05f);
    // glass
    pushBox(verts, idx, -0.88f, 0.98f, 0.85f, 0.88f, 1.48f, 1.08f, 0.35f, 0.55f, 0.7f, 0.0f);
    pushBox(verts, idx, -0.88f, 0.98f, -0.58f, 0.88f, 1.48f, -0.42f, 0.3f, 0.45f, 0.6f, 0.0f);
    // bumpers
    pushBox(verts, idx, -1.2f, 0.22f, 1.95f, 1.2f, 0.55f, 2.25f, 0.25f, 0.25f, 0.28f, 0.02f);
    pushBox(verts, idx, -1.2f, 0.22f, -2.4f, 1.2f, 0.55f, -2.15f, 0.25f, 0.25f, 0.28f, 0.02f);
    // headlights / taillights
    pushBox(verts, idx, -1.05f, 0.45f, 2.05f, -0.55f, 0.65f, 2.22f, 0.95f, 0.95f, 0.75f, 0.f);
    pushBox(verts, idx,  0.55f, 0.45f, 2.05f,  1.05f, 0.65f, 2.22f, 0.95f, 0.95f, 0.75f, 0.f);
    pushBox(verts, idx, -1.05f, 0.45f, -2.38f, -0.55f, 0.65f, -2.2f, 0.9f, 0.15f, 0.12f, 0.f);
    pushBox(verts, idx,  0.55f, 0.45f, -2.38f,  1.05f, 0.65f, -2.2f, 0.9f, 0.15f, 0.12f, 0.f);
    // wheels
    float wr = 0.1f, wg = 0.1f, wb = 0.12f;
    pushBox(verts, idx, -1.28f, 0.0f, 1.15f, -0.92f, 0.48f, 1.75f, wr, wg, wb);
    pushBox(verts, idx,  0.92f, 0.0f, 1.15f,  1.28f, 0.48f, 1.75f, wr, wg, wb);
    pushBox(verts, idx, -1.28f, 0.0f,-1.75f, -0.92f, 0.48f,-1.15f, wr, wg, wb);
    pushBox(verts, idx,  0.92f, 0.0f,-1.75f,  1.28f, 0.48f,-1.15f, wr, wg, wb);
    Mesh m; m.upload(verts, idx); return m;
}

inline Mesh makePlayerMesh() {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    // legs
    pushBox(verts, idx, -0.22f, 0.0f, -0.12f, -0.02f, 0.55f, 0.12f, 0.15f, 0.2f, 0.45f);
    pushBox(verts, idx,  0.02f, 0.0f, -0.12f,  0.22f, 0.55f, 0.12f, 0.15f, 0.2f, 0.45f);
    // torso (Hawaiian shirt vibe — coral)
    pushBox(verts, idx, -0.28f, 0.55f, -0.16f, 0.28f, 1.15f, 0.16f, 0.95f, 0.35f, 0.4f);
    // head
    pushBox(verts, idx, -0.16f, 1.15f, -0.14f, 0.16f, 1.5f, 0.14f, 0.92f, 0.72f, 0.55f);
    // arms
    pushBox(verts, idx, -0.42f, 0.6f, -0.1f, -0.28f, 1.1f, 0.1f, 0.92f, 0.72f, 0.55f);
    pushBox(verts, idx,  0.28f, 0.6f, -0.1f,  0.42f, 1.1f, 0.1f, 0.92f, 0.72f, 0.55f);
    Mesh m; m.upload(verts, idx); return m;
}

inline Mesh makePalmMesh() {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    // segmented trunk
    pushBox(verts, idx, -0.2f, 0, -0.2f, 0.2f, 1.6f, 0.2f, 0.48f, 0.32f, 0.16f);
    pushBox(verts, idx, -0.17f, 1.5f, -0.17f, 0.17f, 3.2f, 0.17f, 0.45f, 0.3f, 0.14f);
    pushBox(verts, idx, -0.14f, 3.1f, -0.14f, 0.14f, 4.7f, 0.14f, 0.42f, 0.28f, 0.13f);
    // layered fronds
    pushBox(verts, idx, -2.4f, 4.4f, -0.18f, 2.4f, 4.85f, 0.18f, 0.14f, 0.58f, 0.2f);
    pushBox(verts, idx, -0.18f, 4.4f, -2.4f, 0.18f, 4.85f, 2.4f, 0.12f, 0.52f, 0.18f);
    pushBox(verts, idx, -1.7f, 4.55f, -1.7f, 1.7f, 5.05f, 1.7f, 0.18f, 0.62f, 0.22f);
    pushBox(verts, idx, -1.9f, 4.7f, -0.5f, 1.9f, 5.15f, 0.5f, 0.16f, 0.55f, 0.2f);
    // coconuts
    pushBox(verts, idx, -0.35f, 4.3f, -0.35f, 0.35f, 4.7f, 0.35f, 0.35f, 0.22f, 0.08f);
    Mesh m; m.upload(verts, idx); return m;
}

inline Mesh makePedMesh(float r, float g, float b) {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    pushBox(verts, idx, -0.18f, 0, -0.1f, 0.18f, 0.5f, 0.1f, 0.2f, 0.2f, 0.25f);
    pushBox(verts, idx, -0.22f, 0.5f, -0.12f, 0.22f, 1.05f, 0.12f, r, g, b);
    pushBox(verts, idx, -0.14f, 1.05f, -0.12f, 0.14f, 1.35f, 0.12f, 0.9f, 0.7f, 0.55f);
    Mesh m; m.upload(verts, idx); return m;
}
