// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

#include "model.h"

#include <SDL2/SDL.h>

#include <array>
#include <optional>
#include <string>

namespace ef {

struct RenderOptions {
    int width = 1280;
    int height = 900;
    int selected = 0;
    bool labels = true;
    bool focus = false;

    //! Coverage overlays, mirroring the planner's `mpSamCoverage` and
    //! `mpEWRCoverage` toggles.
    bool sam_coverage = false;
    bool ewr_coverage = false;
    //! Multiplier on the assumed per-class radius (the files carry no range).
    float coverage_scale = 1.0f;

    //! Explicit world rectangle (x0, y0, x1, y1). Used when supplied, because
    //! the shipped data mixes coordinate spaces and an inferred extent can be
    //! dominated by outliers.
    std::optional<std::array<int, 4>> bounds;
};

SDL_Surface* render_map(const Dataset& ds, const RenderOptions& opt);
bool save_png(SDL_Surface* surface, const std::string& path);
void print_mission(const Dataset& ds, int index);
void print_stats(const Dataset& ds);

}  // namespace ef
