// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "modelview.h"

#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace ef {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Projected {
    float x = 0.0f, y = 0.0f;  // screen space
    float z = 0.0f;            // view space depth
    bool valid = false;
};

// View-space light direction (normalised), fixed to the camera.
constexpr float kLight[3] = {0.42f, 0.36f, 0.83f};

void fill_rect(SDL_Surface* surface, int x, int y, int w, int h, Uint32 colour) {
    SDL_Rect r{x, y, w, h};
    SDL_FillRect(surface, &r, colour);
}

}  // namespace

void OrbitCamera::rotate(float dx, float dy) {
    yaw += dx;
    pitch = std::clamp(pitch + dy, -1.45f, 1.45f);
}

void OrbitCamera::dolly(float factor) {
    zoom = std::clamp(zoom * factor, 0.25f, 6.0f);
}

void draw_model(SDL_Surface* surface, const Mesh& mesh, const OrbitCamera& camera,
                int ox, int oy, int w, int h, const TextureSlots* textures,
                const Palette* palette) {
    fill_rect(surface, ox, oy, w, h, SDL_MapRGBA(surface->format, 12, 13, 16, 255));
    if (w <= 0 || h <= 0) return;

    // Normalise so the model's radius maps to one unit.
    Vec3f lo{}, hi{};
    if (!mesh.bounds(lo, hi)) return;
    const float centre[3] = {(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f,
                             (lo.z + hi.z) * 0.5f};
    const float ex = hi.x - lo.x, ey = hi.y - lo.y, ez = hi.z - lo.z;
    float radius = 0.5f * std::sqrt(ex * ex + ey * ey + ez * ez);
    if (radius < 1e-6f) radius = 1.0f;

    const float cy = std::cos(camera.yaw), sy = std::sin(camera.yaw);
    const float cp = std::cos(camera.pitch), sp = std::sin(camera.pitch);

    // Camera distance in radii. At zoom 1 a 260-unit sphere fills the frame.
    const float distance = 2.6f / camera.zoom;
    const float focus = (w < h ? w : h) * 1.35f;

    auto project = [&](const Vec3f& p, Projected& out) {
        const float x = (p.x - centre[0]) / radius;
        const float y = (p.y - centre[1]) / radius;
        const float z = (p.z - centre[2]) / radius;

        // Yaw about Y, then pitch about X.
        const float x1 = cy * x + sy * z;
        const float z1 = -sy * x + cy * z;
        const float y2 = cp * y - sp * z1;
        const float z2 = sp * y + cp * z1;

        const float depth = z2 + distance;
        if (depth <= 0.05f) {
            out.valid = false;
            return;
        }
        const float inv = focus / depth;
        out.x = ox + w * 0.5f + x1 * inv;
        out.y = oy + h * 0.5f - y2 * inv;
        out.z = depth;
        out.valid = true;
    };

    auto normal = [&](const Vec3f& a, const Vec3f& b, const Vec3f& c) {
        const float e1[3] = {b.x - a.x, b.y - a.y, b.z - a.z};
        const float e2[3] = {c.x - a.x, c.y - a.y, c.z - a.z};
        float n[3] = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2],
                      e1[0] * e2[1] - e1[1] * e2[0]};
        // Rotate into view space (same order as the projection).
        const float nx = cy * n[0] + sy * n[2];
        const float nz1 = -sy * n[0] + cy * n[2];
        const float ny = cp * n[1] - sp * nz1;
        const float nz = sp * n[1] + cp * nz1;
        n[0] = nx; n[1] = ny; n[2] = nz;
        const float len = std::sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (len < 1e-12f) return 0.0f;
        return std::fabs((n[0] * kLight[0] + n[1] * kLight[1] + n[2] * kLight[2]) / len);
    };

    std::vector<float> zbuf(static_cast<std::size_t>(w) * static_cast<std::size_t>(h),
                            -std::numeric_limits<float>::infinity());
    auto* pixels = static_cast<Uint32*>(surface->pixels);
    const int pitch = surface->pitch / 4;
    const SDL_PixelFormat* fmt = surface->format;

    SDL_LockSurface(surface);

    for (const auto& t : mesh.triangles) {
        if (t.sprite) continue;
        if (t.decal && !camera.decals) continue;
        Projected p0, p1, p2;
        project(t.a, p0);
        project(t.b, p1);
        project(t.c, p2);
        if (!p0.valid || !p1.valid || !p2.valid) continue;

        const float shade = normal(t.a, t.b, t.c);
        // Nearer triangles are *smaller* in z; keep the smallest for the z-buffer.
        const float z0 = -p0.z, z1 = -p1.z, z2 = -p2.z;

        const float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
        if (std::fabs(area) < 1e-6f) continue;
        if (camera.cull && area < 0.0f) continue;

        const int x0 = std::max(static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))), ox);
        const int x1 = std::min(static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x}))), ox + w - 1);
        const int y0 = std::max(static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))), oy);
        const int y1 = std::min(static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y}))), oy + h - 1);
        if (x1 < x0 || y1 < y0) continue;

        const Texture* texture = nullptr;
        if (textures != nullptr && static_cast<std::size_t>(t.texture_slot) < textures->size()) {
            texture = (*textures)[static_cast<std::size_t>(t.texture_slot)];
        }
        const bool sample = texture != nullptr && t.has_uv;
        Uint32 flat_colour;
        if (palette != nullptr && !sample) {
            // EF2000 bakes the shading into the mesh: every flat-shaded polygon
            // carries a palette index on the grey ramp, so the model is drawn
            // with the stored colour instead of a computed light.
            const std::uint32_t rgb = (*palette)[t.colors[0]];
            flat_colour = SDL_MapRGBA(fmt, static_cast<Uint8>((rgb >> 16) & 0xFF),
                                      static_cast<Uint8>((rgb >> 8) & 0xFF),
                                      static_cast<Uint8>(rgb & 0xFF), 255);
        } else {
            const float base = 0.30f + 0.70f * shade;
            flat_colour = SDL_MapRGBA(
                fmt, static_cast<Uint8>(std::clamp(base * 150.0f + 20.0f, 0.0f, 255.0f)),
                static_cast<Uint8>(std::clamp(base * 162.0f + 22.0f, 0.0f, 255.0f)),
                static_cast<Uint8>(std::clamp(base * 178.0f + 26.0f, 0.0f, 255.0f)), 255);
        }

        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float py = static_cast<float>(y) + 0.5f;
                const float w0 = ((p1.x - px) * (p2.y - py) - (p2.x - px) * (p1.y - py)) / area;
                const float w1 = ((p2.x - px) * (p0.y - py) - (p0.x - px) * (p2.y - py)) / area;
                const float w2 = 1.0f - w0 - w1;
                if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;

                Uint32 colour = flat_colour;
                if (sample) {
                    const float u = w0 * t.uv[0][0] + w1 * t.uv[1][0] + w2 * t.uv[2][0];
                    const float v = w0 * t.uv[0][1] + w1 * t.uv[1][1] + w2 * t.uv[2][1];
                    const std::uint32_t texel = texture->at(static_cast<int>(u),
                                                            static_cast<int>(v));
                    if ((texel & 0xFF000000u) == 0) continue;  // masked out
                    colour = SDL_MapRGBA(fmt, static_cast<Uint8>((texel >> 16) & 0xFF),
                                         static_cast<Uint8>((texel >> 8) & 0xFF),
                                         static_cast<Uint8>(texel & 0xFF), 255);
                }

                const float z = w0 * z0 + w1 * z1 + w2 * z2;
                float& slot = zbuf[static_cast<std::size_t>(y - oy) * static_cast<std::size_t>(w) +
                                  static_cast<std::size_t>(x - ox)];
                if (z > slot) {
                    slot = z;
                    pixels[y * pitch + x] = colour;
                }
            }
        }
    }

    if (camera.wireframe) {
        for (const auto& t : mesh.triangles) {
            if (t.sprite) continue;
            if (t.decal && !camera.decals) continue;
            Projected p0, p1, p2;
            project(t.a, p0);
            project(t.b, p1);
            project(t.c, p2);
            if (!p0.valid || !p1.valid || !p2.valid) continue;
            const Uint32 ink = SDL_MapRGBA(fmt, 220, 232, 245, 255);
            // Bresenham for the three edges.
            auto line = [&](const Projected& a, const Projected& b) {
                int xa = static_cast<int>(a.x), ya = static_cast<int>(a.y);
                const int xb = static_cast<int>(b.x), yb = static_cast<int>(b.y);
                const int dx = std::abs(xb - xa), dy = -std::abs(yb - ya);
                const int sx = xa < xb ? 1 : -1, sy = ya < yb ? 1 : -1;
                int err = dx + dy;
                for (int guard = 0; guard < 4096; ++guard) {
                    if (xa >= ox && xa < ox + w && ya >= oy && ya < oy + h) {
                        pixels[ya * pitch + xa] = ink;
                    }
                    if (xa == xb && ya == yb) break;
                    const int e2 = 2 * err;
                    if (e2 >= dy) { err += dy; xa += sx; }
                    if (e2 <= dx) { err += dx; ya += sy; }
                }
            };
            line(p0, p1);
            line(p1, p2);
            line(p2, p0);
        }
    }

    SDL_UnlockSurface(surface);
}

bool render_model_png(const Mesh& mesh, const OrbitCamera& camera,
                      const TextureSlots* textures, const Palette* palette,
                      const std::string& path, int width, int height) {
    SDL_Surface* surface =
        SDL_CreateRGBSurfaceWithFormat(0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return false;
    SDL_FillRect(surface, nullptr, SDL_MapRGB(surface->format, 0, 0, 0));
    draw_model(surface, mesh, camera, 0, 0, width, height, textures, palette);
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(surface);
    if (!converted) return false;
    const bool ok = IMG_SavePNG(converted, path.c_str()) == 0;
    SDL_FreeSurface(converted);
    return ok;
}

}  // namespace ef
