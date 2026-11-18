// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

// Interactive 3D view of a DID `.3` mesh: orbit camera, perspective
// projection, z-buffer and flat Lambert shading. Renders into an SDL_Surface
// so it can be blitted into the normal viewer window.

#include "mesh.h"
#include "texture.h"

#include <SDL2/SDL.h>

#include <string>

namespace ef {

struct OrbitCamera {
    float yaw = 0.75f;     // radians
    float pitch = 0.35f;   // radians
    float zoom = 1.0f;     // 1 = fit
    bool wireframe = false;
    bool decals = false;
    //! The meshes are open shells with mixed winding, so culling hides as
    //! much as it fixes; off by default, toggle with C in the viewer.
    bool cull = false;
    bool textured = true;

    void rotate(float dx, float dy);
    void dolly(float factor);
};

// Draws `mesh` into `surface`'s rectangle (x, y, w, h).
// Per-triangle texture lookup: ``slots[mesh.triangle.texture_slot]``.
using TextureSlots = std::vector<const Texture*>;

void draw_model(SDL_Surface* surface, const Mesh& mesh, const OrbitCamera& camera,
                int x, int y, int w, int h, const TextureSlots* textures = nullptr,
                const Palette* palette = nullptr);

//! Renders one view to a PNG, for scripted comparison against the game.
bool render_model_png(const Mesh& mesh, const OrbitCamera& camera,
                      const TextureSlots* textures, const Palette* palette,
                      const std::string& path, int width = 640, int height = 640);

}  // namespace ef
