#pragma once

#include <cstdint>
#include <vector>

namespace ef {

struct Vec3f {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

// One rasterised triangle in object space.
struct MeshTriangle {
    Vec3f a, b, c;
    std::uint8_t colors[3] = {0, 0, 0};
    bool textured = false;
    //! Billboards face the viewer, so their vertices are not object space.
    bool sprite = false;
};

struct Mesh {
    std::vector<MeshTriangle> triangles;

    bool bounds(Vec3f& lo, Vec3f& hi) const;
    std::size_t triangle_count(bool include_sprites = false) const;
};

}  // namespace ef
