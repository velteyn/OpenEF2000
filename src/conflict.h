// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

// Reader/writer for the campaign scenario text (`CONFLICT.CG` and
// `CAMPAIGN.CG`). The generated mission file is handled by parser.cpp; this
// module keeps the scenario layer separate because the generator needs the
// mobiles, pilots and allegiances, not just the mission records.

#include "model.h"

#include <array>
#include <string>
#include <vector>

namespace ef {

struct ScenarioArea {
    std::string name;
    std::string side;
    int value = 0;
    int airfield1 = 0;
    int airfield2 = 0;
    bool cv = false;                    //!< CV record followed by 30 numbers
    std::vector<int> cv_values;
};

struct ScenarioPilot {
    std::string name;
    std::array<int, 11> fields{};
    std::optional<Vec3> pos;
    std::array<int, 4> tail{};
};

struct ScenarioMobile {
    std::string kind;    //!< PL (air), GB (ground), SH (ship)
    std::string type;
    std::string status;  //!< OK / DEAD / RESERVED
    std::string side;
    std::string name;
    std::array<int, 18> fields{};
    std::optional<Vec3> pos;  //!< position from the following PIL 3DP
    bool has_pilot = false;
    ScenarioPilot pilot;
};

struct Scenario {
    int difficulty = 6;
    int performance_a = 1;
    int performance_b = 50;
    std::array<int, 6> world_status{};
    std::vector<ScenarioArea> areas;
    std::vector<ScenarioMobile> mobiles;
    std::vector<ScenarioPilot> pilots;
    std::vector<std::vector<int>> allegiances;

    const ScenarioArea* find_area(const std::string& name) const;
};

//! Loads `CONFLICT.CG` or `CAMPAIGN.CG`.
bool load_conflict(const std::string& path, Scenario* out, std::string* error);
//! Writes the same schema back (used by the generator and round-trip tests).
bool save_conflict(const std::string& path, const Scenario& conflict,
                   std::string* error);

//! Grouping helpers used by the generator: (side, position) -> mobiles.
struct MobileGroup {
    std::string side;
    Vec3 pos;
    std::vector<const ScenarioMobile*> mobiles;
    int count() const { return static_cast<int>(mobiles.size()); }
};

std::vector<MobileGroup> group_mobiles(const Scenario& conflict, const char* kind);

}  // namespace ef
