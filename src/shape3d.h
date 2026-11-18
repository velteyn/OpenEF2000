// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

// Parser and bytecode interpreter for Digital Image Design `.3` shape files.
//
// A `.3` file is a big-endian word stream: a header with LOD distances, a
// class id and an optional hard-coded texture name, then one bytecode program
// per level of detail. The programs drive a small virtual machine that writes
// object-space vertices and draws flat-, Gouraud- and texture-shaded
// primitives. The format is documented by the 3View SDK; this is an
// independent implementation.
//
// Only the geometry-producing instructions matter here; texture, fog and
// rasteriser state are parsed but ignored.

#include "mesh.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace ef {

struct ShapeLod {
    int min_distance = 0;
    int max_distance = 0;
    int xyz_limit = 0;
    std::size_t begin = 0;  // first opcode word
    std::size_t end = 0;    // one past the last word
};

using ShapeParameters = std::array<int, 19>;

class Shape {
public:
    // Throws std::runtime_error on a malformed file.
    static Shape parse(std::span<const std::uint8_t> data);

    const std::vector<ShapeLod>& lods() const { return lods_; }
    std::uint16_t class_id() const { return class_id_; }
    int rendering_priority() const { return rendering_priority_; }
    const std::optional<std::string>& texture_name() const { return texture_name_; }

    // Linear sweep over a LOD's bytecode; returns the index of the 0xFFFF
    // terminator, or nullopt if the program is malformed.
    std::optional<std::size_t> verify(std::size_t lod) const;

    const std::vector<std::uint16_t>& words() const { return words_; }

private:
    std::vector<std::uint16_t> words_;
    std::vector<ShapeLod> lods_;
    std::uint16_t class_id_ = 0;
    int rendering_priority_ = 0;
    std::optional<std::string> texture_name_;
};

// Interprets one LOD and collects every triangle it draws.
Mesh execute_shape(const Shape& shape, std::size_t lod,
                   const ShapeParameters& parameters = {},
                   int viewer_distance = 0, int time_of_day = 3);

}  // namespace ef
