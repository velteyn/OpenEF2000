// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "stats.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <tuple>

namespace ef {
namespace {

std::string upper(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string unquote(std::string s) {
    if (!s.empty() && s.front() == '"') s.erase(s.begin());
    if (!s.empty() && s.back() == '"') s.pop_back();
    return s;
}

std::vector<ForceCategory> make_rows(std::initializer_list<const char*> names) {
    std::vector<ForceCategory> rows;
    rows.reserve(names.size());
    for (const char* name : names) rows.push_back(ForceCategory{name, {}});
    return rows;
}

void add_row(std::vector<ForceCategory>& rows, const std::string& name,
             int side, int count) {
    if (name.empty()) return;
    for (auto& row : rows) {
        if (row.name == name) {
            row.counts[static_cast<std::size_t>(side)] += count;
            return;
        }
    }
}

std::string classify_aircraft(const std::string& type) {
    const std::string t = upper(type);
    const auto has = [&](const char* needle) {
        return t.find(needle) != std::string::npos;
    };
    if (has("A50") || has("E767") || has("E3") || has("AWACS") ||
        has("JSTARS")) {
        return "AWACs";
    }
    if (has("IL78") || has("J767") || has("KC") || has("TANKER")) {
        return "Refueller";
    }
    if (has("R767") || has("C130") || has("IL76") || has("C17") ||
        has("TRANSPORT") || t.rfind("AN", 0) == 0) {
        return "Transports";
    }
    if (has("F117") || has("TORNADO") || has("SU34") || has("B52") ||
        has("BOMBER") || t.rfind("TU", 0) == 0) {
        return "Bombers";
    }
    return "Fighters";
}

bool naval(const std::string& type) {
    const std::string t = upper(type);
    return t.find("SHIP") != std::string::npos ||
           t.find("TANKER") != std::string::npos ||
           t.find("TYPE42") != std::string::npos ||
           t.find("NEUTRASHIMY") != std::string::npos ||
           t.find("NAVAL") != std::string::npos ||
           t.find("VESSEL") != std::string::npos;
}

std::string classify_ground(const std::string& type, const std::string& role) {
    const std::string t = upper(type);
    const auto has = [&](const char* needle) {
        return t.find(needle) != std::string::npos;
    };
    if (has("TANKER") || has("SUPPLY_SHIP") || has("TRANSPORT")) {
        return "Transport Ships";
    }
    if (has("TYPE42") || has("NEUTRASHIMY") || has("BATTLE") ||
        has("DESTROYER") || has("FRIGATE")) {
        return "BattleShips";
    }
    if (has("PATRIOT") || has("SA_") || has("RAPIER") || has("ROLAND") ||
        has("VULCAN") || has("SHILKA") || has("ZSU") || has("BUK") ||
        has("KUB") || has("SAM") || has("AAA")) {
        return "AntiAircraft";
    }
    (void)role;
    return {};
}

std::string classify_structure(const std::string& type) {
    const std::string t = upper(type);
    const auto has = [&](const char* needle) {
        return t.find(needle) != std::string::npos;
    };
    if (has("EWR")) return "EWR Sites";
    if (has("CHOKE")) return "Choke Points";
    if (has("POL") || has("INDUSTRIAL")) return "Political Ind.";
    if (has("OIL")) return "Oilrigs";
    if (has("AIRFIELD")) return "Airfields";
    if (has("PORT")) return "Ports";
    if (has("DAM")) return "Dams";
    return {};
}

}  // namespace

ForceSide force_side(const std::string& side) {
    if (side == "NATO") return ForceSide::nato;
    if (side == "RUSSIAN") return ForceSide::enemy;
    return ForceSide::neutral;
}

const char* force_side_name(ForceSide side) {
    switch (side) {
        case ForceSide::nato: return "NATO";
        case ForceSide::neutral: return "NEUTRAL";
        case ForceSide::enemy: return "ENEMY";
    }
    return "";
}

int ForceStats::total() const {
    int sum = 0;
    for (const auto& s : sides) sum += s.total();
    return sum;
}

int category_value(const std::vector<ForceCategory>& rows, const std::string& name,
                   int side) {
    for (const auto& row : rows) {
        if (row.name == name) return row.counts[static_cast<std::size_t>(side)];
    }
    return 0;
}

ForceStats compute_force_stats(const Dataset& ds) {
    ForceStats stats;
    stats.aircraft =
        make_rows({"Fighters", "Bombers", "Transports", "Refueller", "AWACs"});
    stats.ground = make_rows({"AntiAircraft", "BattleShips", "Transport Ships"});
    stats.structures = make_rows({"EWR Sites", "Choke Points", "Political Ind.",
                                  "Oilrigs", "Airfields", "Ports", "Dams"});

    // Mission files repeat the same site across many records; count each
    // ground unit and structure once.
    std::set<std::tuple<int, int, std::string, int>> seen_ground;
    std::set<std::tuple<int, int, std::string, std::string, int>> seen_structure;

    for (const auto& m : ds.missions) {
        const int side = static_cast<int>(force_side(m.side));
        auto& side_force = stats.sides[static_cast<std::size_t>(side)];

        for (const auto& f : m.flights) {
            const int n = std::max(1, f.count);
            side_force.aircraft += n;
            add_row(stats.aircraft, classify_aircraft(f.aircraft), side, n);
        }

        for (const auto& g : m.groups) {
            const int n = std::max(1, g.count);
            if (g.pos) {
                const auto key =
                    std::make_tuple(g.pos->x, g.pos->y, g.type, side);
                if (!seen_ground.insert(key).second) continue;
            }
            if (naval(g.type)) side_force.ships += n;
            else side_force.ground += n;
            add_row(stats.ground, classify_ground(g.type, g.role), side, n);
        }

        for (const auto& t : m.targets) {
            for (const auto& a : t.areas) {
                if (a.pos) {
                    const auto key = std::make_tuple(a.pos->x, a.pos->y, a.type,
                                                     a.name, side);
                    if (!seen_structure.insert(key).second) continue;
                }
                side_force.structures += 1;
                add_row(stats.structures, classify_structure(a.type), side, 1);
            }
        }
    }
    return stats;
}

int mission_base_index(const Mission& mission) {
    if (mission.type != "PLANE" || mission.values.empty()) return -1;
    return mission.values.front();
}

int find_base_index(const Dataset& ds, const std::string& name) {
    const std::string target = unquote(name);
    for (int i = 0; i < static_cast<int>(ds.bases.size()); ++i) {
        if (unquote(ds.bases[static_cast<std::size_t>(i)].name) == target) {
            return i;
        }
    }
    return -1;
}

int base_efa_count(const Dataset& ds, int base_index, bool only_free) {
    if (base_index < 0) return 0;
    int total = 0;
    for (const auto& m : ds.missions) {
        if (mission_base_index(m) != base_index) continue;
        for (const auto& f : m.flights) {
            if (upper(f.aircraft).find("EFA") == std::string::npos) continue;
            if (only_free) {
                bool standby = false;
                for (const auto& r : m.routes) {
                    for (const auto& w : r.waypoints) {
                        if (w.action == "STANDBY") standby = true;
                    }
                }
                if (!standby) continue;
            }
            total += std::max(1, f.count);
        }
    }
    return total;
}

}  // namespace ef
