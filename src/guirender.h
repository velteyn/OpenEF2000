// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

// Software renderer for the reconstructed GUI.
//
// The panel style is an approximation: the game draws frames procedurally from
// palette indices, but the exact bevel rules live in the original widget code.
// Everything structural -- window stack, geometry, text, hollow index 0,
// selected-state colours -- is faithful to the `.gui` files.

#include "did.h"
#include "gui.h"
#include "model.h"
#include "ilbm.h"
#include "texture.h"

#include <SDL2/SDL.h>

#include <filesystem>
#include <string>

namespace ef {

struct GuiRenderOptions {
    std::string language = "en";
    bool backdrop = true;     //!< draw `picture` behind the windows
    bool cursor = false;      //!< draw a placeholder software cursor
    int cursor_x = -1;
    int cursor_y = -1;
    std::string hover_window;
    std::string hover_icon;
    //! Opens this window before drawing, so dialogs can be inspected headlessly.
    std::string open_window;
    //! Loose data directory, for files the engine reads directly (HISCORES.DAT).
    std::filesystem::path data_dir;
    //! When set, the strategic map is drawn inside the planner/campaign map
    //! windows, using the GUI state for coverage, zoom and selection.
    const Dataset* dataset = nullptr;
    //! Headless overrides for the map state (the viewer changes it by input).
    int camp_zoom = -1;
    bool camp_sam = false;
    bool camp_ewr = false;
    int camp_sides = -1;   //!< side bitmask; -1 keeps the screen's default
    int camp_layers = -1;  //!< layer bitmask; -1 keeps the screen's default
    int camp_pan_x = 0;
    int camp_pan_y = 0;
    int camp_mode = -1;      //!< map style 0..3; -1 keeps the screen default
    int camp_flags = -1;     //!< 0 hides unit markers, 1 shows them
    //! Rubber-band rectangle while dragging in zoom mode (screen coords).
    bool band = false;
    int band_x0 = 0, band_y0 = 0, band_x1 = 0, band_y1 = 0;
};

//! Draws the active screen of `system` into `surface` (640x400 logical size).
void draw_gui(SDL_Surface* surface, GuiSystem& system, const Palette& palette,
              const GuiRenderOptions& options, const IlbmImage* backdrop = nullptr);

//! True for the windows that carry the strategic map.
bool gui_is_map_window(const std::string& name);

//! Maps a screen point inside a map window to world coordinates. `radius` is
//! the pick radius in world units (ten screen pixels), used when resolving a
//! click to a target, base or waypoint.
bool gui_map_world_at(const GuiSystem& system, const Dataset& ds,
                      const GuiWindow& win, int sx, int sy, int* world_x,
                      int* world_y, int* radius);

//! Drag-to-pan. `dx`/`dy` are screen deltas since the last motion event.
bool gui_map_pan(GuiSystem& system, const Dataset& ds, const GuiWindow& win,
                 int dx, int dy);

//! Wheel zoom that keeps the world point under the cursor fixed.
bool gui_map_zoom_at(GuiSystem& system, const Dataset& ds, const GuiWindow& win,
                     int sx, int sy, int steps);

//! Zoom to a screen rectangle (the original's Edit/Zoom mode drag).
bool gui_map_zoom_rect(GuiSystem& system, const Dataset& ds, const GuiWindow& win,
                       int x0, int y0, int x1, int y1);

//! Blits an indexed image across the whole surface, using its own palette.
void draw_image(SDL_Surface* surface, const IlbmImage& image);

//! Loads the screen's `picture` backdrop; nullptr when absent.
std::optional<IlbmImage> load_backdrop(const DidArchive& archive, const std::string& name);

//! Convenience: render a screen straight to PNG.
bool render_gui_png(const DidArchive& archive, const std::string& screen,
                    const std::string& path, const GuiRenderOptions& options = {});

}  // namespace ef
