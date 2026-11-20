// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "parser.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace ef {
namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> lines;
    std::string cur;
    for (char c : text) {
        if (c == '\n') {
            if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            lines.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) lines.push_back(cur);
    return lines;
}

std::vector<int> ints(const std::vector<std::string>& t, std::size_t start) {
    std::vector<int> out;
    for (std::size_t i = start; i < t.size(); ++i) out.push_back(to_int(t[i]));
    return out;
}

Vec3 vec3(const std::vector<std::string>& t, std::size_t start) {
    Vec3 p;
    if (start + 0 < t.size()) p.x = to_int(t[start + 0]);
    if (start + 1 < t.size()) p.y = to_int(t[start + 1]);
    if (start + 2 < t.size()) p.z = to_int(t[start + 2]);
    return p;
}

bool is_side(const std::string& s) {
    return s == "NATO" || s == "RUSSIAN" || s == "SWEDISH" ||
           s == "FINNISH" || s == "BALTIC" || s == "US" || s == "UK" ||
           s == "NORWEGIAN" || s == "NO_ALLEGIANCE";
}

}  // namespace

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    while (i < line.size()) {
        char c = line[i];
        if (c == ' ' || c == '\t' || c == '\r') {
            ++i;
            continue;
        }
        if (c == ';') break;
        if (c == '"') {
            std::size_t j = line.find('"', i + 1);
            if (j == std::string::npos) j = line.size();
            out.push_back(line.substr(i + 1, j - i - 1));
            i = j + 1;
        } else {
            std::size_t j = i;
            while (j < line.size() && line[j] != ' ' && line[j] != '\t' &&
                   line[j] != '\r' && line[j] != ';') {
                ++j;
            }
            out.push_back(line.substr(i, j - i));
            i = j;
        }
    }
    return out;
}

int to_int(const std::string& s) {
    if (s.empty()) return 0;
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (end == s.c_str()) return 0;
    return static_cast<int>(v);
}

