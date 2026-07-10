#pragma once
#include <GL/glew.h>
#include <vector>
#include <cmath>
#include <cstdlib>
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
    addFace(0, 1, 0, x0,y1,z1, x1,y1,z1, x1,y1,z0, x0,y1,z0, 1.f + shadeVar);
    addFace(0,-1, 0, x0,y0,z0, x1,y0,z0, x1,y0,z1, x0,y0,z1, 1.f - shadeVar * 2.f);
    addFace(0, 0, 1, x0,y0,z1, x1,y0,z1, x1,y1,z1, x0,y1,z1, 1.f);
    addFace(0, 0,-1, x1,y0,z0, x0,y0,z0, x0,y1,z0, x1,y1,z0, 1.f - shadeVar);
    addFace(1, 0, 0, x1,y0,z1, x1,y0,z0, x1,y1,z0, x1,y1,z1, 1.f + shadeVar * 0.5f);
    addFace(-1,0, 0, x0,y0,z0, x0,y0,z1, x0,y1,z1, x0,y1,z0, 1.f - shadeVar * 0.5f);
}

// damage 0..1 permanent visual wreck
inline Mesh makeCarMesh(float r, float g, float b, float damage = 0.f) {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    float d = clampf(damage, 0.f, 1.f);
    // body sinks / darkens with damage
    float dark = 1.f - d * 0.55f;
    float br = r * dark, bg = g * dark, bb = b * dark;
    float crush = d * 0.35f;
    float hoodDrop = d * 0.28f;
    float lean = d * 0.12f;

    pushBox(verts, idx, -1.15f - lean, 0.18f, -2.25f + crush * 0.3f,
            1.15f + lean * 0.5f, 0.72f - d * 0.08f, 2.05f - crush * 0.15f, br, bg, bb, 0.07f);
    // hood crumpled
    pushBox(verts, idx, -1.05f, 0.72f - hoodDrop, 0.55f,
            1.05f, 0.92f - hoodDrop * 1.2f, 1.95f - crush,
            br * 0.9f, bg * 0.9f, bb * 0.9f, 0.05f);
    pushBox(verts, idx, -1.05f, 0.72f, -2.1f, 1.05f, 0.95f - d * 0.1f, -0.55f,
            br * 0.92f, bg * 0.92f, bb * 0.92f, 0.05f);
    pushBox(verts, idx, -0.95f, 0.9f - hoodDrop * 0.4f, -0.55f,
            0.95f, 1.52f - d * 0.15f, 1.05f - crush * 0.4f,
            br * 0.82f, bg * 0.82f, bb * 0.88f, 0.05f);
    // glass — cracked dark when damaged
    float gr = lerpf(0.35f, 0.15f, d), gg = lerpf(0.55f, 0.18f, d), gb = lerpf(0.7f, 0.2f, d);
    pushBox(verts, idx, -0.88f, 0.98f - hoodDrop * 0.3f, 0.85f - crush * 0.3f,
            0.88f, 1.48f - d * 0.12f, 1.08f - crush * 0.3f, gr, gg, gb, 0.f);
    pushBox(verts, idx, -0.88f, 0.98f, -0.58f, 0.88f, 1.48f - d * 0.1f, -0.42f,
            gr * 0.9f, gg * 0.9f, gb * 0.9f, 0.f);
    pushBox(verts, idx, -1.2f, 0.22f, 1.95f - crush, 1.2f, 0.55f - d * 0.1f, 2.25f - crush,
            0.25f, 0.25f, 0.28f, 0.02f);
    pushBox(verts, idx, -1.2f, 0.22f, -2.4f, 1.2f, 0.55f, -2.15f, 0.25f, 0.25f, 0.28f, 0.02f);

    // lights break with damage
    if (d < 0.35f) {
        pushBox(verts, idx, -1.05f, 0.45f, 2.05f, -0.55f, 0.65f, 2.22f, 0.95f, 0.95f, 0.75f, 0.f);
        pushBox(verts, idx,  0.55f, 0.45f, 2.05f,  1.05f, 0.65f, 2.22f, 0.95f, 0.95f, 0.75f, 0.f);
    } else if (d < 0.7f) {
        // one headlight smashed
        pushBox(verts, idx, -1.05f, 0.45f, 2.05f, -0.55f, 0.65f, 2.22f, 0.2f, 0.15f, 0.1f, 0.f);
        pushBox(verts, idx,  0.55f, 0.45f, 2.05f,  1.05f, 0.65f, 2.22f, 0.95f, 0.95f, 0.75f, 0.f);
    } else {
        pushBox(verts, idx, -1.05f, 0.45f, 2.05f, -0.55f, 0.65f, 2.22f, 0.15f, 0.12f, 0.1f, 0.f);
        pushBox(verts, idx,  0.55f, 0.45f, 2.05f,  1.05f, 0.65f, 2.22f, 0.12f, 0.1f, 0.08f, 0.f);
    }
    float tr = lerpf(0.9f, 0.25f, d);
    pushBox(verts, idx, -1.05f, 0.45f, -2.38f, -0.55f, 0.65f, -2.2f, tr, 0.15f, 0.12f, 0.f);
    pushBox(verts, idx,  0.55f, 0.45f, -2.38f,  1.05f, 0.65f, -2.2f, tr, 0.15f, 0.12f, 0.f);

    float wr = 0.1f, wg = 0.1f, wb = 0.12f;
    pushBox(verts, idx, -1.28f, 0.0f, 1.15f, -0.92f, 0.48f, 1.75f, wr, wg, wb);
    pushBox(verts, idx,  0.92f, 0.0f, 1.15f,  1.28f, 0.48f, 1.75f, wr, wg, wb);
    pushBox(verts, idx, -1.28f, 0.0f,-1.75f, -0.92f, 0.48f,-1.15f, wr, wg, wb);
    pushBox(verts, idx,  0.92f, 0.0f,-1.75f,  1.28f, 0.48f,-1.15f, wr, wg, wb);

    // permanent dent panels
    if (d > 0.15f) {
        float dd = d * 0.4f;
        pushBox(verts, idx, -1.22f, 0.3f, 0.2f, -1.05f - dd, 0.65f, 1.2f, 0.12f, 0.1f, 0.1f, 0.f);
        pushBox(verts, idx, 1.05f + dd * 0.5f, 0.25f, -1.0f, 1.25f, 0.6f, 0.3f, 0.15f, 0.12f, 0.1f, 0.f);
    }
    if (d > 0.4f) {
        // scrap / hood buckle
        pushBox(verts, idx, -0.4f, 0.85f - hoodDrop, 1.0f, 0.5f, 1.05f - hoodDrop, 1.6f,
                0.2f, 0.18f, 0.16f, 0.f);
    }
    if (d > 0.65f) {
        // smoke-ish dark roof scar
        pushBox(verts, idx, -0.6f, 1.35f - d * 0.1f, -0.2f, 0.6f, 1.5f, 0.6f, 0.08f, 0.08f, 0.09f, 0.f);
    }

    Mesh m; m.upload(verts, idx); return m;
}

