// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

#include "model.h"

#include <array>
#include <string>
#include <vector>

namespace ef {

//! The summary screen's three columns. The scenario's neutrals are the Nordic
//! and Baltic actors that are not in NATO or the Russian side.
enum class ForceSide { nato = 0, neutral = 1, enemy = 2 };
constexpr int kForceSideCount = 3;

struct SideForce {
    int aircraft = 0;    //!< sum of `FLIGHT count`
    int ground = 0;      //!< sum of non-naval `GROUP count`
    int ships = 0;       //!< sum of naval `GROUP count`
    int structures = 0;  //!< target areas
    int total() const { return aircraft + ground + ships + structures; }
};

//! One row of a summary bar window: a category and its count per side.
struct ForceCategory {
    std::string name;
    std::array<int, kForceSideCount> counts{};
};

struct ForceStats {
    std::array<SideForce, kForceSideCount> sides{};
    //! Rows in the order the shipped bar windows use.
    std::vector<ForceCategory> aircraft;    // Fighters, Bombers, ...
    std::vector<ForceCategory> ground;      // AntiAircraft, BattleShips, ...
    std::vector<ForceCategory> structures;  // EWR Sites, Choke Points, ...

    int total() const;
    int side_total(ForceSide side) const { return sides[static_cast<int>(side)].total(); }
};

ForceSide force_side(const std::string& side);
const char* force_side_name(ForceSide side);

//! Value of a named row in one of the summary categories (0 when absent).
int category_value(const std::vector<ForceCategory>& rows, const std::string& name,
                   int side);

//! Counts the current forces described by the mission files. The static data
//! has no loss history, so this is current strength, not casualties.
ForceStats compute_force_stats(const Dataset& ds);

//! Index of the takeoff base a PLANE mission's `values[0]` refers to, or -1.
int mission_base_index(const Mission& mission);
//! Index of a base by (unquoted) name, or -1.
int find_base_index(const Dataset& ds, const std::string& name);
//! Sum of EFA flights assigned to `base_index`. `only_free` counts the ones
//! still on standby rather than committed to a strike.
int base_efa_count(const Dataset& ds, int base_index, bool only_free);

}  // namespace ef