bool parse_mission(const std::string& path, Dataset& ds) {
    std::string text = read_file(path);
    if (text.empty()) return false;
    ds.mission_path = path;

    enum class Last { None, Mission, Group, Route, Target, Area, AttackPattern,
                      AttackRoute, Flight, Base };
    Last last = Last::None;
    int mi = -1, gi = -1, ri = -1, ti = -1, ai = -1, api = -1, ari = -1, fi = -1, bi = -1;
    bool ap_on_area = false;

    auto pattern = [&]() -> AttackPattern* {
        if (mi < 0 || ti < 0 || api < 0) return nullptr;
        auto& target = ds.missions[mi].targets[ti];
        if (ap_on_area) {
            if (ai < 0 || ai >= static_cast<int>(target.areas.size())) return nullptr;
            auto& v = target.areas[ai].patterns;
            return api < static_cast<int>(v.size()) ? &v[api] : nullptr;
        }
        return api < static_cast<int>(target.patterns.size()) ? &target.patterns[api] : nullptr;
    };

    auto set_pos = [&](Vec3 p) {
        if (last == Last::Mission && mi >= 0) ds.missions[mi].pos = p;
        else if (last == Last::Group && mi >= 0 && gi >= 0) ds.missions[mi].groups[gi].pos = p;
        else if (last == Last::Target && mi >= 0 && ti >= 0) ds.missions[mi].targets[ti].pos = p;
        else if (last == Last::Area && mi >= 0 && ti >= 0 && ai >= 0)
            ds.missions[mi].targets[ti].areas[ai].pos = p;
        else if (last == Last::Flight && mi >= 0 && fi >= 0) ds.missions[mi].flights[fi].pos = p;
        else if (last == Last::Base && bi >= 0) ds.bases[bi].pos = p;
    };

    for (const std::string& line : split_lines(text)) {
        auto t = tokenize(line);
        if (t.empty()) continue;
        std::string kw = t[0];
        if (kw == "3D_POSITION" || kw == "POSITION3D") kw = "3DP";

        if (kw == "NUM_MISSIONS") {
            if (t.size() > 1) ds.num_missions_declared = to_int(t[1]);
        } else if (kw == "WORLD_STATUS") {
            ds.world_status = ints(t, 1);
        } else if (kw == "MISSION" && t.size() >= 4) {
            Mission m;
            if (is_side(t[1])) {
                m.type = "PLANE";
                m.side = t[1]; m.role = t[2]; m.status = t[3];
                m.values = ints(t, 4);
            } else {
                if (t.size() < 5) continue;
                m.type = t[1]; m.side = t[2]; m.role = t[3]; m.status = t[4];
                m.values = ints(t, 5);
            }
            ds.missions.push_back(std::move(m));
            mi = static_cast<int>(ds.missions.size()) - 1;
            gi = ri = ti = ai = api = ari = fi = -1;
            last = Last::Mission;
        } else if (kw == "GROUP" && mi >= 0 && t.size() >= 5) {
            Group g;
            g.count = to_int(t[1]); g.type = t[2]; g.role = t[3]; g.status = t[4];
            ds.missions[mi].groups.push_back(std::move(g));
            gi = static_cast<int>(ds.missions[mi].groups.size()) - 1;
            ri = ti = ai = api = ari = fi = -1;
            last = Last::Group;
        } else if (kw == "3DP") {
            set_pos(vec3(t, 1));
        } else if (kw == "WP_ROUTE" && mi >= 0) {
            Route r;
            r.count = t.size() > 1 ? to_int(t[1]) : 0;
            ds.missions[mi].routes.push_back(std::move(r));
            ri = static_cast<int>(ds.missions[mi].routes.size()) - 1;
            last = Last::Route;
        } else if (kw == "WAYPOINT" && mi >= 0 && ri >= 0 && t.size() >= 7) {
            Waypoint w;
            w.index = to_int(t[1]);
            w.p = vec3(t, 2);
            w.a = to_int(t[5]);
            w.b = to_int(t[6]);
            if (t.size() > 7) w.action = t[7];
            ds.missions[mi].routes[ri].waypoints.push_back(std::move(w));
        } else if (kw == "TARGET" && mi >= 0) {
            Target tg;
            if (t.size() >= 5) {
                tg.mission = to_int(t[2]);
                tg.group = to_int(t[4]);
            }
            ds.missions[mi].targets.push_back(std::move(tg));
            ti = static_cast<int>(ds.missions[mi].targets.size()) - 1;
            ai = api = ari = -1;
            last = Last::Target;
        } else if (kw == "TARGET_AREA" && mi >= 0 && ti >= 0 && t.size() >= 3) {
            TargetArea a;
            a.type = t[1]; a.name = t[2];
            ds.missions[mi].targets[ti].areas.push_back(std::move(a));
            ai = static_cast<int>(ds.missions[mi].targets[ti].areas.size()) - 1;
            api = ari = -1;
            last = Last::Area;
        } else if ((kw == "LEAD" || kw == "TWO" || kw == "THREE" ||
                    kw == "FOUR" || kw == "FIVE" || kw == "SIX") &&
                   mi >= 0 && ti >= 0 && ai >= 0 && t.size() >= 4) {
            TargetElement e;
            e.slot = kw; e.kind = t[1]; e.type = t[2]; e.name = t[3];
            ds.missions[mi].targets[ti].areas[ai].elements.push_back(std::move(e));
        } else if (kw == "ATTACK_PATTERN" && mi >= 0 && ti >= 0 && t.size() >= 3) {
            AttackPattern p;
            p.type = t[1]; p.count = to_int(t[2]);
            ap_on_area = (ai >= 0);
            auto& target = ds.missions[mi].targets[ti];
            auto& v = ap_on_area ? target.areas[ai].patterns : target.patterns;
            v.push_back(std::move(p));
            api = static_cast<int>(v.size()) - 1;
            ari = -1;
            last = Last::AttackPattern;
        } else if (kw == "ATTACK_ROUTE") {
            if (auto* p = pattern(); p != nullptr) {
                AttackRoute r;
                r.count = t.size() > 1 ? to_int(t[1]) : 0;
                p->routes.push_back(std::move(r));
                ari = static_cast<int>(p->routes.size()) - 1;
                last = Last::AttackRoute;
            }
        } else if (kw == "ATTACK_NODE") {
            if (auto* p = pattern(); p != nullptr && ari >= 0 &&
                ari < static_cast<int>(p->routes.size())) {
                AttackNode n;
                n.p = vec3(t, 1);
                if (t.size() > 7) n.action = t[7];
                p->routes[ari].nodes.push_back(std::move(n));
            }
        } else if (kw == "FLIGHT" && mi >= 0 && t.size() >= 5) {
            Flight f;
            f.count = to_int(t[1]); f.aircraft = t[2]; f.role = t[3]; f.status = t[4];
            ds.missions[mi].flights.push_back(std::move(f));
            fi = static_cast<int>(ds.missions[mi].flights.size()) - 1;
            last = Last::Flight;
        } else if (kw == "PYLON" && mi >= 0 && fi >= 0 && t.size() >= 3) {
            ds.missions[mi].flights[fi].pylons.emplace_back(to_int(t[1]), t[2]);
        } else if (kw == "PIL" && mi >= 0) {
            ++ds.missions[mi].pilot_slots;
        } else if (kw == "BASE") {
            Base b;
            if (t.size() >= 4) { b.side = t[1]; b.type = t[2]; b.name = t[3]; }
            ds.bases.push_back(std::move(b));
            bi = static_cast<int>(ds.bases.size()) - 1;
            last = Last::Base;
        } else if (kw == "TIMESLOTS" || kw == "TIMESLOT") {
            // not needed for the strategic prototype
        }
    }
    return true;
}

