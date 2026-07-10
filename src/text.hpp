#pragma once
// FreeType-backed HUD / nameplate text renderer (screen-space NDC quads)
#include <GL/glew.h>
#include <ft2build.h>
#include FT_FREETYPE_H

#include <string>
#include <vector>
#include <unordered_map>
#include <iostream>
#include <cmath>
#include <cstring>
#include <algorithm>

#include "math.hpp"
#include "shader.hpp"

struct Glyph {
    float ax;      // advance x (pixels)
    float bw, bh;  // bitmap size
    float bl, bt;  // bearing
    float tx0, ty0, tx1, ty1; // atlas UVs
};

class TextRenderer {
public:
    GLuint prog = 0;
    GLuint vao = 0, vbo = 0;
    GLuint atlas = 0;
    int atlasW = 0, atlasH = 0;
    float pixelSize = 28.f;
    std::unordered_map<char, Glyph> glyphs;
    bool ok = false;

    static const char* VERT() {
        return R"(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
layout(location=2) in vec4 aCol;
out vec2 vUV;
out vec4 vCol;
void main() {
    vUV = aUV;
    vCol = aCol;
    gl_Position = vec4(aPos, 0.0, 1.0);
}
)";
    }
    static const char* FRAG() {
        return R"(
#version 330 core
in vec2 vUV;
in vec4 vCol;
uniform sampler2D uAtlas;
out vec4 FragColor;
void main() {
    float a = texture(uAtlas, vUV).r;
    if (a < 0.05) discard;
    FragColor = vec4(vCol.rgb, vCol.a * a);
}
)";
    }

    bool init(float sizePx = 28.f, const char* fontPath = nullptr) {
        pixelSize = sizePx;
        const char* candidates[] = {
            fontPath, // may be null — skipped below
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/fonts-go/Go-Regular.ttf",
            "/usr/share/fonts/fonts-go/Go-Bold.ttf",
            "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
            "/usr/share/fonts/opentype/freefont/FreeSans.otf",
        };
        const int nCandidates = static_cast<int>(sizeof(candidates) / sizeof(candidates[0]));

        FT_Library ft;
        if (FT_Init_FreeType(&ft)) {
            std::cerr << "FreeType init failed\n";
            return false;
        }

        FT_Face face = nullptr;
        const char* used = nullptr;
        for (int i = 0; i < nCandidates; ++i) {
            if (!candidates[i] || !candidates[i][0]) continue;
            if (FT_New_Face(ft, candidates[i], 0, &face) == 0) {
                used = candidates[i];
                break;
            }
            face = nullptr;
        }
        if (!face) {
            std::cerr << "No usable TTF font found for HUD text\n";
            FT_Done_FreeType(ft);
            return false;
        }
        FT_Set_Pixel_Sizes(face, 0, static_cast<FT_UInt>(pixelSize));
        std::cout << "Text font: " << used << " @ " << pixelSize << "px\n";

        // Pack glyphs into a simple atlas (row wrap)
        const int pad = 2;
        int x = pad, y = pad, rowH = 0;
        atlasW = 1024;
        atlasH = 512;
        std::vector<unsigned char> pixels(atlasW * atlasH, 0);

        for (unsigned char c = 32; c < 127; ++c) {
            if (FT_Load_Char(face, c, FT_LOAD_RENDER)) continue;
            FT_GlyphSlot g = face->glyph;
            int w = static_cast<int>(g->bitmap.width);
            int h = static_cast<int>(g->bitmap.rows);
            if (x + w + pad >= atlasW) {
                x = pad;
                y += rowH + pad;
                rowH = 0;
            }
            if (y + h + pad >= atlasH) {
                std::cerr << "Font atlas overflow\n";
                break;
            }
            // FreeType bitmaps are top-down. glTexImage2D treats the first row as
            // the texture bottom (V=0). Copy top-down into ascending rows so the
            // glyph top lands at lower V; draw() maps top vertices to ty0.
            for (int row = 0; row < h; ++row) {
                std::memcpy(&pixels[(y + row) * atlasW + x],
                            g->bitmap.buffer + row * g->bitmap.pitch,
                            static_cast<size_t>(w));
            }
            Glyph gl{};
            gl.ax = static_cast<float>(g->advance.x >> 6);
            gl.bw = static_cast<float>(w);
            gl.bh = static_cast<float>(h);
            gl.bl = static_cast<float>(g->bitmap_left);
            gl.bt = static_cast<float>(g->bitmap_top);
            gl.tx0 = static_cast<float>(x) / atlasW;
            gl.ty0 = static_cast<float>(y) / atlasH;           // FreeType top → lower V
            gl.tx1 = static_cast<float>(x + w) / atlasW;
            gl.ty1 = static_cast<float>(y + h) / atlasH;       // FreeType bottom → higher V
            glyphs[static_cast<char>(c)] = gl;

            x += w + pad;
            rowH = std::max(rowH, h);
        }

        FT_Done_Face(face);
        FT_Done_FreeType(ft);

        glGenTextures(1, &atlas);
        glBindTexture(GL_TEXTURE_2D, atlas);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, atlasW, atlasH, 0, GL_RED, GL_UNSIGNED_BYTE, pixels.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        prog = makeProgram(VERT(), FRAG());
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        // pos2 uv2 col4
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(2 * sizeof(float)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(4 * sizeof(float)));
        glBindVertexArray(0);

        ok = true;
        return true;
    }

    void destroy() {
        if (atlas) glDeleteTextures(1, &atlas);
        if (vbo) glDeleteBuffers(1, &vbo);
        if (vao) glDeleteVertexArrays(1, &vao);
        if (prog) glDeleteProgram(prog);
        atlas = vao = vbo = prog = 0;
        ok = false;
    }

    // Measure string width in NDC units at given pixel height scale relative to window
    float measureNdc(const std::string& text, float scale, int fbW) const {
        if (!ok || fbW <= 0) return 0.f;
        float wPx = 0.f;
        for (char c : text) {
            auto it = glyphs.find(c);
            if (it == glyphs.end()) continue;
            wPx += it->second.ax * scale;
        }
        return (wPx / static_cast<float>(fbW)) * 2.f;
    }

    // Draw text: (nx, ny) is baseline-left in NDC. scale multiplies glyph size.
    void draw(const std::string& text, float nx, float ny, float scale,
              float r, float g, float b, float a, int fbW, int fbH) {
        if (!ok || text.empty() || fbW <= 0 || fbH <= 0) return;

        std::vector<float> verts;
        verts.reserve(text.size() * 6 * 8);

        float penX = 0.f; // pixels from start
        auto toNdcX = [&](float px) {
            return nx + (px / static_cast<float>(fbW)) * 2.f;
        };
        auto toNdcY = [&](float py) {
            // py is offset up from baseline in pixels
            return ny + (py / static_cast<float>(fbH)) * 2.f;
        };

        for (char c : text) {
            auto it = glyphs.find(c);
            if (it == glyphs.end()) {
                penX += pixelSize * 0.4f * scale;
                continue;
            }
            const Glyph& gl = it->second;
            float x0 = penX + gl.bl * scale;
            float y0 = gl.bt * scale;          // top relative to baseline
            float x1 = x0 + gl.bw * scale;
            float y1 = y0 - gl.bh * scale;     // bottom

            float X0 = toNdcX(x0), X1 = toNdcX(x1);
            float Y0 = toNdcY(y0), Y1 = toNdcY(y1);

            auto push = [&](float x, float y, float u, float v) {
                verts.push_back(x); verts.push_back(y);
                verts.push_back(u); verts.push_back(v);
                verts.push_back(r); verts.push_back(g); verts.push_back(b); verts.push_back(a);
            };
            // tri 1
            push(X0, Y0, gl.tx0, gl.ty0);
            push(X0, Y1, gl.tx0, gl.ty1);
            push(X1, Y1, gl.tx1, gl.ty1);
            // tri 2
            push(X0, Y0, gl.tx0, gl.ty0);
            push(X1, Y1, gl.tx1, gl.ty1);
            push(X1, Y0, gl.tx1, gl.ty0);

            penX += gl.ax * scale;
        }

        if (verts.empty()) return;

        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, atlas);
        glUniform1i(glGetUniformLocation(prog, "uAtlas"), 0);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 8));
        glBindVertexArray(0);
    }

    // Centered horizontally at (cx, cy) baseline
    void drawCentered(const std::string& text, float cx, float cy, float scale,
                      float r, float g, float b, float a, int fbW, int fbH) {
        float w = measureNdc(text, scale, fbW);
        draw(text, cx - w * 0.5f, cy, scale, r, g, b, a, fbW, fbH);
    }

    // Draw with dark drop shadow for readability
    void drawShadowed(const std::string& text, float nx, float ny, float scale,
                      float r, float g, float b, float a, int fbW, int fbH) {
        float ox = (1.5f / fbW) * 2.f;
        float oy = (-1.5f / fbH) * 2.f;
        draw(text, nx + ox, ny + oy, scale, 0.f, 0.f, 0.f, a * 0.75f, fbW, fbH);
        draw(text, nx, ny, scale, r, g, b, a, fbW, fbH);
    }

    void drawCenteredShadowed(const std::string& text, float cx, float cy, float scale,
                              float r, float g, float b, float a, int fbW, int fbH) {
        float w = measureNdc(text, scale, fbW);
        drawShadowed(text, cx - w * 0.5f, cy, scale, r, g, b, a, fbW, fbH);
    }
};

