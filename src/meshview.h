// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

#include "mesh.h"

#include <string>

namespace ef {

// Wavefront OBJ. Sprites (view-facing billboards) are skipped by default.
std::string mesh_to_obj(const Mesh& mesh, const std::string& name,
                        bool include_sprites = false);

// Orthographic side/top/front contact sheet, saved as PNG. Needs SDL2_image.
bool render_mesh_png(const Mesh& mesh, const std::string& path, int tile = 360);

}  // namespace ef
