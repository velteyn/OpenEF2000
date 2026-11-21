// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "campaigngen.h"

#include "did.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <tuple>

namespace ef {
namespace {

// ---------------------------------------------------------------------------
// Fixed tables
// ---------------------------------------------------------------------------

//! Ground installation classes, in the order the game's type table uses.
//! Target-element names, indexed by the kind stored in the `.ssd` label table.
const char* const kElementNames[] = {
    "EWR_SITE", "GLOBES", "DISHES", "DIRECTIONAL_RADAR", "CONTROL_BUILDING",
    "C4_TYPE", "HQ_BUILDING", "COMMAND_VEHICLE", "COMMAND_BUNKER",
    "COMMS_BUILDING", "GLOBE_ARRAY", "SUPPLY DUMP", "ACCESS_ROAD",
    "HARDENED_WAREHOUSE", "CAMOUFLAGED_WAREHOUSE", "SMALL_WAREHOUSE",
    "LARGE_WAREHOUSE", "MUNITIONS_BUILDING", "STACKED_CONTAINERS",
    "CHOKE_POINT", "ROAD_RAIL_BRIDGE", "ROAD_BRIDGE", "RAIL_BRIDGE",
    "ROAD_TUNNEL", "RAIL_TUNNEL", "BRIDGING_VEHICLES", "ROAD_JUNCTION",
    "RAIL_CROSSING", "SHELTERED_TERMINUS", "POLITICAL_INDUSTRIAL",
    "TILE_ROOF_BUILDING", "MULTI_STOREY_OFFICES", "SMALL_FACTORY",
    "LARGE_FACTORY", "POWER_STATION_TOWERS", "FACTORY_CHIMNEYS",
    "TOWN_BUILDING", "CITY_BUILDING", "POL_TYPE", "OIL_REFINERY",
    "OIL_CONTAINERS", "PUMPING_STATION", "OILRIGS", "POWER_STATION",
    "CYLINDRICAL_VESSELS", "CONNECTING_PIPE", "NUCLEAR_POWER_BUILDING",
    "COOLING_TOWER", "MINERAL_BURN_BUILDING", "AIRFIELD", "HAS2", "HANGAR8",
    "HANGAR4", "CONTROL_TOWER", "APRON", "RUNWAY", "DISPERSED", "AIR_DEFENSE",
    "SA_6", "SA_11", "PATRIOT", "RAPIER", "MARCONI_S723", "MARCONI_S373",
    "M_163_VULCAN", "ZSU_23_4_SHILKA", "T80", "BRIDGING _VECICLE",
    "ARTILLERY_UNIT", "GROUND_FORCE", "TROOPS", "ECM_FORCE", "MOBILE BRIDGE",
    "PORT", "FORTIFIED_MARINER", "QUAY", "POL_PLATFORM", "DOCKSIDE_CRANE",
    "DRY_DOCK", "DAM", "DAM_SLUICE_CONTROL", "DAM_GENERATION_PLANT", "TENT",
    "POLITICAL_BUILDING", "HOSPITAL", "CHURCH", "OIL_AND_SUPPLY_DUMP",
    "TRIPPLE_A", "SAM_SITE", "SUPPLY_UNIT", "TRUCK", "BMP_1", "CHALENGER",
    "D_30", "M198", "MLRS", "M110", "M1973", "RM70", "ROLAND", "WARRIOR",
    "AWACS_TYPE", "JSTARS_TYPE", "REFUELLER_TYPE", "AA_TYPE", "NAVY_GROUP"};

const char* element_name(int kind) {
    static constexpr int kCount =
        static_cast<int>(sizeof(kElementNames) / sizeof(kElementNames[0]));
    return kind >= 0 && kind < kCount ? kElementNames[kind] : "TARGET";
}

//! Navigation waypoints from NAVIG.NAV. The original's strike corridors are
//! routes through this network, so generated fixes are snapped onto it.
std::set<std::pair<int, int>> load_nav_points(const std::string& data_dir) {
    std::set<std::pair<int, int>> out;
    if (data_dir.empty()) return out;
    std::ifstream in(std::filesystem::path(data_dir) / "NAVIG.NAV",
                     std::ios::binary);
    if (!in) return out;
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("WAYPOINT", 0) != 0) continue;
        std::istringstream is(line);
        std::string kind;
        int index = 0, x = 0, y = 0;
        if (!(is >> kind >> index >> x >> y)) continue;
        out.emplace(x, y);
    }
    return out;
}

//! Nearest navigation waypoint within ten map units, skipping points already
//! used by this leg.
bool snap_to_nav(const std::set<std::pair<int, int>>& nav,
                 const std::set<std::pair<int, int>>& used, int* x, int* y) {
    for (int r = 0; r <= 10; ++r) {
        for (int dx = -r; dx <= r; ++dx) {
            for (int dy = -r; dy <= r; ++dy) {
                if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
                const std::pair<int, int> p{*x + dx, *y + dy};
                if (!nav.count(p) || used.count(p)) continue;
                *x = p.first;
                *y = p.second;
                return true;
            }
        }
    }
    return false;
}

//! Builds one strike package's route: a tanker track behind the base, a
//! meandering outbound corridor, an ingress fix, the target and a separate
//! corridor home. Every flight of the package flies the same geometry; only
//! the strike flight marks the ingress and target waypoints.
std::vector<std::array<int, 4>> build_strike_route(
    const GenBase& base, const GenArea& target, bool tanker_at_base,
    const std::set<std::pair<int, int>>& nav_points, CampaignRng& rng) {
    std::vector<std::array<int, 4>> route;  // x, y, z, action
    std::set<std::pair<int, int>> used;
    used.emplace(base.x, base.y);
    used.emplace(target.x, target.y);
    const auto push_nav = [&](int x, int y, int z, int action) {
        if (!route.empty()) {
            const int px = route.back()[0];
            const int py = route.back()[1];
            snap_to_nav(nav_points, used, &x, &y);
            if (x == px && y == py) return;
        }
        used.emplace(x, y);
        route.push_back({x, y, z, action});
    };
    const double dx = target.x - base.x;
    const double dy = target.y - base.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double ux = distance > 0.0 ? dx / distance : 0.0;
    const double uy = distance > 0.0 ? dy / distance : 0.0;

    route.push_back({base.x, base.y, 0, 0});  // TAKE_OFF
    int origin_x = base.x;
    int origin_y = base.y;
    if (tanker_at_base && distance > 100.0) {
        // The tanker track sits about twenty-six cells behind the base.
        route.push_back({base.x - 26, base.y - 1, 250, 1});  // REFUEL
        origin_x = base.x - 26;
        origin_y = base.y - 1;
    }

    // Captured packages carry anywhere from zero to ten intermediate fixes
    // on each leg, so the corridor length is drawn, not derived from range.
    const int out_fixes = rng.range(9);
    const int back_fixes = rng.range(10);
    // Captured routes fly low over the target more often than not, but both
    // altitude bands are common.
    const int altitude = rng.range(10) < 6 ? 120 : 10;

    std::vector<double> fractions;
    fractions.reserve(static_cast<std::size_t>(std::max(out_fixes, back_fixes)));
    for (int i = 0; i < out_fixes; ++i)
        fractions.push_back(rng.between(6, 86) / 100.0);
    std::sort(fractions.begin(), fractions.end());
    for (double t : fractions) {
        const int jitter = rng.between(-18, 18);
        push_nav(static_cast<int>(std::lround(origin_x + dx * t - uy * jitter)),
                 static_cast<int>(std::lround(origin_y + dy * t + ux * jitter)),
                 altitude, 2);  // NO_ACTION
    }

    push_nav(target.x - static_cast<int>(std::lround(ux * 9.0)),
             target.y - static_cast<int>(std::lround(uy * 9.0)), 5,
             3);  // INGRESS
    route.push_back({target.x, target.y, 0, 4});  // STRIKE_TARGET
    used.emplace(target.x, target.y);

    fractions.clear();
    for (int i = 0; i < back_fixes; ++i)
        fractions.push_back(rng.between(6, 86) / 100.0);
    std::sort(fractions.begin(), fractions.end(), std::greater<double>());
    for (double t : fractions) {
        const int jitter = rng.between(-24, 24);
        push_nav(static_cast<int>(std::lround(origin_x + dx * t - uy * jitter)),
                 static_cast<int>(std::lround(origin_y + dy * t + ux * jitter)),
                 altitude, 2);  // NO_ACTION
    }
    route.push_back({base.x, base.y, 0, 5});  // LANDING
    return route;
}

const char* const kGroundTypes[] = {
    "BEDFORD_1", "BEDFORD_2", "BMP_1",   "CHALENGER", "D_30",      "JUG_1",
    "JUG_2",     "JUG_3",     "M_163_VULCAN", "M198",  "MLRS",     "MOBRIDGE",
    "M110",      "M1973",     "PATRIOT_1", "PATRIOT_2", "PONTOON", "RAPIER",
    "RM70",      "ROLAND",    "MARCONI_S373", "MARCONI_S723", "SA_6", "SA_11",
    "T80",       "WARRIOR",   "ZSU_23_4_SHILKA"};
constexpr int kGroundTypeCount = 27;

//! Anti-air classes in the order the air-defence loop cycles them.
//! Slot cycle indexed by the loop counter: slot 0 uses entry 1, slot 1 entry
//! 2, and so on, wrapping at four.
constexpr int kWestDefence[] = {17, 8, 19, 14};   // Rapier, Vulcan, Roland, Patriot
constexpr int kEastDefence[] = {26, 22, 23, 23};  // ZSU, SA-6, SA-11, SA-11

int ground_type_index(const std::string& name) {
    for (int i = 0; i < kGroundTypeCount; ++i) {
        if (name == kGroundTypes[i]) return i;
    }
    return -1;
}

const char* const kSideNames[] = {"NATO",   "RUSSIAN", "SWEDISH", "FINNISH",
                                  "BALTIC", "DANISH",  "NORWEGIAN", "US", "UK"};

//! Mobile status codes, in the order the game's status table uses.
int status_code(const std::string& name) {
    static const char* const kStatus[] = {"OK",     "DEAD",    "CRIPPLED",
                                          "DISABLED", "DAMAGED", "NO_RESULT",
                                          "UNKNOWN", "RESERVED", "ABORT",
                                          "COMPLETE"};
    for (int i = 0; i < 10; ++i) {
        if (name == kStatus[i]) return i;
    }
    return 0;
}

int side_code(const std::string& name) {
    for (int i = 0; i < 9; ++i) {
        if (name == kSideNames[i]) return i;
    }
    return -1;
}

bool is_western(int side) { return side == 0 || side == 2 || side == 5 || side >= 6; }

