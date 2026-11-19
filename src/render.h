// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

#include "model.h"

#include <SDL2/SDL.h>

#include <array>
#include <optional>
#include <string>

namespace ef {

//! Strategic-map side filter bits. The three actors the data names are NATO,
//! the Russian side, and the neutral Nordic/Baltic states (Sweden, Finland,
//! the Baltic republics).
enum MapSide : unsigned {
    MapSideNato = 1u << 0,
    MapSideRussian = 1u << 1,
    MapSideNeutral = 1u << 2,
    MapSideAll = MapSideNato | MapSideRussian | MapSideNeutral,
};

//! Strategic-map layer filter bits, matching the six checkboxes in the
//! shipped `mapmodeswin` dialog.
enum MapLayer : unsigned {
    MapLayerAirDefence = 1u << 0,
    MapLayerAirSupport = 1u << 1,
    MapLayerNavy = 1u << 2,
    MapLayerComms = 1u << 3,
    MapLayerSupply = 1u << 4,
    MapLayerPolitical = 1u << 5,
    MapLayerAll = (1u << 6) - 1u,
};

//! Side bits for a data side string. Unknown sides are not filtered out.
unsigned map_side_bits(const std::string& side);
//! Layer bits for a unit type and role. Aircraft pass their role as
//! `AIR_SUPPORT` when their kind is already known.
unsigned map_layer_bits(const std::string& type, const std::string& role = "");

//! Screen/world mapping used by `render_map`. Returned so the GUI can turn a
//! click inside the map window into a world position.
struct MapProjection {
    int world_min_x = 0, world_min_y = 0, world_max_x = 1, world_max_y = 1;
    int left = 0, top = 0, right = 1, bottom = 1;

    int world_x(int screen_x) const;
    int world_y(int screen_y) const;
    int screen_x(int world_x) const;
    int screen_y(int world_y) const;
    bool contains(int screen_x, int screen_y) const;
};

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

    //! Draw only the map, with no side panel, footer or margins. Used when
    //! embedding the map inside a GUI window.
    bool bare = false;
    //! Zoom factor applied to the computed extent (1.0 = the whole theatre).
    float zoom = 1.0f;

    //! Filters shared with the campaign screen's Map Options dialog.
    unsigned side_mask = MapSideAll;
    unsigned layer_mask = MapLayerAll;
    //! Map style: 0/3 relief, 1/2 political. Political modes use a light
    //! background and place labels, matching the original's `m` cycle.
    int map_mode = 0;
    //! Flags toggle (`f`): unit, flight and airbase markers. Target areas and
    //! coverage rings are independent overlays.
    bool flags = true;
    //! Pan offset in world units (drag-to-pan and zoom-to-cursor).
    int pan_x = 0, pan_y = 0;

    //! Explicit world rectangle (x0, y0, x1, y1). Used when supplied, because
    //! the shipped data mixes coordinate spaces and an inferred extent can be
    //! dominated by outliers.
    std::optional<std::array<int, 4>> bounds;
};

SDL_Surface* render_map(const Dataset& ds, const RenderOptions& opt);
//! The projection `render_map` uses for these options. Used by the GUI to map
//! a map-window click back to a world position.
MapProjection map_projection(const Dataset& ds, const RenderOptions& opt);
bool save_png(SDL_Surface* surface, const std::string& path);
void print_mission(const Dataset& ds, int index);
void print_stats(const Dataset& ds);

}  // namespace ef
