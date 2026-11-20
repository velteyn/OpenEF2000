// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "conflict.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <map>
#include <sstream>
#include <tuple>

namespace ef {
namespace {

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream os;
    os << in.rdbuf();
    std::string text = os.str();
    text.erase(std::remove(text.begin(), text.end(), '\r'), text.end());
    return text;
}

std::vector<std::string> tokenize(const std::string& text) {
    std::vector<std::string> tokens;
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }
        if (c == ';') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        if (c == '"') {
            std::size_t j = i + 1;
            while (j < text.size() && text[j] != '"' && text[j] != '\n') ++j;
            if (j < text.size() && text[j] == '"') ++j;
            tokens.push_back(text.substr(i, j - i));
            i = j;
            continue;
        }
        std::size_t j = i;
        while (j < text.size() && !std::isspace(static_cast<unsigned char>(text[j])) &&
               text[j] != ';') {
            ++j;
        }
        tokens.push_back(text.substr(i, j - i));
        i = j;
    }
    return tokens;
}

std::string unquote(const std::string& s) {
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
        return s.substr(1, s.size() - 2);
    }
    return s;
}

int to_int(const std::string& s) {
    return static_cast<int>(std::strtol(s.c_str(), nullptr, 10));
}

std::string now_stamp() {
    const std::time_t now = std::time(nullptr);
    char buf[64]{};
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    std::strftime(buf, sizeof(buf), "%a %b %e %H:%M:%S %Y", &tm);
    return buf;
}

}  // namespace

const ScenarioArea* Scenario::find_area(const std::string& name) const {
    for (const auto& a : areas) {
        if (a.name == name) return &a;
    }
    return nullptr;
}

bool load_conflict(const std::string& path, Scenario* out, std::string* error) {
    const std::string text = read_file(path);
    if (text.empty()) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    const auto tokens = tokenize(text);
    std::size_t p = 0;
    const auto next_int = [&](int fallback = 0) {
        if (p >= tokens.size()) return fallback;
        return to_int(tokens[p++]);
    };
    const auto next_token = [&]() -> std::string {
        if (p >= tokens.size()) return {};
        return tokens[p++];
    };

    Scenario c;
    ScenarioMobile* last_mobile = nullptr;
    while (p < tokens.size()) {
        const std::string head = tokens[p++];
        if (head == "DIFFICULTY") {
            c.difficulty = next_int(6);
        } else if (head == "PERFORMANCE") {
            c.performance_a = next_int(1);
            c.performance_b = next_int(50);
        } else if (head == "WORLD_STATUS") {
            for (int& v : c.world_status) v = next_int(0);
        } else if (head == "NUM_TARGET_AREAS" || head == "NUM_MOBILES" ||
                   head == "NUM_ALLEGIANCES") {
            const int n = next_int(0);
            if (head == "NUM_ALLEGIANCES") {
                for (int row = 0; row < n; ++row) {
                    std::vector<int> values;
                    for (int i = 0; i < 56; ++i) values.push_back(next_int(0));
                    c.allegiances.push_back(std::move(values));
                }
                for (int row = 0; row < 40; ++row) {
                    std::vector<int> values;
                    for (int i = 0; i < 40; ++i) values.push_back(next_int(0));
                    c.allegiances.push_back(std::move(values));
                }
            }
        } else if (head == "SSNAME") {
            ScenarioArea a;
            a.name = unquote(next_token());
            a.side = next_token();
            a.value = next_int(0);
            a.airfield1 = next_int(0);
            a.airfield2 = next_int(0);
            const std::string flag = next_token();
            a.cv = (flag == "CV");
            if (a.cv) {
                for (int i = 0; i < 30; ++i) a.cv_values.push_back(next_int(0));
            }
            c.areas.push_back(std::move(a));
        } else if (head == "MOB") {
            ScenarioMobile m;
            m.kind = next_token();
            m.type = next_token();
            m.status = next_token();
            m.side = next_token();
            m.name = unquote(next_token());
            for (int& v : m.fields) v = next_int(0);
            c.mobiles.push_back(std::move(m));
            last_mobile = &c.mobiles.back();
        } else if (head == "PIL") {
            ScenarioPilot pl;
            pl.name = unquote(next_token());
            for (int& v : pl.fields) v = next_int(0);
            const std::string tag = next_token();
            if (tag == "3DP") {
                Vec3 pos;
                pos.x = next_int(0);
                pos.y = next_int(0);
                pos.z = next_int(0);
                // The shipped PIL records carry three tail numbers; the fourth
                // slot is kept for compatibility with older notes.
                pl.tail[0] = next_int(0);
                pl.tail[1] = next_int(0);
                pl.tail[2] = next_int(0);
                pl.tail[3] = 0;
                pl.pos = pos;
                if (last_mobile != nullptr) {
                    last_mobile->pos = pos;
                    last_mobile->has_pilot = true;
                    last_mobile->pilot = pl;
                }
            }
            c.pilots.push_back(std::move(pl));
        } else {
            // Unknown directive: skip its numeric tail up to the next keyword.
            while (p < tokens.size()) {
                const std::string& t = tokens[p];
                if (!t.empty() && std::isalpha(static_cast<unsigned char>(t[0]))) break;
                ++p;
            }
        }
    }
    *out = std::move(c);
    return true;
}

