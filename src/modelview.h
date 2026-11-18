#pragma once

// Interactive 3D view of a DID `.3` mesh: orbit camera, perspective
// projection, z-buffer and flat Lambert shading. Renders into an SDL_Surface
// so it can be blitted into the normal viewer window.

#include "mesh.h"

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

    void rotate(float dx, float dy);
    void dolly(float factor);
};

// Draws `mesh` into `surface`'s rectangle (x, y, w, h).
void draw_model(SDL_Surface* surface, const Mesh& mesh, const OrbitCamera& camera,
                int x, int y, int w, int h);

}  // namespace ef
