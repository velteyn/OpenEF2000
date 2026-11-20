// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "campaigngen.h"

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

//! Target-list ordering. The generator writes the areas grouped by
//! installation class in this order, preserving table order inside a group.
int area_priority(int type) {
    if (is_airfield_type(type)) return 0;
    if (type >= 90 && type <= 95) return 1;
    switch (type) {
        case 205: return 2;
        case 206: return 3;
        case 207: return 4;
        case 204: return 5;
        case 79: return 6;
        case 127: return 7;
        case 142: return 8;
        case 143: return 9;
        case 159: return 10;
        case 101: return 11;
        case 106: return 12;
        case 107: return 13;
        case 108: return 14;
        case 109: return 15;
        case 110: return 16;
        case 111: return 17;
        case 96: return 18;
        case 97: return 19;
        case 98: return 20;
        case 99: return 21;
        case 100: return 22;
        case 192: return 23;
        case 193: return 24;
        case 194: return 25;
        case 195: return 26;
        case 63: return 27;
        default: return 28;
    }
}

//! Number of defence slots per installation class. Airfields carry the full
//! four-slot ring, the larger installations three, and outlying sites one.
int site_count_for_type(int type) {
    if (is_airfield_type(type)) return 4;
    if (type == 127 || type == 204) return 4;
    if (type == 142 || type == 143) return 1;
    if (type >= 96 && type <= 143) return 3;
    return 1;
}
bool is_port_type(int type) {
    return (type >= 90 && type <= 95) || (type >= 205 && type <= 207);
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

//! Parses one table row: `x  y  alt  model.sss  "Name"  ;type`.
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
    campaign.world_status = {settings.world_status[0], settings.world_status[1],
                             settings.world_status[2], settings.world_status[3],
                             settings.clock[0], settings.clock[1]};

    const auto current = index_areas(scenario);
    const auto prev = previous != nullptr ? index_areas(*previous)
                                          : std::map<std::string, const ScenarioArea*>{};

    // 1. Areas. The generator walks the fixed table, not the scenario list.
    campaign.areas.reserve(table.size());
    for (const auto& row : table) {
        GenArea area;
        area.name = row.name;
        area.x = row.x;
        area.y = row.y;
        area.type = row.type;
        if (auto it = prev.find(row.name); it != prev.end()) {
            area.side = it->second->side;
            area.airfield1 = it->second->airfield1;
            area.airfield2 = it->second->airfield2;
        } else if (auto it = current.find(row.name); it != current.end()) {
            area.side = it->second->side;
            area.airfield1 = it->second->airfield1;
            area.airfield2 = it->second->airfield2;
        }
        campaign.areas.push_back(std::move(area));
    }
    std::stable_sort(campaign.areas.begin(), campaign.areas.end(),
                     [](const GenArea& a, const GenArea& b) {
                         const int pa = area_priority(a.type);
                         const int pb = area_priority(b.type);
                         return pa < pb;
                     });
    for (auto& area : campaign.areas) {
        if (area.side.empty()) {
            area.side = inherit_side(area.name, area.x, area.y, campaign.areas);
        }
        if (area.airfield1 == 0) {
            area.airfield1 = 30;
            area.airfield2 = 30;
        }
    }

    // 2. Target values: thousands from the class draw plus the site offset.
    for (auto& area : campaign.areas) {
        const int thousands = (rng.range(5) + 1) * 1000;
        const int site = rng.range(14);
        area.value = std::max(1, thousands - site);
        area.map_cell = area.x / 40 + (area.y / 40) * 10;
    }

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

    // 4. Defence sites around every base area.
    for (auto& area : campaign.areas) {
        // The slot count follows the installation class: airfields carry the
        // full four-slot ring (Vulcan, Roland, Patriot, Rapier -- or SA-6,
        // SA-11, SA-11, ZSU), larger installations three, outlying sites one.
        const int count = site_count_for_type(area.type);
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

    // 5. Mobile index. Ground mobiles feed the air-defence pool; air mobiles
    //    the flight tasking; ships the convoys.
    campaign.mobiles.reserve(scenario.mobiles.size());
    for (std::size_t i = 0; i < scenario.mobiles.size(); ++i) {
        const auto& m = scenario.mobiles[i];
        GenMobile gm;
        gm.source = i;
        gm.side_code = side_code(m.side);
        gm.state = 0;
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

    // Link each ground mobile to the nearest same-side area whose defence
    // ring covers it. The ring radius is about twelve map units, so mobiles
    // outside every ring stay unassigned and never appear in a mission.
    constexpr long kLinkRadius = 12;
    for (auto& gm : campaign.mobiles) {
        if (gm.class_id < 0) continue;
        long best = -1;
        int best_area = -1;
        for (std::size_t a = 0; a < campaign.areas.size(); ++a) {
            const auto& area = campaign.areas[a];
            if (area.sites.empty()) continue;
            if (side_code(area.side) != gm.side_code) continue;
            const long dx = area.x - gm.x, dy = area.y - gm.y;
            const long d = dx * dx + dy * dy;
            if (d > kLinkRadius * kLinkRadius) continue;
            if (best < 0 || d < best) {
                best = d;
                best_area = static_cast<int>(a);
            }
        }
        gm.area = best_area;
    }

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
              {Number(settings.world_status[0]), Number(settings.world_status[1]),
               Number(settings.world_status[2]), Number(settings.world_status[3])});

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
                if (gm.used) continue;
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

    // 7. Sea tasking: one convoy per ship group, scaled by group size.
    {
        std::map<std::pair<std::string, std::pair<int, int>>, int> groups;
        for (const auto& m : scenario.mobiles) {
            if (m.kind != "SH" || !m.pos) continue;
            ++groups[{m.side, {m.pos->x, m.pos->y}}];
        }
        for (const auto& [key, ships] : groups) {
            const int convoys = std::max(1, (ships + 15) / 30);
            const int side = side_code(key.first);
            const bool east = !is_western(side);
            const char* hull = east ? "UDALOY_CLASS" : "TYPE42_CLASS";
            for (int i = 0; i < convoys; ++i) {
                const int id = mission_id++;
                emit.addv("MISSION", {"SHIP", key.first, "SEA_SUPPLY", "OK", "0",
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
    //! Missions per base, used for the 24-hour TIMESLOT schedule.
    std::map<int, std::vector<std::pair<int, std::string>>> schedule;
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

        auto aircraft_for = [](const std::string& side) -> const char* {
            if (side == "SWEDISH") return "JAS39";
            if (side == "RUSSIAN") return "SU35";
            if (side == "FINNISH") return "F18";
            if (side == "BALTIC") return "SU27";
            return "EFA";
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

        std::vector<const GenArea*> strike_targets;
        for (const auto& area : campaign.areas) {
            if (!is_airfield_type(area.type) && !is_port_type(area.type)) continue;
            if (area.side == "NATO" || area.side == "SWEDISH") continue;
            strike_targets.push_back(&area);
        }
        std::sort(strike_targets.begin(), strike_targets.end(),
                  [](const GenArea* a, const GenArea* b) { return a->name < b->name; });
        std::size_t target_index = 0;

        for (const auto& req : kRoles) {
            auto it = bases_by_side.find(req.side);
            if (it == bases_by_side.end() || it->second.empty()) continue;
            const auto& list = it->second;
            for (int n = 0; n < req.count; ++n) {
                std::size_t& cursor = next_base[req.side];
                const GenBase* base = list[cursor % list.size()];
                ++cursor;
                const int id = mission_id++;
                ++out->aircraft;
                const std::string role = req.role;
                schedule[base->index].emplace_back(id, role);
                const bool strike_role = role == "AF_KILL" || role == "INTERD" ||
                                         role == "SEAD";
                emit.addv("MISSION", {"PLANE", req.side, role, "OK", "0",
                                      Number(id), "1"});

                const char* flight_role = "INTERCEPT";
                if (strike_role) flight_role = role == "SEAD" ? "SEAD_STRIKE" : "STRIKE";
                else if (role == "AWACS_PATROL") flight_role = "AWACS";
                else if (role == "JSTARS_PATROL") flight_role = "JSTAR";
                else if (role == "REFUELLER_PATROL") flight_role = "REFUEL";

                const char* type = aircraft_for(req.side);
                if (role == "AWACS_PATROL")
                    type = std::string(req.side) == "RUSSIAN" ? "A50" : "E767";
                else if (role == "JSTARS_PATROL") type = "J767";
                else if (role == "REFUELLER_PATROL")
                    type = std::string(req.side) == "RUSSIAN" ? "IL78M" : "R767";

                emit.addv("FLIGHT", {Number(req.ships), type, flight_role, "OK"});

                static const char* const kEfa[] = {
                    "ASRAAM", "ASRAAM", "ASRAAM", "ASRAAM", "AMRAAM", "AMRAAM",
                    "AMRAAM", "AMRAAM", "SS225",  "AMRAAM", "SS225",  "AMRAAM",
                    "CENTRE_FUEL"};
                static const char* const kJas[] = {"ASRAAM", "ASRAAM", "AMRAAM",
                                                   "AMRAAM", "SS225",  "SS225"};
                static const char* const kSu[] = {"APHID", "APHID", "ALAMO", "ALAMO",
                                                  "ARCHER", "ARCHER", "ARCHER",
                                                  "ARCHER"};
                static const char* const kSeaBomb[] = {"MK_82", "MK_82", "MK_83",
                                                       "MK_83"};
                const char* const* loadout = nullptr;
                int loadout_len = 0;
                if (std::string(type) == "EFA") {
                    loadout = kEfa;
                    loadout_len = 13;
                } else if (std::string(type) == "JAS39") {
                    loadout = kJas;
                    loadout_len = 6;
                } else if (std::string(type) == "SU35") {
                    loadout = kSu;
                    loadout_len = 8;
                }
                for (int p = 0; p < 20; ++p) {
                    std::string name = "NO_WEAPON";
                    if (strike_role && p >= 2 && p < 6)
                        name = kSeaBomb[p - 2];
                    else if (!strike_role && loadout != nullptr && p < loadout_len)
                        name = loadout[p];
                    emit.addv("PYLON", {Number(p), name});
                }
                emit.addv("3DP", {Number(base->x), Number(base->y), "0", "0", "0",
                                  "0"});
                if (strike_role) {
                    const GenArea* target = nullptr;
                    if (!strike_targets.empty())
                        target = strike_targets[target_index++ % strike_targets.size()];
                    emit.addv("WP_ROUTE", {"4", "0", "0"});
                    emit.addv("WAYPOINT",
                              {"0", Number(base->x), Number(base->y), "0", "0", "0",
                               "TAKE_OFF", "0", "0", "0"});
                    if (target != nullptr) {
                        emit.addv("WAYPOINT",
                                  {"1", Number(target->x), Number(target->y), "0",
                                   "0", "0", "INGRESS", "0", "0", "0"});
                        emit.addv("WAYPOINT",
                                  {"2", Number(target->x), Number(target->y), "0",
                                   "0", "0", "STRIKE_TARGET", "0", "0", "0"});
                    }
                    emit.addv("WAYPOINT",
                              {"3", Number(base->x), Number(base->y), "0", "0", "0",
                               "LANDING", "0", "0", "0"});
                    emit.addv("TARGET", {"MISSION", "0", "GROUP", "0"});
                    if (target != nullptr) {
                        emit.addv("TARGET_AREA",
                                  {base_type_for(target->type),
                                   "\"" + target->name + "\""});
                        emit.addv("3DP", {Number(target->x), Number(target->y), "0",
                                          "0", "0", "0"});
                        emit.addv("LEAD", {"TARGET", "HANGAR8", "\"\"", "0", "0",
                                           "0", "0", "0", "0"});
                        emit.addv("ATTACK_PATTERN", {"LAYDOWN_ATTACK", "1"});
                        emit.addv("ATTACK_ROUTE", {"2"});
                        emit.addv("ATTACK_NODE",
                                  {"0", "-9216", "700", "0", "0", "0", "PASS"});
                        emit.addv("ATTACK_NODE",
                                  {"2048", "2048", "700", "0", "0", "0", "STRIKE"});
                    }
                } else {
                    emit.addv("WP_ROUTE", {"8", "0", "0"});
                    emit.addv("WAYPOINT",
                              {"0", Number(base->x), Number(base->y), "0", "0", "0",
                               "TAKE_OFF", "0", "0", "0"});
                    for (int w = 1; w <= 5; ++w) {
                        const int dx = rng.between(-16, 16);
                        const int dy = rng.between(-16, 16);
                        const int alt = role == "CAP" || role == "INTC" ? 90 : 250;
                        emit.addv("WAYPOINT",
                                  {Number(w), Number(base->x + dx),
                                   Number(base->y + dy), Number(alt),
                                   Number(rng.between(0, 2047)),
                                   Number(rng.between(0, 2047)), "PATROL", "0", "0",
                                   "0"});
                    }
                    emit.addv("WAYPOINT",
                              {"6", Number(base->x), Number(base->y), "90", "0", "0",
                               "PATROL_LOOP", "7", "0", "0"});
                    emit.addv("WAYPOINT",
                              {"7", Number(base->x), Number(base->y), "0", "0", "0",
                               "LANDING", "7", "0", "0"});
                    emit.addv("TARGET", {"MISSION", "0", "GROUP", "0"});
                }
                for (int p = 0; p < req.ships && !pilots.empty(); ++p) {
                    const auto* pilot = pilots[next_pilot++ % pilots.size()];
                    std::vector<std::string> fields{"\"" + pilot->name + "\""};
                    for (int f = 0; f < 11; ++f) fields.push_back(Number(pilot->fields[f]));
                    emit.addv("PIL", fields);
                    emit.blank();
                }
                emit.blank();
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
        emit.addv("TIMESLOTS", {Number(static_cast<int>(slots.size()) * 2)});
        for (std::size_t i = 0; i < slots.size(); ++i) {
            const int id = slots[i].first;
            const std::string& role = slots[i].second;
            const int takeoff = static_cast<int>(i) * 2;
            int duration = 252;
            if (role == "AWACS_PATROL" || role == "REFUELLER_PATROL" ||
                role == "JSTARS_PATROL")
                duration = 460;
            else if (role == "AF_KILL" || role == "INTERD" || role == "SEAD")
                duration = 200;
            const int landing = std::min(takeoff + duration, 24 * 60 - 1);
            emit.addv("TIMESLOT", {"0", Number(takeoff / 60), Number(takeoff % 60),
                                   "TAKE_OFF", role, Number(id), "0"});
            emit.addv("TIMESLOT", {"0", Number(landing / 60), Number(landing % 60),
                                   "LANDING", role, Number(id), "0"});
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
            campaign.missions.values({Number(area.x), Number(area.y),
                                      Number(area.x + 24), Number(area.y - 24)});
        }
    }

    // The trailing WORLD_STATUS words carry the generator's RNG state.
    campaign.world_status[4] = static_cast<int>(rng.state() >> 16);
    campaign.world_status[5] = static_cast<int>(rng.state() & 0xFFFFu);
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
    out << "NUM_MOBILES " << scenario.mobiles.size() << "\r\n\r\n";

    for (std::size_t i = 0; i < scenario.mobiles.size(); ++i) {
        const auto& m = scenario.mobiles[i];
        auto fields = m.fields;
        if (i < campaign.mobiles.size()) {
            const auto& gm = campaign.mobiles[i];
            if (m.kind == "GB") {
                fields[8] = gm.mission >= 0 ? gm.mission : -1;
            } else if (m.kind == "PL") {
                fields[3] = gm.used ? 2 : 0;
                fields[8] = gm.mission >= 0 ? gm.mission : -1;
                fields[16] = gm.base >= 0 ? gm.base : -1;
            } else if (m.kind == "SH") {
                fields[16] = gm.base >= 0 ? gm.base : -1;
                fields[8] = gm.mission >= 0 ? gm.mission : -1;
            }
        }
        out << "MOB " << m.kind << ' ' << m.type << ' ' << m.status << ' ' << m.side
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