inline Mesh makePlayerMesh() {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    pushBox(verts, idx, -0.22f, 0.0f, -0.12f, -0.02f, 0.55f, 0.12f, 0.15f, 0.2f, 0.45f);
    pushBox(verts, idx,  0.02f, 0.0f, -0.12f,  0.22f, 0.55f, 0.12f, 0.15f, 0.2f, 0.45f);
    pushBox(verts, idx, -0.28f, 0.55f, -0.16f, 0.28f, 1.15f, 0.16f, 0.95f, 0.35f, 0.4f);
    pushBox(verts, idx, -0.16f, 1.15f, -0.14f, 0.16f, 1.5f, 0.14f, 0.92f, 0.72f, 0.55f);
    pushBox(verts, idx, -0.42f, 0.6f, -0.1f, -0.28f, 1.1f, 0.1f, 0.92f, 0.72f, 0.55f);
    pushBox(verts, idx,  0.28f, 0.6f, -0.1f,  0.42f, 1.1f, 0.1f, 0.92f, 0.72f, 0.55f);
    Mesh m; m.upload(verts, idx); return m;
}

inline Mesh makePalmMesh() {
    std::vector<Vertex> verts;
    std::vector<unsigned> idx;
    pushBox(verts, idx, -0.2f, 0, -0.2f, 0.2f, 1.6f, 0.2f, 0.48f, 0.32f, 0.16f);
    pushBox(verts, idx, -0.17f, 1.5f, -0.17f, 0.17f, 3.2f, 0.17f, 0.45f, 0.3f, 0.14f);
    pushBox(verts, idx, -0.14f, 3.1f, -0.14f, 0.14f, 4.7f, 0.14f, 0.42f, 0.28f, 0.13f);
    pushBox(verts, idx, -2.4f, 4.4f, -0.18f, 2.4f, 4.85f, 0.18f, 0.14f, 0.58f, 0.2f);
    pushBox(verts, idx, -0.18f, 4.4f, -2.4f, 0.18f, 4.85f, 2.4f, 0.12f, 0.52f, 0.18f);
    pushBox(verts, idx, -1.7f, 4.55f, -1.7f, 1.7f, 5.05f, 1.7f, 0.18f, 0.62f, 0.22f);
    pushBox(verts, idx, -1.9f, 4.7f, -0.5f, 1.9f, 5.15f, 0.5f, 0.16f, 0.55f, 0.2f);
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

// Furniture pieces for interiors
inline void pushFurniture(std::vector<Vertex>& v, std::vector<unsigned>& i,
                          float ox, float oy, float oz, int kind) {
    if (kind == 0) { // sofa
        pushBox(v, i, ox - 1.2f, oy, oz - 0.4f, ox + 1.2f, oy + 0.45f, oz + 0.4f, 0.55f, 0.25f, 0.35f);
        pushBox(v, i, ox - 1.2f, oy + 0.45f, oz + 0.15f, ox + 1.2f, oy + 0.95f, oz + 0.4f, 0.5f, 0.22f, 0.32f);
    } else if (kind == 1) { // table + chairs
        pushBox(v, i, ox - 0.7f, oy + 0.55f, oz - 0.7f, ox + 0.7f, oy + 0.65f, oz + 0.7f, 0.45f, 0.3f, 0.18f);
        pushBox(v, i, ox - 0.65f, oy, oz - 0.65f, ox - 0.55f, oy + 0.55f, oz - 0.55f, 0.35f, 0.25f, 0.15f);
        pushBox(v, i, ox + 0.55f, oy, oz + 0.55f, ox + 0.65f, oy + 0.55f, oz + 0.65f, 0.35f, 0.25f, 0.15f);
    } else if (kind == 2) { // bed
        pushBox(v, i, ox - 1.0f, oy, oz - 1.5f, ox + 1.0f, oy + 0.35f, oz + 1.5f, 0.7f, 0.75f, 0.85f);
        pushBox(v, i, ox - 1.0f, oy + 0.35f, oz + 0.9f, ox + 1.0f, oy + 0.55f, oz + 1.5f, 0.9f, 0.9f, 0.95f);
    } else if (kind == 3) { // shelf / counter
        pushBox(v, i, ox - 1.4f, oy, oz - 0.3f, ox + 1.4f, oy + 0.9f, oz + 0.3f, 0.4f, 0.35f, 0.3f);
        pushBox(v, i, ox - 1.35f, oy + 0.9f, oz - 0.28f, ox + 1.35f, oy + 1.4f, oz + 0.28f, 0.55f, 0.55f, 0.6f);
    } else { // plant pot
        pushBox(v, i, ox - 0.25f, oy, oz - 0.25f, ox + 0.25f, oy + 0.4f, oz + 0.25f, 0.5f, 0.3f, 0.2f);
        pushBox(v, i, ox - 0.35f, oy + 0.35f, oz - 0.35f, ox + 0.35f, oy + 0.9f, oz + 0.35f, 0.15f, 0.55f, 0.2f);
    }
}
