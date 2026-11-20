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
#include "ssd.h"

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
    std::string data_dir;  //!< game data, used to read `.ssd` label tables
    int draw_offset = 0;
    //! Eight-hour campaign steps to advance before generating (0 or more).
    int advance_steps = 0;
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
    std::string model;   //!< `.sss` model name from the table
    int x = 0;
    int y = 0;
    int heading = 0;     //!< model heading (0/90/180/270)
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

//! War-state evaluation. The loss ratio is the fraction of a side's mobiles
//! marked dead or crippled; the binary compares it against 0.75 (collapse) and
//! 0.95 (destruction). The strategic-target counts that gate the other branch
//! are not modelled yet.
struct CampaignOutcome {
    double loss_ratio[9]{};  //!< indexed by side code
    int units[9]{};
    int losses[9]{};
    bool collapse = false;   //!< any side past the 0.75 threshold
    bool destroyed = false;  //!< any side past the 0.95 threshold
    int worst_side = -1;
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
    CampaignOutcome outcome;
};

CampaignOutcome evaluate_campaign(const GeneratedCampaign& campaign);




//! Runs the generator. `previous` is the state read from `CAMPAIGN.CG`; it may
//! be null for a first campaign, in which case ownership comes from `scenario`.
bool generate_campaign(const Scenario& scenario, const Scenario* previous,
                       const std::vector<TargetSite>& table,
                       const GenSettings& settings, GeneratedCampaign* out,
                       std::string* error);

//! Advances the campaign clock by one 8-hour step: hour 0 -> 8 -> 16 -> next
//! day, minute cleared. `clock` is `WORLD_STATUS` in file order
//! (`day`, `hour`, `minute`, ...).
void advance_campaign_clock(std::array<int, 6>* clock);

//! Writes the regenerated `CAMPAIGN.CG` (areas, mobiles, pilots, allegiances).
bool write_campaign_file(const std::string& path, const Scenario& scenario,
                         const GeneratedCampaign& campaign, std::string* error);

}  // namespace ef