//! Site readiness by weapon class, from the 0xd9bd0 class table.
int site_readiness(int weapon_class) {
    switch (weapon_class) {
        case 8: return 90;
        case 14: return 50;
        case 17: return 100;
        case 19: return 0;
        case 22:
        case 23:
        case 26: return 95;
        default: return 100;
    }
}

//! The nine convoy ports the generator adds from its embedded table.
struct VirtualPort {
    const char* name;
    int side;
    int x;
    int y;
};

const VirtualPort kVirtualPorts[] = {
    {"Gdansk", 4, 225, 0},      {"Sassnitz", 3, 213, 0},
    {"Karlshamn", 2, 195, 0},   {"Helsingor", 0, 118, 0},
    {"Muros", 0, 0, 20},        {"Inverness", 0, 0, 148},
    {"Baltiysk", 1, 225, 0},    {"nayyanmar", 1, 399, 344},
    {"Archangelsk", 1, 399, 229},
};

bool is_airfield_type(int type) { return type >= 40 && type <= 47; }
bool is_port_type(int type) {
    return (type >= 90 && type <= 95) || (type >= 205 && type <= 207);
}

//! Number of defence slots per installation class. Airfields and the larger
//! naval installations carry eight (two passes of the four-class cycle); the
//! big towns carry three; everything else one.
int site_count_for_type(int type) {
    if (is_airfield_type(type)) return 8;
    if (type == 127 || type == 204) return 8;
    if (type == 142 || type == 143) return 1;
    if (type >= 96) return 3;
    return 1;
}

const char* const kStampFormat = "%a %b %e %H:%M:%S %Y";

std::string now_stamp() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    char buf[64]{};
    std::strftime(buf, sizeof(buf), kStampFormat, &tm);
    return buf;
}

std::string Number(int v) { return std::to_string(v); }

//! Upper bound of the small value reduction for a non-base installation. The
//! exact number of sub-targets lives in the `.sss` model; these bounds come
//! from the observed per-class ranges.
int damage_for(int type, const std::string&) {
    // Modal figure per class. The per-area variation comes from the
    // installation's sub-target list (`.sss`), which is not modelled yet.
    if (type >= 96 && type <= 100) return 1;
    if (type >= 101 && type <= 111) return 2;
    if (type == 127) return 4;
    return 0;
}

bool parse_table_row(const std::string& line, TargetSite* out) {
    std::size_t q1 = line.find('"');
    if (q1 == std::string::npos) return false;
    std::size_t q2 = line.find('"', q1 + 1);
    if (q2 == std::string::npos) return false;
    const std::string name = line.substr(q1 + 1, q2 - q1 - 1);

    std::istringstream head(line.substr(0, q1));
    int x = 0, y = 0, alt = 0;
    std::string model;
    if (!(head >> x >> y >> alt >> model)) return false;

    std::size_t semi = line.find(';', q2);
    if (semi == std::string::npos) return false;
    int type = std::atoi(line.c_str() + semi + 1);
    if (type <= 0) return false;

    out->name = name;
    out->model = model;
    out->x = x;
    out->y = y;
    out->altitude = alt;
    out->type = type;
    return true;
}

// ---------------------------------------------------------------------------
// Mission emission
// ---------------------------------------------------------------------------

struct Emitter {
    MissionDocument* doc = nullptr;
    int next_id = 0;

    void blank() { doc->blank(); }

    void add(const std::string& keyword, std::initializer_list<std::string> args) {
        doc->add(keyword, args);
    }

    void addv(const std::string& keyword, std::vector<std::string> args) {
        doc->add(keyword, args);
    }
};

}  // namespace

const char* side_name(int code) {
    if (code < 0 || code > 8) return "NO_ALLEGIANCE";
    return kSideNames[code];
}

bool load_target_table(const std::string& data_dir, std::vector<TargetSite>* out,
                       std::string* error) {
    namespace fs = std::filesystem;
    std::vector<fs::path> candidates{fs::path(data_dir) / "DID1ST.DAT",
                                     fs::path(data_dir) / "DID.DAT"};
    for (const auto& path : candidates) {
        std::ifstream in(path, std::ios::binary);
        if (!in) continue;
        std::ostringstream os;
        os << in.rdbuf();
        const std::string blob = os.str();
        // The table is a plain-text block. Locate it by its first row and read
        // rows until the block ends, keeping the longest run found.
        std::vector<TargetSite> best;
        for (std::size_t pos = blob.find("164\t14"); pos != std::string::npos;
             pos = blob.find("164\t14", pos + 1)) {
            std::vector<TargetSite> rows;
            std::size_t p = pos;
            while (p < blob.size()) {
                std::size_t e = blob.find('\n', p);
                if (e == std::string::npos) e = blob.size();
                std::string line = blob.substr(p, e - p);
                if (!line.empty() && line.back() == '\r') line.pop_back();
                TargetSite row;
                if (!parse_table_row(line, &row)) break;
                rows.push_back(std::move(row));
                p = e + 1;
                if (p >= blob.size()) break;
            }
            if (rows.size() > best.size()) best = std::move(rows);
        }
        if (!best.empty()) {
            *out = std::move(best);
            return true;
        }
    }
    if (error) *error = "target/base table not found in " + data_dir;
    return false;
}

// ---------------------------------------------------------------------------
// Generation
// ---------------------------------------------------------------------------

namespace {

std::map<std::string, const ScenarioArea*> index_areas(const Scenario& s) {
    std::map<std::string, const ScenarioArea*> out;
    for (const auto& a : s.areas) out.emplace(a.name, &a);
    return out;
}

//! Ownership for areas the scenario does not list (e.g. Visby): take the side
//! of the nearest area that does have one.
std::string inherit_side(const std::string& name, int x, int y,
                         const std::vector<GenArea>& areas) {
    long best = -1;
    std::string side;
    for (const auto& a : areas) {
        if (a.name == name || a.side.empty()) continue;
        const long dx = a.x - x, dy = a.y - y;
        const long d = dx * dx + dy * dy;
        if (best < 0 || d < best) {
            best = d;
            side = a.side;
        }
    }
    return side;
}

std::string base_type_for(int type) {
    if (is_airfield_type(type)) return "AIRFIELD";
    return "PORT";
}

}  // namespace

CampaignOutcome evaluate_campaign(const GeneratedCampaign& campaign) {
    CampaignOutcome out;
    for (const auto& gm : campaign.mobiles) {
        if (gm.side_code < 0 || gm.side_code > 8) continue;
        ++out.units[gm.side_code];
        if (gm.state == 1 || gm.state == 2) ++out.losses[gm.side_code];
    }
    double worst = 0.0;
    for (int i = 0; i < 9; ++i) {
        out.loss_ratio[i] =
            out.units[i] > 0 ? static_cast<double>(out.losses[i]) / out.units[i]
                             : 0.0;
        if (out.units[i] > 0 && out.loss_ratio[i] > worst) {
            worst = out.loss_ratio[i];
            out.worst_side = i;
        }
        if (out.loss_ratio[i] > 0.75) out.collapse = true;
        if (out.loss_ratio[i] > 0.95) out.destroyed = true;
    }
    return out;
}

void advance_campaign_clock(std::array<int, 6>* clock) {
    if (clock == nullptr) return;
    int& day = (*clock)[0];
    int& hour = (*clock)[1];
    int& minute = (*clock)[2];
    if (hour < 0x10) {
        hour += 8;
    } else {
        hour = 0;
    }
    if (hour == 0) ++day;
    minute = 0;
}

namespace {

struct AttackNodeDef {
    int x, y, z;
    const char* action;
};

//! The three attack patterns are constant in every captured run: all 13
//! laydown, 9 mobile and 20 stand-off blocks are byte-identical. Each has six
//! routes.
constexpr int kPatternRoutes = 6;

const AttackNodeDef kLaydown[6][4] = {
    {{0, -9216, 700, "PASS"}, {2048, -5120, 700, "PASS"}, {-1024, 2048, 700, "STRIKE"}, {4096, 6144, 2000, "PASS"}},
    {{0, -9216, 700, "PASS"}, {0, -5120, 700, "PASS"}, {0, 2048, 700, "STRIKE"}, {4096, 6144, 2000, "PASS"}},
    {{0, -9216, 700, "PASS"}, {6144, -5120, 700, "PASS"}, {-3072, 2048, 700, "STRIKE"}, {4096, 6144, 2000, "PASS"}},
    {{0, -9216, 700, "PASS"}, {-4096, -5120, 700, "PASS"}, {2048, 2048, 700, "STRIKE"}, {4096, 6144, 2000, "PASS"}},
    {{0, -9216, 700, "PASS"}, {8192, -5120, 700, "PASS"}, {-4096, 2048, 700, "STRIKE"}, {4096, 6144, 2000, "PASS"}},
    {{0, -9216, 700, "PASS"}, {-10240, -5120, 700, "PASS"}, {4096, 2048, 700, "STRIKE"}, {4096, 6144, 2000, "PASS"}},
};

const AttackNodeDef kStandOff[6][5] = {
    {{-6144, 24576, 1000, "PASS"}, {0, -10240, 2000, "PASS"}, {0, -8192, 2000, "PASS"}, {0, 2048, 2000, "STRIKE"}, {8192, 2048, 2000, "PASS"}},
    {{-6144, 24576, 1000, "PASS"}, {0, -16384, 2000, "PASS"}, {0, -8192, 2000, "PASS"}, {0, 2048, 2000, "STRIKE"}, {8192, 2048, 2000, "PASS"}},
    {{-6144, 24576, 1000, "PASS"}, {0, -22528, 2000, "PASS"}, {0, -8192, 2000, "PASS"}, {0, 2048, 2000, "STRIKE"}, {8192, 2048, 2000, "PASS"}},
    {{-6144, 24576, 1000, "PASS"}, {0, -28672, 2000, "PASS"}, {0, -8192, 2000, "PASS"}, {0, 2048, 2000, "STRIKE"}, {8192, 2048, 2000, "PASS"}},
    {{-6144, 24576, 1000, "PASS"}, {0, 30720, 2000, "PASS"}, {0, -8192, 2000, "PASS"}, {0, 2048, 2000, "STRIKE"}, {8192, 2048, 2000, "PASS"}},
    {{-6144, 24576, 1000, "PASS"}, {0, 26624, 2000, "PASS"}, {0, -8192, 2000, "PASS"}, {0, 2048, 2000, "STRIKE"}, {8192, 2048, 2000, "PASS"}},
};

const AttackNodeDef kMobile[2] = {
    {0, 0, 1000, "STRIKE_THEN_TURN"},
    {1024, 0, 1000, "PASS"},
};

}  // namespace

