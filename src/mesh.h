// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

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
    //! Per-vertex texture coordinates in texels (0..255, 0..191).
    float uv[3][2] = {{0.0f, 0.0f}, {0.0f, 0.0f}, {0.0f, 0.0f}};
    bool textured = false;
    bool has_uv = false;
    //! 0 = the shape's hard-coded texture, k = global slot k (see init3d.txt).
    int texture_slot = 0;
    //! Billboards face the viewer, so their vertices are not object space.
    bool sprite = false;
    //! Masked/blended overlay (markings, canopy, afterburner). Meaningless
    //! without texture sampling, so viewers hide it by default.
    bool decal = false;
};

struct Mesh {
    std::vector<MeshTriangle> triangles;

    bool bounds(Vec3f& lo, Vec3f& hi) const;
    std::size_t triangle_count(bool include_sprites = false) const;
};

}  // namespace ef