// Project world point through VP to NDC; returns false if behind camera
inline bool worldToNdc(const Mat4& view, const Mat4& proj, const Vec3& world,
                       float& ndcX, float& ndcY) {
    Mat4 vp = proj * view;
    const float* m = vp.data();
    float x = m[0]*world.x + m[4]*world.y + m[8]*world.z  + m[12];
    float y = m[1]*world.x + m[5]*world.y + m[9]*world.z  + m[13];
    float z = m[2]*world.x + m[6]*world.y + m[10]*world.z + m[14];
    float w = m[3]*world.x + m[7]*world.y + m[11]*world.z + m[15];
    if (w <= 0.05f) return false;
    ndcX = x / w;
    ndcY = y / w;
    if (z / w < -1.f || z / w > 1.f) return false;
    return true;
}

inline std::string randomUsername(unsigned seed) {
    static const char* adj[] = {
        "Neon", "Palm", "Coral", "Vice", "Sunset", "Turbo", "Chrome", "Miami",
        "Flamingo", "Pastel", "Night", "Ocean", "Heat", "Pixel", "Lucky", "Wild"
    };
    static const char* noun[] = {
        "Rider", "Ace", "Kid", "Wolf", "Fox", "Drift", "Pulse", "Wave",
        "Ghost", "King", "Nova", "Blaze", "Dash", "Storm", "Byte", "Star"
    };
    unsigned a = seed * 2654435761u;
    unsigned n = (seed * 1597334677u) ^ 0x9E3779B9u;
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%s%s%u",
                  adj[a % 16], noun[n % 16], (seed % 90u) + 10u);
    return std::string(buf);
}