namespace {

//! Reads the label tables for the given models from the shipped archives.
std::map<std::string, std::vector<SsLabel>> load_model_labels(
    const std::string& data_dir, const std::vector<std::string>& models) {
    std::map<std::string, std::vector<SsLabel>> out;
    if (data_dir.empty() || models.empty()) return out;
    namespace fs = std::filesystem;
    for (const char* archive : {"DID1ST.DAT", "DID.DAT"}) {
        const fs::path path = fs::path(data_dir) / archive;
        if (!fs::exists(path)) continue;
        std::unique_ptr<DidArchive> did;
        try {
            did = std::make_unique<DidArchive>(path);
        } catch (const std::exception&) {
            continue;
        }
        for (const auto& model : models) {
            if (out.count(model) != 0) continue;
            const std::string stem = model.size() > 4 &&
                                             model.compare(model.size() - 4, 4, ".sss") == 0
                                         ? model.substr(0, model.size() - 4)
                                         : model;
            auto entry = did->resolve(stem, "ss\\", ".ssd");
            if (!entry) continue;
            auto bytes = did->read(*entry);
            if (!bytes) continue;
            out[model] = parse_ssd_labels(*bytes);
        }
    }
    return out;
}

}  // namespace

bool generate_campaign(const Scenario& scenario, const Scenario* previous,
                       const std::vector<TargetSite>& table,
                       const GenSettings& settings, GeneratedCampaign* out,
                       std::string* error) {
    if (table.empty()) {
        if (error) *error = "empty target table";
        return false;
    }
    GeneratedCampaign campaign;
    CampaignRng rng(settings.seed);
    for (int i = 0; i < settings.draw_offset; ++i) rng.next();
    if (previous != nullptr) {
        campaign.world_status = previous->world_status;
    } else {
        campaign.world_status = {settings.world_status[0], settings.world_status[1],
                                 settings.world_status[2], settings.world_status[3],
                                 settings.clock[0], settings.clock[1]};
    }
    for (int i = 0; i < settings.advance_steps; ++i) {
        advance_campaign_clock(&campaign.world_status);
    }

    const auto current = index_areas(scenario);
    const auto prev = previous != nullptr ? index_areas(*previous)
                                          : std::map<std::string, const ScenarioArea*>{};

    // 1. Areas. The output order comes from the scenario/state file; the fixed
    //    table supplies position and class by name. Table entries that the
    //    scenario omits (Visby) are inserted after their nearest preceding
    //    table neighbour, which is how the original's extra area lands
    //    between Shimsk and Vardo.
    std::map<std::string, const TargetSite*> table_by_name;
    for (const auto& row : table) table_by_name.emplace(row.name, &row);

    const auto& order_source =
        (previous != nullptr && previous->areas.size() > 1) ? previous->areas
                                                            : scenario.areas;
    std::map<std::string, const ScenarioArea*> listed;
    for (const auto& area : order_source) {
        if (table_by_name.find(area.name) == table_by_name.end()) continue;
        GenArea out_area;
        out_area.name = area.name;
        const TargetSite* row = table_by_name[area.name];
        out_area.model = row->model;
        out_area.x = row->x;
        out_area.y = row->y;
        out_area.heading = row->altitude;
        out_area.type = row->type;
        out_area.side = area.side;
        out_area.airfield1 = area.airfield1;
        out_area.airfield2 = area.airfield2;
        campaign.areas.push_back(std::move(out_area));
        listed.emplace(area.name, &area);
    }
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (listed.count(table[i].name) != 0) continue;
        GenArea out_area;
        out_area.name = table[i].name;
        out_area.model = table[i].model;
        out_area.x = table[i].x;
        out_area.y = table[i].y;
        out_area.heading = table[i].altitude;
        out_area.type = table[i].type;
        out_area.airfield1 = 30;
        out_area.airfield2 = 30;
        std::size_t insert_at = 0;
        bool anchored = false;
        for (std::size_t j = i; j-- > 0;) {
            for (std::size_t k = 0; k < campaign.areas.size(); ++k) {
                if (campaign.areas[k].name == table[j].name) {
                    insert_at = k + 1;
                    anchored = true;
                    break;
                }
            }
            if (anchored) break;
        }
        campaign.areas.insert(campaign.areas.begin() + insert_at,
                              std::move(out_area));
        listed.emplace(table[i].name, scenario.find_area(table[i].name));
    }

    // 2. Target values. One draw per area, in output order: the upper half of
    //    the state selects the thousands digit (0..4). The low part of the
    //    value is the installation's own worth, 1000 minus its damage count,
    //    so a damaged site rolls the value back into the previous thousand.
    for (auto& area : campaign.areas) {
        // Only airfields and ports are valued by the draw. Every other
        // installation stays at 1000 minus its own damage count and consumes
        // no random number.
        const bool valued = is_airfield_type(area.type) ||
                            is_port_type(area.type) || area.type == 204;
        if (!valued) {
            area.value = 1000 - damage_for(area.type, area.side);
        } else {
            const int draw = static_cast<int>((rng.next() >> 16) & 0xFFFFu);
            const int thousands = (draw % 5) * 1000;
            const int site = 1000 - damage_for(area.type, area.side);
            area.value = thousands + site;
        }
        area.map_cell = area.x / 40 + (area.y / 40) * 10;
    }
    for (auto& area : campaign.areas) {
        if (area.side.empty()) {
            area.side = inherit_side(area.name, area.x, area.y, campaign.areas);
        }
        if (area.airfield1 == 0) {
            area.airfield1 = 30;
            area.airfield2 = 30;
        }
    }

    // (Target values are assigned after the areas are put in output order:
    //  the generator draws exactly one value per area, in that order.)

    // 3. Bases: airfields and ports from the table, then the virtual ports.
    for (const auto& area : campaign.areas) {
        if (!is_airfield_type(area.type) && !is_port_type(area.type)) continue;
        GenBase base;
        base.name = area.name;
        base.side = area.side;
        base.type = base_type_for(area.type);
        base.x = area.x;
        base.y = area.y;
        base.value = area.value;
        base.port = is_port_type(area.type);
        base.index = static_cast<int>(campaign.bases.size());
        campaign.bases.push_back(std::move(base));
    }
    for (const auto& vp : kVirtualPorts) {
        GenBase base;
        base.name = vp.name;
        base.side = kSideNames[vp.side];
        base.type = "PORT";
        base.x = vp.x;
        base.y = vp.y;
        base.value = 1000;
        base.port = true;
        base.index = static_cast<int>(campaign.bases.size());
        campaign.bases.push_back(std::move(base));
    }

    // 4. Defence sites around every base area. The offsets come from the
    //    installation model's label table when it is available: every entry
    //    with kind 0xFFFF is one ring position, rotated by the area's heading.
    std::vector<std::string> models;
    for (const auto& area : campaign.areas) {
        if (std::find(models.begin(), models.end(), area.model) == models.end())
            models.push_back(area.model);
    }
    const auto model_labels = load_model_labels(settings.data_dir, models);

    // Target elements: the non-marker entries of the model, rotated by the
    // area heading. A mission carries two, four or six of them.
    for (auto& area : campaign.areas) {
        const auto it = model_labels.find(area.model);
        if (it == model_labels.end()) continue;
        std::vector<const SsLabel*> candidates;
        for (const auto& label : it->second) {
            // Only real target elements: valid type-table entries, and not the
            // dispersal parking spots.
            if (label.kind < 106 && label.kind != 56) candidates.push_back(&label);
        }
        if (candidates.empty()) continue;
        // The original shuffles the element list before taking the first few,
        // which is why neighbouring installations expose different subsets.
        for (std::size_t i = candidates.size(); i > 1; --i) {
            const std::size_t j =
                static_cast<std::size_t>(rng.range(static_cast<int>(i)));
            std::swap(candidates[i - 1], candidates[j]);
        }
        const int want = 2 * (1 + rng.range(3));
        const int n = std::min<int>(want, static_cast<int>(candidates.size()));
        for (int i = 0; i < n; ++i) {
            AreaElement element;
            element.kind = candidates[i]->kind;
            element.x = candidates[i]->x;
            element.y = candidates[i]->y;
            element.z = candidates[i]->z;
            rotate_by_heading(area.heading, &element.x, &element.y);
            area.elements.push_back(element);
        }
    }

    for (auto& area : campaign.areas) {
        // The slot count follows the installation class: airfields carry the
        // full four-slot ring (Vulcan, Roland, Patriot, Rapier -- or SA-6,
        // SA-11, SA-11, ZSU), larger installations three, outlying sites one.
        const int count = site_count_for_type(area.type);
        std::vector<const SsLabel*> ring;
        if (const auto it = model_labels.find(area.model); it != model_labels.end()) {
            for (const auto& label : it->second) {
                if (label.kind == kSsDefencePoint) ring.push_back(&label);
            }
        }
        if (!ring.empty()) {
            for (int i = 0; i < count; ++i) {
                const SsLabel& label = *ring[static_cast<std::size_t>(i) % ring.size()];
                DefenceSite site;
                int ox = label.x;
                int oy = label.y;
                rotate_by_heading(area.heading, &ox, &oy);
                site.off_x = ox;
                site.off_y = oy;
                site.health = 100;
                site.a = rng.between(0, 2047);
                site.b = rng.between(0, 2047);
                area.sites.push_back(site);
            }
            continue;
        }
        const double base_angle = static_cast<double>(rng.between(0, 359));
        for (int i = 0; i < count; ++i) {
            DefenceSite site;
            const double angle = base_angle + 90.0 * i +
                                 static_cast<double>(rng.between(-20, 20));
            const double radius = static_cast<double>(rng.between(8, 12));
            const double radians = angle * 3.14159265358979323846 / 180.0;
            site.off_x = static_cast<int>(std::lround(std::cos(radians) * radius * 64.0));
            site.off_y = static_cast<int>(std::lround(std::sin(radians) * radius * 64.0));
            site.health = 100;
            site.a = rng.between(0, 2047);
            site.b = rng.between(0, 2047);
            area.sites.push_back(site);
        }
    }

    // 5. Mobile index. The live order of battle is carried forward from the
    //    previous campaign state; CONFLICT.CG only seeds the first campaign.
    //    Ground mobiles feed the air-defence pool, air mobiles the flight
    //    tasking and ships the convoys.
    const std::vector<ScenarioMobile>& state_mobiles =
        (previous != nullptr && !previous->mobiles.empty()) ? previous->mobiles
                                                            : scenario.mobiles;
    campaign.state_mobiles = state_mobiles;
    campaign.mobiles.reserve(state_mobiles.size());
    for (std::size_t i = 0; i < state_mobiles.size(); ++i) {
        const auto& m = state_mobiles[i];
        GenMobile gm;
        gm.source = i;
        gm.side_code = side_code(m.side);
        gm.state = status_code(m.status);
        gm.x = m.pos ? m.pos->x : 0;
        gm.y = m.pos ? m.pos->y : 0;
        if (m.kind == "GB") {
            gm.class_id = ground_type_index(m.type);
            if (gm.class_id < 0) gm.class_id = 0;
        } else {
            gm.class_id = -1;
        }
        campaign.mobiles.push_back(gm);
    }

    // Airframes belong to the base at their position and side. A base carries
    // at most 49 of them; the surplus stays in reserve.
    {
        constexpr int kBaseCapacity = 49;
        std::map<std::tuple<std::string, int, int>, int> base_at;
        std::map<int, int> based;
        for (const auto& base : campaign.bases) {
            base_at.emplace(std::make_tuple(base.side, base.x, base.y), base.index);
        }
        for (auto& gm : campaign.mobiles) {
            if (gm.class_id >= 0) continue;  // air and ship mobiles only
            const auto it = base_at.find(
                std::make_tuple(std::string(side_name(gm.side_code)), gm.x, gm.y));
            if (it == base_at.end() || based[it->second] >= kBaseCapacity) {
                gm.base = -1;
                continue;
            }
            gm.base = it->second;
            ++based[it->second];
        }
    }

    // Link each ground mobile to the area at its exact position and side.
    // The scenario places every ground group on a target coordinate, so this
    // is the link the original's coordinate comparison performs.
    std::map<std::tuple<std::string, int, int>, int> area_at;
    for (std::size_t a = 0; a < campaign.areas.size(); ++a) {
        const auto& area = campaign.areas[a];
        if (area.sites.empty()) continue;
        area_at.emplace(std::make_tuple(area.side, area.x, area.y),
                        static_cast<int>(a));
    }
    for (auto& gm : campaign.mobiles) {
        if (gm.class_id < 0) continue;
        const auto it = area_at.find(std::make_tuple(std::string(side_name(gm.side_code)),
                                                     gm.x, gm.y));
        gm.area = it == area_at.end() ? -1 : it->second;
    }

    const std::set<std::pair<int, int>> nav_points =
        load_nav_points(settings.data_dir);

    Emitter emit;
    emit.doc = &campaign.missions;

    const std::uint32_t stamp = static_cast<std::uint32_t>(std::time(nullptr));
    const std::string when = now_stamp();

    // --- header ------------------------------------------------------------
    {
        auto& lines = campaign.missions.lines;
        MissionLine first;
        first.comment = true;
        first.lf_only = true;
        first.text = ";" + std::to_string(stamp) +
                     " Conflict generator Mission file. " + when;
        lines.push_back(std::move(first));
        emit.blank();
        char num[64];
        std::snprintf(num, sizeof(num), ";NUM_MISSIONS <no. missions>");
        MissionLine note;
        note.comment = true;
        note.text = num;
        lines.push_back(std::move(note));
    }
    const std::size_t num_missions_line = campaign.missions.lines.size();
    emit.addv("NUM_MISSIONS", {"0", "INCLUDE_PILOTS"});
    emit.blank();
    emit.addv("WORLD_STATUS",
              {Number(campaign.world_status[0]), Number(campaign.world_status[1]),
               Number(campaign.world_status[2]), Number(campaign.world_status[3])});

    // 6. Air-defence tasking.
    std::array<int, 27> used{};
    std::vector<int> mission_counters;
    int mission_id = 0;
    for (std::size_t a = 0; a < campaign.areas.size(); ++a) {
        auto& area = campaign.areas[a];
        if (area.sites.empty()) continue;
        const int side = side_code(area.side);
        if (side < 0) continue;
        used.fill(0);
        int available[kGroundTypeCount] = {};
        for (const auto& gm : campaign.mobiles) {
            if (gm.class_id < 0 || gm.area != static_cast<int>(a)) continue;
            if (gm.used) continue;
            // Dead and crippled units are not available for tasking.
            if (gm.state == 1 || gm.state == 2) continue;
            ++available[gm.class_id];
        }
        const int* cycle = is_western(side) ? kWestDefence : kEastDefence;
        for (std::size_t s = 0; s < area.sites.size(); ++s) {
            const int weapon = cycle[(s + 1) % 4];
            const int remaining = available[weapon] - used[weapon];
            int count = 0;
            if (remaining > 8) {
                count = 8;
            } else if (remaining > 0) {
                count = remaining;
            } else {
                continue;
            }
            used[weapon] += count;
            area.sites[s].weapon_class = weapon;
            area.sites[s].readiness = site_readiness(weapon);

            // Consume unused mobiles of this class for the group.
            int consumed = 0;
            for (auto& gm : campaign.mobiles) {
                if (gm.class_id != weapon || gm.area != static_cast<int>(a)) continue;
                if (gm.used || gm.state == 1 || gm.state == 2) continue;
                gm.used = true;
                gm.mission = mission_id;
                if (++consumed >= count) break;
            }

            const int id = mission_id++;
            emit.addv("MISSION", {"GBASED", area.side, "AIR_DEFENSE", "OK", "-1",
                                  Number(id), "1"});
            emit.addv("GROUP", {Number(count), kGroundTypes[weapon], "INTERCEPT",
                                "OK"});
            emit.addv("3DP", {Number(area.x), Number(area.y), "0",
                              Number(area.sites[s].off_x),
                              Number(area.sites[s].off_y), "0"});
            emit.addv("WP_ROUTE", {"1", "0", "0"});
            emit.addv("WAYPOINT",
                      {"0", Number(area.x), Number(area.y), "0",
                       Number(area.sites[s].off_x), Number(area.sites[s].off_y),
                       rng.range(2) == 0 ? "SCAN_TO_LOCK" : "SCAN_TO_FIRE", "0",
                       "0", "0"});
            emit.addv("TARGET", {"MISSION", "0", "GROUP", "0"});
            emit.blank();
        }
    }
    const int gbased_count = mission_id;

    //! Missions per base, used for the 24-hour TIMESLOT schedule.
    std::map<int, std::vector<std::pair<int, std::string>>> schedule;
    std::map<int, int> mission_flights;   //!< flights (or ship groups) per id
    std::map<int, int> mission_route_len;  //!< rounded route length per id
    // 7. Sea tasking: one convoy per ship group, scaled by group size.
    {
        std::map<std::pair<std::string, std::pair<int, int>>, int> groups;
        for (const auto& m : campaign.state_mobiles) {
            if (m.kind != "SH" || !m.pos) continue;
            ++groups[{m.side, {m.pos->x, m.pos->y}}];
        }
        for (const auto& [key, ships] : groups) {
            const int convoys = std::max(1, (ships + 15) / 30);
            const int side = side_code(key.first);
            const bool east = !is_western(side);
            const char* hull = east ? "UDALOY_CLASS" : "TYPE42_CLASS";
            int convoy_base = -1;
            for (const auto& base : campaign.bases) {
                if (base.x == key.second.first && base.y == key.second.second) {
                    convoy_base = base.index;
                    break;
                }
            }
            for (int i = 0; i < convoys; ++i) {
                const int id = mission_id++;
                if (convoy_base >= 0)
                    schedule[convoy_base].emplace_back(id, "SEA_SUPPLY");
                mission_flights[id] = 3;
                emit.addv("MISSION", {"SHIP", key.first, "SEA_SUPPLY", "OK",
                                      Number(convoy_base >= 0 ? convoy_base : -1),
                                      Number(id), "3"});
                emit.addv("GROUP", {"3", hull, "ESCORT", "OK"});
                emit.addv("GROUP", {"3", "SUPPLY_SHIP", "SUPPLY", "OK"});
                emit.addv("GROUP", {"3", "OIL_TANKER", "SUPPLY", "OK"});
                emit.addv("3DP", {Number(key.second.first), Number(key.second.second),
                                  "0", "0", "0", "0"});
                emit.addv("WP_ROUTE", {"2", "0", "0"});
                emit.addv("WAYPOINT",
                          {"0", Number(key.second.first), Number(key.second.second),
                           "0", "0", "0", "LAUNCH", "0", "0", "0"});
                emit.addv("WAYPOINT",
                          {"1", Number(key.second.first), Number(key.second.second),
                           "0", "0", "0", "LANDING", "0", "0", "0"});
                emit.addv("TARGET", {"MISSION", "0", "GROUP", "0"});
                emit.blank();
            }
        }
    }
    const int ship_count = mission_id - gbased_count;

    out->aircraft = 0;
    // 8. Air tasking. The role totals are invariant across runs; the split per
    //    side follows the side's share of the air order of battle.
    {
        struct RoleRequest {
            const char* role;
            const char* side;
            int count;
            int ships;
        };
        static const RoleRequest kRoles[] = {
            {"CAP", "RUSSIAN", 96, 2},   {"CAP", "SWEDISH", 24, 2},
            {"CAP", "FINNISH", 24, 2},   {"CAP", "NATO", 18, 2},
            {"INTC", "RUSSIAN", 15, 2},  {"INTC", "NATO", 5, 2},
            {"INTC", "SWEDISH", 3, 2},   {"INTC", "FINNISH", 3, 2},
            {"AF_KILL", "NATO", 9, 4},   {"AF_KILL", "RUSSIAN", 6, 4},
            {"INTERD", "NATO", 11, 4},   {"INTERD", "RUSSIAN", 1, 4},
            {"SEAD", "RUSSIAN", 2, 4},   {"REFUELLER_PATROL", "NATO", 3, 1},
            {"REFUELLER_PATROL", "RUSSIAN", 3, 1},
            {"REFUELLER_KILL", "NATO", 1, 2},
            {"REFUELLER_KILL", "RUSSIAN", 1, 2},
            {"AWACS_PATROL", "NATO", 1, 1},
            {"AWACS_PATROL", "RUSSIAN", 1, 1},
            {"AWACS_KILL", "RUSSIAN", 1, 2},
            {"JSTARS_PATROL", "NATO", 1, 1},
        };

        std::vector<const ScenarioPilot*> pilots;
        for (const auto& m : scenario.mobiles) {
            if (m.kind == "PL" && m.has_pilot) pilots.push_back(&m.pilot);
        }
        std::size_t next_pilot = 0;

        std::map<std::string, std::vector<const GenBase*>> bases_by_side;
        for (const auto& base : campaign.bases) {
            if (!base.port) bases_by_side[base.side].push_back(&base);
        }
        std::map<std::string, std::size_t> next_base;

        // Strike packages refuel only when their base hosts a tanker and the
        // target is beyond a hundred cells. The tanker bases are deterministic
        // for the package table, so work them out before emitting anything.
        std::set<int> tanker_bases;
        {
            std::map<std::string, std::size_t> cursor;
            for (const auto& req : kRoles) {
                auto it = bases_by_side.find(req.side);
                if (it == bases_by_side.end() || it->second.empty()) continue;
                const auto& list = it->second;
                for (int n = 0; n < req.count; ++n) {
                    const GenBase* base = list[cursor[req.side]++ % list.size()];
                    if (std::string(req.role) == "REFUELLER_PATROL")
                        tanker_bases.insert(base->index);
                }
            }
        }

        // Which aircraft types are based where. Used to pick a base for the
        // support flights and to keep CAP at the airfields that hold fighters.
        std::map<std::string, std::vector<const GenBase*>> type_base_list;
        {
            std::map<int, std::set<std::string>> base_types;
            for (const auto& gm : campaign.mobiles) {
                if (gm.class_id >= 0 || gm.used) continue;
                if (gm.state == 1 || gm.state == 2) continue;
                if (gm.base < 0 || gm.source >= campaign.state_mobiles.size())
                    continue;
                base_types[gm.base].insert(campaign.state_mobiles[gm.source].type);
            }
            for (const auto& base : campaign.bases) {
                const auto it = base_types.find(base.index);
                if (it == base_types.end()) continue;
                for (const auto& type : it->second)
                    type_base_list[type].push_back(&base);
            }
            for (auto& [type, list] : type_base_list) {
                std::sort(list.begin(), list.end(),
                          [](const GenBase* a, const GenBase* b) {
                              return a->index < b->index;
                          });
            }
        }

        // Strike packages are grouped by airfield: the capture fills one
        // airfield's strike aircraft before moving to the next. Airfields are
        // ordered by the role suitability of their aircraft type (torndo
        // before harrier, Su-25 before Su-34, ...), then by index.
        auto role_preferred = [](const std::string& side, const std::string& role) {
            if (role == "AF_KILL") {
                if (side == "RUSSIAN")
                    return std::vector<std::string>{"SU25", "SU34", "MIG27"};
                if (side == "SWEDISH") return std::vector<std::string>{"JAS39"};
                if (side == "FINNISH") return std::vector<std::string>{"F18"};
                if (side == "BALTIC") return std::vector<std::string>{"SU27", "MIG29"};
                return std::vector<std::string>{"TORNADO", "HARRIER_GR6", "F117A",
                                                "F111R"};
            }
            if (role == "INTERD") {
                if (side == "RUSSIAN")
                    return std::vector<std::string>{"SU34", "SU25", "MIG27"};
                if (side == "SWEDISH") return std::vector<std::string>{"JAS39"};
                if (side == "FINNISH") return std::vector<std::string>{"F18"};
                if (side == "BALTIC") return std::vector<std::string>{"SU27", "MIG29"};
                return std::vector<std::string>{"F117A", "F111R", "TORNADO",
                                                "HARRIER_GR6"};
            }
            if (role == "SEAD") {
                if (side == "RUSSIAN")
                    return std::vector<std::string>{"SU35", "SU27", "SU34"};
                return std::vector<std::string>{"EFA", "F22", "TORNADO"};
            }
            return std::vector<std::string>{};
        };
        auto count_type_at_base = [&](const GenBase* base, const std::string& type) {
            int n = 0;
            for (const auto& gm : campaign.mobiles) {
                if (gm.class_id >= 0 || gm.used) continue;
                if (gm.state == 1 || gm.state == 2) continue;
                if (gm.base != base->index) continue;
                if (gm.source < campaign.state_mobiles.size() &&
                    campaign.state_mobiles[gm.source].type == type)
                    ++n;
            }
            return n;
        };
        std::map<std::string, std::vector<const GenBase*>> role_base_order;
        {
            for (const auto& req : kRoles) {
                const std::string role = req.role;
                if (role != "AF_KILL" && role != "INTERD" && role != "SEAD")
                    continue;
                const auto preferred = role_preferred(req.side, role);
                if (preferred.empty()) continue;
                auto it = bases_by_side.find(req.side);
                if (it == bases_by_side.end()) continue;
                std::vector<std::pair<int, const GenBase*>> ranked;
                for (const GenBase* base : it->second) {
                    int rank = -1;
                    for (std::size_t i = 0; i < preferred.size(); ++i) {
                        if (count_type_at_base(base, preferred[i]) >= 4) {
                            rank = static_cast<int>(i);
                            break;
                        }
                    }
                    if (rank < 0) continue;
                    ranked.emplace_back(rank, base);
                }
                std::sort(ranked.begin(), ranked.end(),
                          [](const auto& a, const auto& b) {
                              if (a.first != b.first) return a.first < b.first;
                              return a.second->index < b.second->index;
                          });
                auto& order = role_base_order[std::string(req.side) + "/" + role];
                for (const auto& entry : ranked) order.push_back(entry.second);
            }
        }
        std::map<std::string, std::size_t> role_base_cursor;

        // CAP sorties are not spread evenly over a side's airfields. The
        // capture fills the highest-numbered (front-line) airfields with eight
        // sorties each while leaving four for every airfield that remains, and
        // gives the leftovers to the lower ones. This reproduces all four
        // sides exactly.
        std::map<std::string, std::vector<const GenBase*>> cap_plan;
        std::map<std::string, std::size_t> cap_cursor;
        std::map<std::string, int> support_serial;
        {
            std::map<std::string, int> quota;
            for (const auto& req : kRoles) {
                if (std::string(req.role) == "CAP") quota[req.side] += req.count;
            }
            for (const auto& [side, list] : bases_by_side) {
                std::vector<const GenBase*> sorted;
                for (const GenBase* base : list) {
                    bool has_aircraft = false;
                    for (const auto& gm : campaign.mobiles) {
                        if (gm.class_id >= 0 || gm.base != base->index) continue;
                        has_aircraft = true;
                        break;
                    }
                    if (has_aircraft) sorted.push_back(base);
                }
                std::sort(sorted.begin(), sorted.end(),
                          [](const GenBase* a, const GenBase* b) {
                              return a->index < b->index;
                          });
                std::vector<int> per_base(sorted.size(), 0);
                int remaining = quota[side];
                for (int i = static_cast<int>(sorted.size()) - 1; i >= 0; --i) {
                    int give = std::min(8, std::max(0, remaining - 4 * i));
                    give = std::min(give, remaining);
                    per_base[static_cast<std::size_t>(i)] = give;
                    remaining -= give;
                }
                if (remaining > 0 && !per_base.empty())
                    per_base.back() += remaining;
                for (std::size_t i = 0; i < sorted.size(); ++i) {
                    for (int n = 0; n < per_base[i]; ++n)
                        cap_plan[side].push_back(sorted[i]);
                }
            }
        }

        // Airfield-denial packages strike the other side's airfields,
        // interdiction its ports, rigs, bridges and tunnels, and SEAD its
        // early-warning sites. The capture keeps the campaign's area order.
        auto enemy_of = [](const std::string& side) {
            if (side == "NATO") return std::string("RUSSIAN");
            if (side == "RUSSIAN") return std::string("NATO");
            return std::string();
        };
        auto is_interdiction_type = [](int type) {
            return type == 79 || type == 91 || type == 95 || type == 96 ||
                   type == 127 || type == 142 || type == 143 || type == 204;
        };
        auto near_friendly = [&](const GenArea& area, const std::string& side) {
            // Strike planning only reaches installations near the attacker's
            // airfields; the captures never target the far east from Norway.
            long best = -1;
            for (const auto& base : campaign.bases) {
                if (base.side != side || base.port) continue;
                const long d = std::abs(base.x - area.x) + std::abs(base.y - area.y);
                if (best < 0 || d < best) best = d;
            }
            return best >= 0 && best <= 191;
        };
        std::map<std::string, std::vector<const GenArea*>> airfields_of;
        std::map<std::string, std::vector<const GenArea*>> interd_of;
        std::map<std::string, std::vector<const GenArea*>> ewr_of;
        for (const auto& area : campaign.areas) {
            if (area.side != "NATO" && area.side != "RUSSIAN") continue;
            if (is_airfield_type(area.type)) {
                airfields_of[area.side].push_back(&area);
            } else if (area.type == 63) {
                if (near_friendly(area, enemy_of(area.side)))
                    ewr_of[area.side].push_back(&area);
            } else if (is_interdiction_type(area.type)) {
                if (near_friendly(area, enemy_of(area.side)))
                    interd_of[area.side].push_back(&area);
            }
        }
        // Airfield denial concentrates on the three nearest enemy airfields.
        for (auto& [side, list] : airfields_of) {
            if (list.size() > 3) list.resize(3);
        }
        std::map<std::string, std::size_t> target_index;

        auto loadout_for = [](const char* type, const char* flight_role) {
            std::vector<std::string> out(20, "NO_WEAPON");
            const std::string t(type), r(flight_role);
            if (t == "EFA") {
                if (r == "STRIKE") {
                    const char* w[] = {"ASRAAM", "ASRAAM", "MK_82", "MK_83",
                                       "MK_83",  "AMRAAM", "AMRAAM", "SS225",
                                       "AMRAAM", "SS225",  "AMRAAM", "CENTRE_FUEL"};
                    for (int i = 0; i < 12; ++i) out[i] = w[i];
                } else if (r == "WW") {
                    const char* w[] = {"ASRAAM", "ASRAAM", "ALARM",  "ALARM",
                                       "ALARM",  "ALARM",  "BL_755", "BL_755",
                                       "SS225",  "AMRAAM", "SS225",  "AMRAAM",
                                       "CENTRE_FUEL"};
                    for (int i = 0; i < 13; ++i) out[i] = w[i];
                } else {
                    const char* w[] = {"ASRAAM", "ASRAAM", "ASRAAM", "ASRAAM",
                                       "AMRAAM", "AMRAAM", "AMRAAM", "AMRAAM",
                                       "SS225",  "AMRAAM", "SS225",  "AMRAAM",
                                       "CENTRE_FUEL"};
                    for (int i = 0; i < 13; ++i) out[i] = w[i];
                }
            } else if (t == "JAS39") {
                const char* w[] = {"ASRAAM", "ASRAAM", "AMRAAM", "AMRAAM",
                                   "SS225",  "SS225"};
                for (int i = 0; i < 6; ++i) out[i] = w[i];
            } else if (t == "SU35") {
                if (r == "STRIKE") {
                    const char* w[] = {"APHID", "APHID", "KAB_500", "KAB_500",
                                       "ARCHER", "ARCHER", "ARCHER", "ARCHER"};
                    for (int i = 0; i < 8; ++i) out[i] = w[i];
                } else {
                    const char* w[] = {"APHID", "APHID", "ALAMO", "ALAMO",
                                       "ARCHER", "ARCHER", "ARCHER", "ARCHER"};
                    for (int i = 0; i < 8; ++i) out[i] = w[i];
                }
            }
            return out;
        };

        auto unused_at = [&](const GenBase* base, const std::string& type) {
            int n = 0;
            for (const auto& gm : campaign.mobiles) {
                if (gm.class_id >= 0 || gm.used) continue;
                if (gm.state == 1 || gm.state == 2) continue;
                if (gm.base != base->index) continue;
                if (gm.source < campaign.state_mobiles.size() &&
                    campaign.state_mobiles[gm.source].type != type)
                    continue;
                ++n;
            }
            return n;
        };
        auto fighter_types = [](const std::string& side) {
            if (side == "RUSSIAN")
                return std::vector<std::string>{"SU35", "SU27", "SU33", "MIG29",
                                                "MIG21"};
            if (side == "SWEDISH") return std::vector<std::string>{"JAS39"};
            if (side == "FINNISH") return std::vector<std::string>{"F18"};
            if (side == "BALTIC") return std::vector<std::string>{"SU27", "MIG29"};
            return std::vector<std::string>{"F15", "F22", "EFA", "F18"};
        };
        // Escorts fly the multi-role fighter first; CAP and interceptors take
        // the dedicated interceptor when the base has one.
        auto escort_types = [](const std::string& side) {
            if (side == "RUSSIAN")
                return std::vector<std::string>{"SU35", "SU33", "SU27", "MIG29"};
            if (side == "SWEDISH") return std::vector<std::string>{"JAS39"};
            if (side == "FINNISH") return std::vector<std::string>{"F18"};
            if (side == "BALTIC") return std::vector<std::string>{"SU27", "MIG29"};
            return std::vector<std::string>{"EFA", "F22", "F15", "F18"};
        };
        auto strike_types = [](const std::string& side) {
            if (side == "RUSSIAN")
                return std::vector<std::string>{"SU35", "SU34", "SU25", "MIG27",
                                                "SU27", "MIG29"};
            if (side == "SWEDISH") return std::vector<std::string>{"JAS39"};
            if (side == "FINNISH") return std::vector<std::string>{"F18"};
            if (side == "BALTIC") return std::vector<std::string>{"SU27", "MIG29"};
            return std::vector<std::string>{"EFA", "TORNADO", "F22", "F117A",
                                            "HARRIER_GR6", "F111R"};
        };
        // First unused airframe at the base whose type is in the class. The
        // original walks the stored order of battle, not a preferred list.
        auto mobile_first = [&](const GenBase* base,
                                const std::vector<std::string>& types) {
            for (const auto& gm : campaign.mobiles) {
                if (gm.class_id >= 0 || gm.used) continue;
                if (gm.state == 1 || gm.state == 2) continue;
                if (gm.base != base->index) continue;
                if (gm.source >= campaign.state_mobiles.size()) continue;
                const std::string& type = campaign.state_mobiles[gm.source].type;
                if (types.empty() ||
                    std::find(types.begin(), types.end(), type) != types.end())
                    return type;
            }
            return std::string();
        };
        auto preferred_type = [&](const GenBase* base,
                                  const std::vector<std::string>& types) {
            for (const auto& type : types) {
                if (unused_at(base, type) > 0) return type;
            }
            // Never fall back to a tanker or AWACS airframe for a combat
            // flight; keep the role's preferred type even if the base is out.
            return types.empty() ? std::string("EFA") : types.front();
        };
        auto choose_flight_type = [&](const GenBase* base,
                                      const std::string& role) {
            const std::string& side = base->side;
            if (role == "AWACS_PATROL")
                return preferred_type(base, {side == "RUSSIAN" ? "A50" : "E767"});
            if (role == "JSTARS_PATROL") return preferred_type(base, {"J767"});
            if (role == "REFUELLER_PATROL")
                return preferred_type(base, {side == "RUSSIAN" ? "IL78M" : "R767"});
            if (role == "STRIKE" || role == "WW" || role == "SEAD_STRIKE" ||
                role == "AF_KILL" || role == "INTERD" || role == "SEAD") {
                const std::string first = mobile_first(base, strike_types(side));
                if (!first.empty()) return first;
                return preferred_type(base, strike_types(side));
            }
            if (role == "ESCORT" || role == "INTERCEPT" || role == "REFUELLER_KILL" ||
                role == "AWACS_KILL")
                return preferred_type(base, escort_types(side));
            return preferred_type(base, fighter_types(side));
        };
        auto support_base = [&](const std::string& type, int nth) -> const GenBase* {
            const auto it = type_base_list.find(type);
            if (it == type_base_list.end() || it->second.empty()) return nullptr;
            const auto& list = it->second;
            return list[static_cast<std::size_t>(nth) % list.size()];
        };
        auto take_airframes = [&](const GenBase* base, const std::string& type,
                                  int ships, int mission_id, int flight_index) {
            int taken = 0;
            for (auto& gm : campaign.mobiles) {
                if (taken >= ships) break;
                if (gm.class_id >= 0 || gm.used) continue;
                if (gm.state == 1 || gm.state == 2) continue;
                if (gm.base != base->index) continue;
                if (gm.source < campaign.state_mobiles.size() &&
                    campaign.state_mobiles[gm.source].type != type)
                    continue;
                gm.used = true;
                gm.mission = mission_id;
                gm.flight = flight_index;
                ++taken;
            }
            return taken;
        };

        auto emit_flight = [&](const GenBase* base, const char* type,
                               const char* flight_role, int ships,
                               const GenArea* target,
                               const std::vector<std::array<int, 4>>* route,
                               int wave, bool target_area = true,
                               int mission_id = -1) {
            take_airframes(base, type, ships, mission_id, wave);
            if (mission_id >= 0) ++mission_flights[mission_id];
            if (route != nullptr && mission_id >= 0) {
                double len = 0.0;
                for (std::size_t i = 1; i < route->size(); ++i) {
                    const double dx = (*route)[i][0] - (*route)[i - 1][0];
                    const double dy = (*route)[i][1] - (*route)[i - 1][1];
                    len += std::sqrt(dx * dx + dy * dy);
                }
                mission_route_len[mission_id] =
                    static_cast<int>(std::lround(len));
            }
            emit.addv("FLIGHT", {Number(ships), type, flight_role, "OK"});
            const auto pylons = loadout_for(type, flight_role);
            for (int p = 0; p < 20; ++p) emit.addv("PYLON", {Number(p), pylons[p]});
            emit.addv("3DP", {Number(base->x), Number(base->y), "0", "0", "0", "0"});
            const std::string r(flight_role);
            if (route != nullptr && target != nullptr) {
                static const char* const kRouteAction[] = {
                    "TAKE_OFF", "REFUEL", "NO_ACTION", "INGRESS", "STRIKE_TARGET",
                    "LANDING"};
                // Only the strike flight marks ingress and target; the escort
                // and wild-weasel flights fly the same corridor as plain fixes.
                const bool mark_target = r == "STRIKE" || r == "SEAD_STRIKE";
                emit.addv("WP_ROUTE", {Number(static_cast<int>(route->size())), "0",
                                       "0"});
                for (std::size_t i = 0; i < route->size(); ++i) {
                    int action = (*route)[i][3];
                    if (!mark_target && (action == 3 || action == 4)) action = 2;
                    emit.addv("WAYPOINT",
                              {Number(static_cast<int>(i)), Number((*route)[i][0]),
                               Number((*route)[i][1]), Number((*route)[i][2]), "0",
                               "0", kRouteAction[action], "0", "0", "0"});
                }
                emit.addv("TARGET", {"MISSION", "0", "GROUP", "0"});
                if ((r == "STRIKE" || r == "SEAD_STRIKE") && target_area) {
                    emit.addv("TARGET_AREA",
                              {base_type_for(target->type),
                               "\"" + target->name + "\""});
                    emit.addv("3DP", {Number(target->x), Number(target->y), "0",
                                      "0", "0", "0"});
                    static const char* const kSlots[] = {"LEAD",  "TWO", "THREE",
                                                         "FOUR", "FIVE", "SIX"};
                    if (!target->elements.empty()) {
                        const std::size_t n =
                            std::min<std::size_t>(target->elements.size(), 6);
                        for (std::size_t i = 0; i < n; ++i) {
                            const auto& element = target->elements[i];
                            emit.addv(kSlots[i],
                                      {"TARGET", element_name(element.kind),
                                       "\"\"", Number(element.x),
                                       Number(element.y), Number(element.z), "0",
                                       "0", "0"});
                        }
                    } else {
                        emit.addv("LEAD", {"TARGET", "HANGAR8", "\"\"", "0", "0",
                                           "0", "0", "0", "0"});
                    }
                }
                // The attack geometry is a fixed template per pattern type and
                // depends only on the flight role and the target class.
                if (r == "WW") {
                    emit.addv("ATTACK_PATTERN", {"MOBILE_ATTACK",
                                                 Number(kPatternRoutes)});
                    for (int route = 0; route < kPatternRoutes; ++route) {
                        emit.addv("ATTACK_ROUTE", {"2"});
                        for (const auto& node : kMobile) {
                            emit.addv("ATTACK_NODE",
                                      {Number(node.x), Number(node.y),
                                       Number(node.z), "0", "0", "0", node.action});
                        }
                    }
                } else if (r == "SEAD_STRIKE" ||
                           (target != nullptr && !is_airfield_type(target->type))) {
                    emit.addv("ATTACK_PATTERN", {"STAND_OFF_ATTACK",
                                                 Number(kPatternRoutes)});
                    for (int route = 0; route < kPatternRoutes; ++route) {
                        emit.addv("ATTACK_ROUTE", {"5"});
                        for (const auto& node : kStandOff[route]) {
                            emit.addv("ATTACK_NODE",
                                      {Number(node.x), Number(node.y),
                                       Number(node.z), "0", "0", "0", node.action});
                        }
                    }
                } else if (r == "STRIKE") {
                    emit.addv("ATTACK_PATTERN", {"LAYDOWN_ATTACK",
                                                 Number(kPatternRoutes)});
                    for (int route = 0; route < kPatternRoutes; ++route) {
                        emit.addv("ATTACK_ROUTE", {"4"});
                        for (const auto& node : kLaydown[route]) {
                            emit.addv("ATTACK_NODE",
                                      {Number(node.x), Number(node.y),
                                       Number(node.z), "0", "0", "0", node.action});
                        }
                    }
                }
            } else {
                emit.addv("WP_ROUTE", {"8", "0", "0"});
                emit.addv("WAYPOINT", {"0", Number(base->x), Number(base->y), "0",
                                       "0", "0", "TAKE_OFF", "0", Number(wave),
                                       "0"});
                const bool support = r == "AWACS" || r == "JSTAR" ||
                                     r == "REFUEL";
                const int alt = support ? 250 : 90;
                // Fighters fly a compact box offset from the base; AWACS and
                // JSTARS orbit a station about forty units out; tankers orbit
                // close to their base on a wide racetrack.
                const int spread = (r == "AWACS" || r == "JSTAR")
                                       ? 17
                                       : (r == "REFUEL" ? 19 : 3);
                int cx = base->x;
                int cy = base->y;
                if (r == "AWACS" || r == "JSTAR") {
                    const double angle = static_cast<double>(rng.between(0, 359)) *
                                         3.14159265358979323846 / 180.0;
                    cx += static_cast<int>(std::lround(std::cos(angle) * 42.0));
                    cy += static_cast<int>(std::lround(std::sin(angle) * 42.0));
                } else if (r == "REFUEL") {
                    cx += rng.between(-4, 4);
                    cy += rng.between(-4, 4);
                } else {
                    cx += rng.between(-18, 18);
                    cy += rng.between(-18, 18);
                }
                int loop_x = cx;
                int loop_y = cy;
                for (int w = 1; w <= 5; ++w) {
                    const int px = cx + rng.between(-spread, spread);
                    const int py = cy + rng.between(-spread, spread);
                    if (w == 1) {
                        loop_x = px;
                        loop_y = py;
                    }
                    emit.addv("WAYPOINT",
                              {Number(w), Number(px), Number(py), Number(alt),
                               Number(rng.between(0, 2047)),
                               Number(rng.between(0, 2047)), "PATROL", "0", "0",
                               "0"});
                }
                emit.addv("WAYPOINT", {"6", Number(loop_x), Number(loop_y),
                                       Number(alt), "0", "0", "PATROL_LOOP", "7",
                                       Number(wave), "0"});
                emit.addv("WAYPOINT", {"7", Number(base->x), Number(base->y), "0",
                                       "0", "0", "LANDING", "7",
                                       Number(wave + 2), "0"});
                emit.addv("TARGET", {"MISSION", "0", "GROUP", "0"});
            }
            for (int p = 0; p < ships && !pilots.empty(); ++p) {
                const auto* pilot = pilots[next_pilot++ % pilots.size()];
                std::vector<std::string> fields{"\"" + pilot->name + "\""};
                for (int f = 0; f < 11; ++f) fields.push_back(Number(pilot->fields[f]));
                emit.addv("PIL", fields);
                emit.blank();
            }
            emit.blank();
        };

        std::map<std::string, int> role_serial;
        for (const auto& req : kRoles) {
            auto it = bases_by_side.find(req.side);
            if (it == bases_by_side.end() || it->second.empty()) continue;
            const auto& list = it->second;
            for (int n = 0; n < req.count; ++n) {
                std::size_t& cursor = next_base[req.side];
                const GenBase* base = list[cursor % list.size()];
                ++cursor;
                const int id = mission_id++;
                const std::string role = req.role;
                const int serial = role_serial[role]++;
                if (role == "CAP") {
                    const auto& plan = cap_plan[req.side];
                    if (!plan.empty())
                        base = plan[cap_cursor[req.side]++ % plan.size()];
                } else if (role == "AWACS_PATROL") {
                    const char* want = std::string(req.side) == "RUSSIAN" ? "A50" : "E767";
                    if (const GenBase* b =
                            support_base(want, support_serial[req.side + role]++))
                        base = b;
                } else if (role == "JSTARS_PATROL") {
                    if (const GenBase* b =
                            support_base("J767", support_serial[req.side + role]++))
                        base = b;
                } else if (role == "REFUELLER_PATROL") {
                    const char* want =
                        std::string(req.side) == "RUSSIAN" ? "IL78M" : "R767";
                    if (const GenBase* b =
                            support_base(want, support_serial[req.side + role]++))
                        base = b;
                } else if (role == "REFUELLER_KILL" || role == "AWACS_KILL") {
                    const char* want =
                        std::string(req.side) == "RUSSIAN" ? "IL78M" : "R767";
                    if (const GenBase* b = support_base(want, 0)) base = b;
                } else if (role == "AF_KILL" || role == "INTERD" ||
                           role == "SEAD") {
                    const std::string key = std::string(req.side) + "/" + role;
                    const auto oit = role_base_order.find(key);
                    if (oit != role_base_order.end()) {
                        const auto types = strike_types(req.side);
                        auto& cursor = role_base_cursor[key];
                        auto has_strike = [&](const GenBase* b) {
                            for (const auto& gm : campaign.mobiles) {
                                if (gm.class_id >= 0 || gm.used) continue;
                                if (gm.state == 1 || gm.state == 2) continue;
                                if (gm.base != b->index) continue;
                                if (gm.source >= campaign.state_mobiles.size())
                                    continue;
                                const std::string& t =
                                    campaign.state_mobiles[gm.source].type;
                                if (std::find(types.begin(), types.end(), t) !=
                                    types.end())
                                    return true;
                            }
                            return false;
                        };
                        while (cursor < oit->second.size() &&
                               !has_strike(oit->second[cursor]))
                            ++cursor;
                        if (cursor < oit->second.size())
                            base = oit->second[cursor];
                    }
                }
                schedule[base->index].emplace_back(id, role);
                // The mission line carries the flying base and the number of
                // flights in the package, as the captured files do.
                const bool support = role == "AWACS_PATROL" ||
                                     role == "JSTARS_PATROL" ||
                                     role == "REFUELLER_PATROL";

                const GenArea* target = nullptr;
                const bool strike_role = role == "AF_KILL" || role == "INTERD" ||
                                         role == "SEAD";
                int strike_ships = 4;
                const std::string enemy = enemy_of(req.side);
                if (strike_role && !enemy.empty()) {
                    const auto& list =
                        role == "AF_KILL"
                            ? airfields_of[enemy]
                            : (role == "SEAD" ? ewr_of[enemy] : interd_of[enemy]);
                    // The planned targets for an airfield are the ones within
                    // reach of that airfield, in campaign order. The captured
                    // packages never cross the whole map to a target.
                    std::vector<const GenArea*> local;
                    for (const GenArea* area : list) {
                        if (std::abs(base->x - area->x) +
                                std::abs(base->y - area->y) <=
                            191)
                            local.push_back(area);
                    }
                    if (local.empty()) local = list;
                    if (!local.empty()) {
                        std::size_t& index =
                            target_index[std::to_string(base->index) + "/" + role];
                        if (index >= local.size()) {
                            // The captured blocks cycle back to the nearest
                            // target rather than restarting the list.
                            std::size_t best = 0;
                            long best_d = -1;
                            for (std::size_t i = 0; i < local.size(); ++i) {
                                const long d = std::abs(base->x - local[i]->x) +
                                               std::abs(base->y - local[i]->y);
                                if (best_d < 0 || d < best_d) {
                                    best_d = d;
                                    best = i;
                                }
                            }
                            target = local[best];
                        } else {
                            target = local[index];
                        }
                        ++index;
                    }
                }
                if (target != nullptr && !target->elements.empty()) {
                    strike_ships = std::clamp(
                        static_cast<int>(target->elements.size()), 2, 6);
                }
                if (role == "SEAD") strike_ships = 6;

                const std::string type = choose_flight_type(base, role);
                const std::string escort = choose_flight_type(base, "ESCORT");

                // Extra flights are only flown when the airfield still holds
                // the aircraft for them, and an escort takes whatever is left
                // (the capture mixes 4-, 3-, 2- and 1-ship escorts).
                bool af_ww = false;
                bool af_extra = false;
                bool interd_sead = false;
                bool interd_escort = false;
                int escort_ships = 4;
                if (role == "AF_KILL") {
                    af_ww = unused_at(base, type) >= strike_ships + 2;
                    int available = unused_at(base, escort);
                    if (escort == type)
                        available -= strike_ships + (af_ww ? 2 : 0);
                    escort_ships = std::clamp(std::min(4, available), 0, 4);
                    af_extra = escort_ships > 0 && serial % 4 == 0;
                } else if (role == "INTERD") {
                    const bool port = target != nullptr &&
                                      !is_airfield_type(target->type);
                    interd_sead = port &&
                                  unused_at(base, type) >= strike_ships + 2;
                    int available = unused_at(base, escort);
                    if (escort == type) available -= strike_ships;
                    escort_ships = std::clamp(std::min(4, available), 0, 4);
                    interd_escort = !interd_sead && escort_ships > 0 &&
                                    serial % 4 == 2;
                    if (interd_sead) escort_ships = 0;
                } else if (role == "SEAD") {
                    int available = unused_at(base, escort);
                    if (escort == type) available -= strike_ships + 4;
                    escort_ships = std::clamp(std::min(4, available), 1, 4);
                }

                int flight_count = 1;
                if (support) flight_count = 2;
                else if (role == "AF_KILL")
                    flight_count = 1 + (af_ww ? 1 : 0) + (af_extra ? 1 : 0);
                else if (role == "INTERD")
                    flight_count = 1 + (interd_sead ? 1 : 0) +
                                   (interd_escort ? 1 : 0);
                else if (role == "SEAD") flight_count = 3;
                emit.addv("MISSION", {"PLANE", req.side, role, "OK",
                                      Number(base->index), Number(id),
                                      Number(flight_count)});

                // One corridor per package, shared by every flight in it.
                std::vector<std::array<int, 4>> package_route;
                const std::vector<std::array<int, 4>>* pkg_route = nullptr;
                if (target != nullptr) {
                    package_route = build_strike_route(
                        *base, *target, tanker_bases.count(base->index) != 0,
                        nav_points, rng);
                    pkg_route = &package_route;
                }

                const int wave = serial;
                if (support) {
                    const char* fr = role == "AWACS_PATROL"   ? "AWACS"
                                     : role == "JSTARS_PATROL" ? "JSTAR"
                                                               : "REFUEL";
                    emit_flight(base, type.c_str(), fr, 1, nullptr, nullptr, wave, true, id);
                    emit_flight(base, escort.c_str(), "ESCORT", escort_ships,
                                nullptr, nullptr, wave + 2, true, id);
                    out->aircraft += 1 + escort_ships;
                } else if (role == "AF_KILL") {
                    emit_flight(base, type.c_str(), "STRIKE", strike_ships, target, pkg_route, wave, true, id);
                    out->aircraft += strike_ships;
                    if (af_ww) {
                        emit_flight(base, type.c_str(), "WW", 2, target, pkg_route, wave + 2, true, id);
                        out->aircraft += 2;
                    }
                    if (af_extra) {
                        emit_flight(base, escort.c_str(), "ESCORT", escort_ships,
                                    target, pkg_route, wave + 4, true, id);
                        out->aircraft += escort_ships;
                    }
                } else if (role == "INTERD") {
                    emit_flight(base, type.c_str(), "STRIKE", strike_ships, target, pkg_route, wave, true, id);
                    out->aircraft += strike_ships;
                    if (interd_sead) {
                        emit_flight(base, type.c_str(), "SEAD_STRIKE", 2, target, pkg_route, wave + 2, true, id);
                        out->aircraft += 2;
                    } else if (interd_escort) {
                        emit_flight(base, escort.c_str(), "ESCORT", escort_ships,
                                    target, pkg_route, wave + 2, true, id);
                        out->aircraft += escort_ships;
                    }
                } else if (role == "SEAD") {
                    emit_flight(base, type.c_str(), "STRIKE", strike_ships, target, pkg_route, wave, true, id);
                    emit_flight(base, type.c_str(), "SEAD_STRIKE", 4, target, pkg_route, wave + 2,
                                true, id);
                    emit_flight(base, escort.c_str(), "ESCORT", escort_ships,
                                target, pkg_route, wave + 4, false, id);
                    out->aircraft += 10 + escort_ships;
                } else {
                    emit_flight(base, type.c_str(), "INTERCEPT", req.ships, nullptr, nullptr,
                                wave, true, id);
                    out->aircraft += req.ships;
                }
            }
        }
    }

    const int plane_count = mission_id - gbased_count - ship_count;

    // NUM_MISSIONS is written once the totals are known.
    campaign.missions.lines[num_missions_line].args[0] = Number(mission_id);

    // --- bases -------------------------------------------------------------
    for (const auto& base : campaign.bases) {
        emit.addv("BASE", {base.side, base.type, "\"" + base.name + "\"",
                           Number(base.index), Number(base.value), "0.000000",
                           base.port ? "255" : "0"});
        emit.addv("3DP", {Number(base.x), Number(base.y), "0", "0", "0", "0"});
        const auto& slots = schedule[base.index];
        struct Slot {
            int minute;
            const char* action;
            const std::string* role;
            int id;
            int wave;
        };
        std::vector<Slot> rows;
        // Four missions per wave; the next wave starts two minutes after the
        // longest flight of the previous one is back. Each flight of a package
        // gets its own row, two minutes behind the previous one.
        constexpr int kWave = 4;
        int wave_start = 0;
        int wave_duration = 0;
        int ship_start = 0;  // convoys launch on their own 45-minute cadence
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const int id = slots[i].first;
            const std::string& role = slots[i].second;
            const int within = static_cast<int>(i) % kWave;
            if (i != 0 && within == 0) {
                wave_start += wave_duration + 2;
                wave_duration = 0;
            }
            int flights = 1;
            if (const auto it = mission_flights.find(id);
                it != mission_flights.end() && it->second > 0)
                flights = it->second;
            if (role == "INTC") continue;  // intercepts launch on demand

            int duration = 252;
            if (role == "SEA_SUPPLY") {
                duration = 465;
            } else if (role == "AWACS_PATROL" || role == "REFUELLER_PATROL" ||
                       role == "JSTARS_PATROL") {
                duration = 460;
            } else if (role == "REFUELLER_KILL") {
                duration = 132;
            } else if (role == "AWACS_KILL") {
                duration = 294;
            } else if (role == "AF_KILL" || role == "INTERD" ||
                       role == "SEAD") {
                const int len = mission_route_len[id];
                const double a = role == "AF_KILL"   ? 0.095
                                 : role == "INTERD" ? 0.168
                                                    : 0.133;
                const double b = role == "AF_KILL"   ? 49.1
                                 : role == "INTERD" ? 43.5
                                                    : -16.8;
                duration = std::clamp(
                    static_cast<int>(std::lround(a * len + b)), 60, 300);
            }
            const int spacing = role == "SEA_SUPPLY" ? 15 : 2;
            for (int f = 0; f < flights; ++f) {
                const bool support_role =
                    role == "AWACS_PATROL" || role == "REFUELLER_PATROL" ||
                    role == "JSTARS_PATROL" || role == "REFUELLER_KILL" ||
                    role == "AWACS_KILL";
                // Support launches at the top of the turn, alongside the first
                // CAP wave; strike packages queue behind the CAP waves.
                const int takeoff =
                    role == "SEA_SUPPLY"
                        ? ship_start + f * spacing
                        : (support_role ? f * 2
                                        : wave_start + within * spacing + f * 2);
                // The timetable covers one eight-hour campaign turn; a
                // landing that would fall outside it is not written.
                if (takeoff < 8 * 60)
                    rows.push_back({takeoff, "TAKE_OFF", &role, id, f});
                const int landing = takeoff + duration;
                if (landing < 8 * 60)
                    rows.push_back({landing, "LANDING", &role, id, f});
            }
            if (role == "SEA_SUPPLY") {
                ship_start += 45;
            } else {
                const int span = duration + (flights - 1) * spacing;
                if (span > wave_duration) wave_duration = span;
            }
        }
        std::stable_sort(rows.begin(), rows.end(),
                         [](const Slot& a, const Slot& b) {
                             return a.minute < b.minute;
                         });
        emit.addv("TIMESLOTS", {Number(static_cast<int>(rows.size()))});
        for (const auto& row : rows) {
            const char* action = row.action;
            if (std::string(*row.role) == "SEA_SUPPLY" &&
                std::string(action) == "TAKE_OFF")
                action = "LAUNCH";
            emit.addv("TIMESLOT",
                      {"0", Number(row.minute / 60), Number(row.minute % 60),
                       action, *row.role, Number(row.id), Number(row.wave)});
        }
        emit.blank();
    }

    // --- flak --------------------------------------------------------------
    int flak = 0;
    for (const auto& area : campaign.areas) {
        for (const auto& site : area.sites) {
            if (site.weapon_class == 8 || site.weapon_class == 26) ++flak;
        }
    }
    emit.blank();
    emit.addv("FLAK", {Number(flak)});
    for (const auto& area : campaign.areas) {
        for (const auto& site : area.sites) {
            if (site.weapon_class != 8 && site.weapon_class != 26) continue;
            const int fx = area.x + site.off_x / 64;
            const int fy = area.y + site.off_y / 64;
            campaign.missions.values({Number(fx), Number(fy), Number(fx + 24),
                                      Number(fy - 24)});
        }
    }

    // The trailing WORLD_STATUS words carry the generator's RNG state.
    campaign.world_status[4] = static_cast<int>(rng.state() >> 16);
    campaign.world_status[5] = static_cast<int>(rng.state() & 0xFFFFu);
    campaign.outcome = evaluate_campaign(campaign);
    campaign.counts = count_missions(campaign.missions);
    campaign.counts.missions = mission_id;
    campaign.counts.gbased = gbased_count;
    campaign.counts.ship = ship_count;
    campaign.counts.plane = plane_count;
    *out = std::move(campaign);
    return true;
}