bool parse_navig(const std::string& path, Dataset& ds) {
    std::string text = read_file(path);
    if (text.empty()) return false;
    ds.navig_path = path;
    int ni = -1, nri = -1;

    for (const std::string& line : split_lines(text)) {
        auto t = tokenize(line);
        if (t.empty()) continue;
        std::string kw = t[0];
        if (kw == "BASE_NAVIGATION") kw = "BNA";
        else if (kw == "3D_POSITION") kw = "3DP";
        else if (kw == "NAVIG_CORRIDOR") kw = "NCO";
        else if (kw == "NAVIG_ROUTE") kw = "NRO";
        else if (kw == "WAYPOINT") kw = "WP";

        if (kw == "NUM_BASE_NAVIGATIONS") {
            if (t.size() > 1) ds.num_bna_declared = to_int(t[1]);
        } else if (kw == "BNA" && t.size() >= 3) {
            Bna b;
            b.type = t[1]; b.base = to_int(t[2]);
            ds.bnas.push_back(std::move(b));
            ni = static_cast<int>(ds.bnas.size()) - 1;
            nri = -1;
        } else if (kw == "3DP" && ni >= 0) {
            ds.bnas[ni].pos = vec3(t, 1);
        } else if (kw == "NCO" && ni >= 0) {
            Route r;
            ds.bnas[ni].routes.push_back(std::move(r));
            nri = static_cast<int>(ds.bnas[ni].routes.size()) - 1;
        } else if (kw == "NRO" && ni >= 0 && nri >= 0) {
            if (t.size() > 1) ds.bnas[ni].routes[nri].count = to_int(t[1]);
        } else if (kw == "WP" && ni >= 0 && nri >= 0 && t.size() >= 8) {
            Waypoint w;
            w.index = to_int(t[1]);
            w.p = vec3(t, 2);
            w.a = to_int(t[5]); w.b = to_int(t[6]);
            w.action = t[7];
            ds.bnas[ni].routes[nri].waypoints.push_back(std::move(w));
        }
    }
    return true;
}

bool parse_conflict(const std::string& path, Dataset& ds) {
    std::string text = read_file(path);
    if (text.empty()) return false;
    ds.conflict_path = path;
    for (const std::string& line : split_lines(text)) {
        auto t = tokenize(line);
        if (t.empty()) continue;
        if (t[0] == "NUM_TARGET_AREAS" && t.size() > 1) {
            ds.num_areas_declared = to_int(t[1]);
        } else if (t[0] == "SSNAME" && t.size() >= 6) {
            ConflictArea a;
            a.name = t[1]; a.side = t[2]; a.value = to_int(t[3]);
            if (t.size() > 6) a.flag = t[6];
            ds.areas.push_back(std::move(a));
        }
    }
    return true;
}

bool parse_campaign_support(const std::string& path, Dataset& ds) {
    std::ifstream file(path, std::ios::binary);
    if (!file) return false;
    std::string line;
    while (std::getline(file, line)) {
        auto t = tokenize(line);
        if (t.size() < 5 || t[0] != "MOB" || t[1] != "PL") continue;
        const std::string& type = t[2];
        const std::string& status = t[3];
        const std::string& side = t[4];
        if (status != "OK") continue;
        auto& s = ds.support[side];
        if (type == "E767" || type == "A50") s.awacs = true;
        else if (type == "J767") s.jstars = true;
        else if (type == "R767" || type == "IL78M") s.tanker = true;
    }
    return true;
}

Dataset load_dataset(const std::string& dir) {
    Dataset ds;
    ds.dir = dir;
    namespace fs = std::filesystem;
    auto p = [&](const char* name) { return (fs::path(dir) / name).string(); };
    if (fs::exists(p("MISSION.CFG"))) parse_mission(p("MISSION.CFG"), ds);
    if (fs::exists(p("NAVIG.NAV"))) parse_navig(p("NAVIG.NAV"), ds);
    if (fs::exists(p("CONFLICT.CG"))) parse_conflict(p("CONFLICT.CG"), ds);
    if (fs::exists(p("CAMPAIGN.CG"))) parse_campaign_support(p("CAMPAIGN.CG"), ds);
    return ds;
}

}  // namespace ef