bool save_conflict(const std::string& path, const Scenario& c, std::string* error) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    out << "DIFFICULTY " << c.difficulty << "\r\n";
    out << "PERFORMANCE " << c.performance_a << ' ' << c.performance_b << "\r\n";
    out << "\r\n";
    out << "WORLD_STATUS";
    for (int v : c.world_status) out << ' ' << v;
    out << "\r\n";
    out << ';' << static_cast<long long>(std::time(nullptr))
        << " Target list.  " << now_stamp() << "\r\n\r\n\r\n";
    out << "NUM_TARGET_AREAS " << c.areas.size() << "\r\n\r\n";
    for (const auto& a : c.areas) {
        out << "SSNAME \"" << a.name << "\" " << a.side << ' ' << a.value << ' '
            << a.airfield1 << ' ' << a.airfield2 << (a.cv ? " CV" : " ALL")
            << "\r\n";
        if (a.cv) {
            for (std::size_t i = 0; i < a.cv_values.size(); ++i) {
                if (i) out << ' ';
                out << a.cv_values[i];
            }
            out << "\r\n";
        }
    }
    out << "NUM_MOBILES " << c.mobiles.size() << "\r\n\r\n";
    for (const auto& m : c.mobiles) {
        out << "MOB " << m.kind << ' ' << m.type << ' ' << m.status << ' '
            << m.side << " \"" << m.name << "\"";
        for (int v : m.fields) out << ' ' << v;
        out << "\r\n";
        if (m.has_pilot) {
            const auto& pl = m.pilot;
            out << "PIL \"" << pl.name << "\"";
            for (int v : pl.fields) out << ' ' << v;
            out << " 3DP " << (pl.pos ? pl.pos->x : 0) << ' '
                << (pl.pos ? pl.pos->y : 0) << ' ' << (pl.pos ? pl.pos->z : 0);
            out << ' ' << pl.tail[0] << ' ' << pl.tail[1] << ' ' << pl.tail[2];
            out << "\r\n";
        }
    }
    out << "NUM_ALLEGIANCES " << 6 << "\r\n";
    for (const auto& row : c.allegiances) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i) out << ' ';
            out << row[i];
        }
        out << "\r\n";
    }
    return true;
}

std::vector<MobileGroup> group_mobiles(const Scenario& conflict, const char* kind) {
    std::vector<MobileGroup> groups;
    std::map<std::tuple<std::string, int, int>, std::size_t> index;
    for (const auto& m : conflict.mobiles) {
        if (m.kind != kind || !m.pos) continue;
        const auto key = std::make_tuple(m.side, m.pos->x, m.pos->y);
        auto it = index.find(key);
        if (it == index.end()) {
            MobileGroup g;
            g.side = m.side;
            g.pos = *m.pos;
            it = index.emplace(key, groups.size()).first;
            groups.push_back(std::move(g));
        }
        groups[it->second].mobiles.push_back(&m);
    }
    return groups;
}

}  // namespace ef
