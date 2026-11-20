// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

// Campaign generator.
//
// Inputs are the shipped scenario (`CONFLICT.CG`), the previous campaign state
// (`CAMPAIGN.CG`, which carries ownership and damage forward) and the fixed
// target/base table stored in `DID1ST.DAT`. Outputs are a regenerated
// `CAMPAIGN.CG` and a `MISSION.CFG`.
//
// The generator follows the phases recovered from the executable: repair,
// attrition and link passes over the previous state, base construction, air
// defence tasking, air/ship tasking, pilot assignment, then the two writers.

#include "campaign.h"
#include "conflict.h"
#include "mission.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace ef {

//! One record of the fixed 543-entry target/base table.
struct TargetSite {
    std::string name;
    std::string model;
    int x = 0;
    int y = 0;
    int altitude = 0;
    int type = 0;  //!< `;NN` class code (40..47 airfield, 90..95 port, ...)
};

//! Canonical side name for the game's side code (0 = NATO ... 8 = UK).
const char* side_name(int code);

//! Reads the table from the shipped archive. It is stored as a plain-text
//! block inside `DID1ST.DAT` (falling back to `DID.DAT`).
bool load_target_table(const std::string& data_dir,
                       std::vector<TargetSite>* out, std::string* error);

struct GenSettings {
    int difficulty = 6;
    int influence = 1;
    std::uint32_t seed = 0x31415926u;
    //! Draws consumed by the shell before generation starts. It varies with
    //! the session (three in most captured runs, zero in one), so it is a
    //! replay knob rather than a generator rule.
    int draw_offset = 0;
    std::array<int, 4> world_status{0, 0, 0, 0};
    std::array<int, 2> clock{0, 0};  //!< campaign day, 8-hour slot
};

//! A ground installation placed around a target area.
struct DefenceSite {
    int weapon_class = -1;
    int health = 100;
    int readiness = 100;
    int off_x = 0;  //!< 1/64 map units, printed in the mission 3DP record
    int off_y = 0;
    int a = 0;
    int b = 0;
    int c = 0;
};

struct GenArea {
    std::string name;
    std::string side;
    int x = 0;
    int y = 0;
    int type = 0;
    int value = 1000;
    int airfield1 = 0;
    int airfield2 = 0;
    int map_cell = 0;
    std::vector<DefenceSite> sites;
};

struct GenBase {
    std::string name;
    std::string side;
    std::string type;  //!< AIRFIELD or PORT
    int x = 0;
    int y = 0;
    int index = 0;
    int value = 0;
    bool port = false;
};

struct GenMobile {
    std::size_t source = 0;      //!< index in the scenario mobile list
    int class_id = 0;            //!< ground-type index for GB, role code otherwise
    int side_code = 0;
    int state = 0;
    bool used = false;
    int x = 0;
    int y = 0;
    int area = -1;
    int mission = -1;
    int flight = 0;
    int base = -1;
    int slot = 0;
};

struct GeneratedCampaign {
    //! Mobile records carried into the new campaign. They come from the
    //! previous campaign state when there is one, otherwise from the scenario.
    std::vector<ScenarioMobile> state_mobiles;
    std::vector<GenArea> areas;
    std::vector<GenBase> bases;
    std::vector<GenMobile> mobiles;
    MissionDocument missions;
    MissionCounts counts;
    int aircraft = 0;
    std::array<int, 6> world_status{};  //!< written to CAMPAIGN.CG
};

//! Runs the generator. `previous` is the state read from `CAMPAIGN.CG`; it may
//! be null for a first campaign, in which case ownership comes from `scenario`.
bool generate_campaign(const Scenario& scenario, const Scenario* previous,
                       const std::vector<TargetSite>& table,
                       const GenSettings& settings, GeneratedCampaign* out,
                       std::string* error);

//! Writes the regenerated `CAMPAIGN.CG` (areas, mobiles, pilots, allegiances).
bool write_campaign_file(const std::string& path, const Scenario& scenario,
                         const GeneratedCampaign& campaign, std::string* error);

}  // namespace ef