// ---------------------------------------------------------------------------
// CAMPAIGN.CG
// ---------------------------------------------------------------------------

bool write_campaign_file(const std::string& path, const Scenario& scenario,
                         const GeneratedCampaign& campaign, std::string* error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    auto area_value = [&](const std::string& name) {
        for (const auto& a : campaign.areas) {
            if (a.name == name) return a.value;
        }
        return 1000;
    };
    auto area_side = [&](const std::string& name) {
        for (const auto& a : campaign.areas) {
            if (a.name == name) return a.side;
        }
        return std::string("NATO");
    };

    out << "DIFFICULTY " << scenario.difficulty << "\r\n";
    out << "PERFORMANCE " << scenario.performance_a << ' '
        << scenario.performance_b << "\r\n\r\n";
    out << "WORLD_STATUS";
    for (int v : campaign.world_status) out << ' ' << v;
    out << "\r\n";

    const std::uint32_t stamp = static_cast<std::uint32_t>(std::time(nullptr));
    const std::string when = now_stamp();
    out << ';' << stamp << " Target list.  " << when << "\n\r\n\r\n\r\n";
    out << "NUM_TARGET_AREAS " << campaign.areas.size() << "\r\n";
    for (const auto& area : campaign.areas) {
        const ScenarioArea* src = scenario.find_area(area.name);
        const int a1 = src != nullptr ? src->airfield1 : area.airfield1;
        const int a2 = src != nullptr ? src->airfield2 : area.airfield2;
        out << "SSNAME \"" << area.name << "\" " << area.side << ' ' << area.value
            << ' ' << a1 << ' ' << a2 << " ALL\r\n";
    }
    out << ';' << stamp << " Mobile list.  " << when << "\n\r\n";
    const auto& mobiles =
        campaign.state_mobiles.empty() ? scenario.mobiles : campaign.state_mobiles;
    out << "NUM_MOBILES " << mobiles.size() << "\r\n\r\n";

    for (std::size_t i = 0; i < mobiles.size(); ++i) {
        const auto& m = mobiles[i];
        auto fields = m.fields;
        std::string status = m.status;
        if (i < campaign.mobiles.size()) {
            const auto& gm = campaign.mobiles[i];
            if (m.kind == "GB") {
                fields[8] = gm.mission >= 0 ? gm.mission : -1;
            } else if (m.kind == "PL") {
                fields[3] = gm.used ? 2 : 0;
                fields[8] = gm.mission >= 0 ? gm.mission : -1;
                fields[16] = gm.base >= 0 ? gm.base : -1;
                // Losses stay lost; the rest are either tasked or reserve.
                if (gm.state != 1 && gm.state != 2) {
                    status = gm.base >= 0 ? "OK" : "RESERVED";
                }
            } else if (m.kind == "SH") {
                fields[16] = gm.base >= 0 ? gm.base : -1;
                fields[8] = gm.mission >= 0 ? gm.mission : -1;
            }
        }
        out << "MOB " << m.kind << ' ' << m.type << ' ' << status << ' ' << m.side
            << " \"" << m.name << "\"";
        for (int v : fields) out << ' ' << v;
        out << "\r\n";
        if (m.has_pilot) {
            const auto& pl = m.pilot;
            out << "PIL \"" << pl.name << "\"";
            for (int v : pl.fields) out << ' ' << v;
            out << " 3DP " << (pl.pos ? pl.pos->x : 0) << ' '
                << (pl.pos ? pl.pos->y : 0) << ' ' << (pl.pos ? pl.pos->z : 0)
                << ' ' << pl.tail[0] << ' ' << pl.tail[1] << ' ' << pl.tail[2]
                << "\r\n";
        }
    }

    out << "\r\n\r\nNUM_ALLEGIANCES 6\r\n\r\n";
    for (const auto& row : scenario.allegiances) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i) out << ' ';
            out << row[i];
        }
        out << "\r\n";
    }
    (void)area_value;
    (void)area_side;
    return static_cast<bool>(out);
}

}  // namespace ef
