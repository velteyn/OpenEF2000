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
#include "ilbm.h"
#include "texture.h"

#include <SDL2/SDL.h>

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
};

//! Draws the active screen of `system` into `surface` (640x400 logical size).
void draw_gui(SDL_Surface* surface, GuiSystem& system, const Palette& palette,
              const GuiRenderOptions& options, const IlbmImage* backdrop = nullptr);

//! Loads the screen's `picture` backdrop; nullptr when absent.
std::optional<IlbmImage> load_backdrop(const DidArchive& archive, const std::string& name);

//! Convenience: render a screen straight to PNG.
bool render_gui_png(const DidArchive& archive, const std::string& screen,
                    const std::string& path, const GuiRenderOptions& options = {});

}  // namespace ef
