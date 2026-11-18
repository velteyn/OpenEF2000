#include "meshview.h"

#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

namespace ef {
namespace {

struct P2 {
    float x, y;
};

struct View {
    int a, b, depth;
    bool flip_x;
};

}  // namespace

std::string mesh_to_obj(const Mesh& mesh, const std::string& name, bool include_sprites) {
    std::string out;
    out.reserve(mesh.triangles.size() * 64);
    out += "# ";
    out += name;
    out += "\no ";
    out += name;
    out += "\n";

    std::vector<Vec3f> vertices;
    std::string faces;

    auto vertex = [&](const Vec3f& p) {
        for (std::size_t i = 0; i < vertices.size(); ++i) {
            const Vec3f& q = vertices[i];
            if (q.x == p.x && q.y == p.y && q.z == p.z) return i + 1;
        }
        vertices.push_back(p);
        char buf[96];
        std::snprintf(buf, sizeof buf, "v %.3f %.3f %.3f\n", p.x, p.y, p.z);
        out += buf;
        return vertices.size();
    };

    for (const auto& t : mesh.triangles) {
        if (t.sprite && !include_sprites) continue;
        if (t.decal && !include_sprites) continue;
        const std::size_t ia = vertex(t.a);
        const std::size_t ib = vertex(t.b);
        const std::size_t ic = vertex(t.c);
        char buf[64];
        std::snprintf(buf, sizeof buf, "f %zu %zu %zu\n", ia, ib, ic);
        faces += buf;
    }

    out += faces;
    return out;
}

bool render_mesh_png(const Mesh& mesh, const std::string& path, int tile) {
    Vec3f lo, hi;
    if (!mesh.bounds(lo, hi)) return false;

    const View views[3] = {{2, 0, 1, false}, {2, 1, 0, false}, {0, 1, 2, false}};
    const int width = tile * 3;
    const int height = tile;

    SDL_Surface* surface = SDL_CreateRGBSurfaceWithFormat(
        0, width, height, 32, SDL_PIXELFORMAT_RGBA32);
    if (!surface) return false;
    SDL_FillRect(surface, nullptr, SDL_MapRGBA(surface->format, 12, 13, 16, 255));

    std::vector<float> zbuf(static_cast<std::size_t>(width) * height,
                            -std::numeric_limits<float>::infinity());
    auto* pixels = static_cast<std::uint32_t*>(surface->pixels);
    const int pitch = surface->pitch / 4;

    const float light[3] = {0.35f, 0.50f, 0.79f};

    for (int v = 0; v < 3; ++v) {
        const View& view = views[v];
        float mn[2] = {std::numeric_limits<float>::max(), std::numeric_limits<float>::max()};
        float mx[2] = {std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest()};
        auto coord = [&](const Vec3f& p, int axis) {
            return axis == 0 ? p.x : axis == 1 ? p.y : p.z;
        };
        for (const auto& t : mesh.triangles) {
            if (t.sprite) continue;
            if (t.decal) continue;
            for (const Vec3f* p : {&t.a, &t.b, &t.c}) {
                for (int k = 0; k < 2; ++k) {
                    const int axis = k == 0 ? view.a : view.b;
                    const float c = coord(*p, axis);
                    mn[k] = std::min(mn[k], c);
                    mx[k] = std::max(mx[k], c);
                }
            }
        }
        if (mn[0] > mx[0]) continue;

        const float span_x = std::max(mx[0] - mn[0], 1e-3f);
        const float span_y = std::max(mx[1] - mn[1], 1e-3f);
        const float scale = std::min((tile - 16) / span_x, (tile - 16) / span_y);
        const float cx = (mn[0] + mx[0]) * 0.5f;
        const float cy = (mn[1] + mx[1]) * 0.5f;
        const int ox = v * tile + tile / 2;
        const int oy = tile / 2;

        auto project = [&](const Vec3f& p) {
            P2 r;
            r.x = (coord(p, view.a) - cx) * scale;
            r.y = (coord(p, view.b) - cy) * scale;
            if (view.flip_x) r.x = -r.x;
            r.y = -r.y;
            return P2{ox + r.x, oy + r.y};
        };

        for (const auto& t : mesh.triangles) {
            if (t.sprite) continue;
            if (t.decal) continue;
            const P2 p0 = project(t.a), p1 = project(t.b), p2 = project(t.c);
            const float d0 = coord(t.a, view.depth);
            const float d1 = coord(t.b, view.depth);
            const float d2 = coord(t.c, view.depth);

            // Flat shading from the geometric normal.
            const float e1[3] = {t.b.x - t.a.x, t.b.y - t.a.y, t.b.z - t.a.z};
            const float e2[3] = {t.c.x - t.a.x, t.c.y - t.a.y, t.c.z - t.a.z};
            float nx = e1[1] * e2[2] - e1[2] * e2[1];
            float ny = e1[2] * e2[0] - e1[0] * e2[2];
            float nz = e1[0] * e2[1] - e1[1] * e2[0];
            const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
            if (len < 1e-9f) continue;
            nx /= len; ny /= len; nz /= len;
            const float shade = 0.35f + 0.65f * std::fabs(nx * light[0] + ny * light[1] + nz * light[2]);
            const auto r = static_cast<std::uint8_t>(std::clamp(0.62f * shade * 255.0f, 0.0f, 255.0f));
            const auto g = static_cast<std::uint8_t>(std::clamp(0.68f * shade * 255.0f, 0.0f, 255.0f));
            const auto b = static_cast<std::uint8_t>(std::clamp(0.78f * shade * 255.0f, 0.0f, 255.0f));
            const std::uint32_t colour = SDL_MapRGBA(surface->format, r, g, b, 255);

            const float area = (p1.x - p0.x) * (p2.y - p0.y) - (p2.x - p0.x) * (p1.y - p0.y);
            if (std::fabs(area) < 1e-6f) continue;

            const int x0 = std::max(static_cast<int>(std::floor(std::min({p0.x, p1.x, p2.x}))), v * tile);
            const int x1 = std::min(static_cast<int>(std::ceil(std::max({p0.x, p1.x, p2.x}))), v * tile + tile - 1);
            const int y0 = std::max(static_cast<int>(std::floor(std::min({p0.y, p1.y, p2.y}))), 0);
            const int y1 = std::min(static_cast<int>(std::ceil(std::max({p0.y, p1.y, p2.y}))), height - 1);

            for (int y = y0; y <= y1; ++y) {
                for (int x = x0; x <= x1; ++x) {
                    const float px = static_cast<float>(x) + 0.5f;
                    const float py = static_cast<float>(y) + 0.5f;
                    const float w0 = ((p1.x - px) * (p2.y - py) - (p2.x - px) * (p1.y - py)) / area;
                    const float w1 = ((p2.x - px) * (p0.y - py) - (p0.x - px) * (p2.y - py)) / area;
                    const float w2 = 1.0f - w0 - w1;
                    if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f) continue;
                    const float z = w0 * d0 + w1 * d1 + w2 * d2;
                    float& slot = zbuf[static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x)];
                    if (z > slot) {
                        slot = z;
                        pixels[y * pitch + x] = colour;
                    }
                }
            }
        }
    }

    SDL_Surface* converted =
        SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(surface);
    if (!converted) return false;
    const bool ok = IMG_SavePNG(converted, path.c_str()) == 0;
    SDL_FreeSurface(converted);
    return ok;
}

}  // namespace ef
