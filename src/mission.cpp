// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "mission.h"

#include <cctype>
#include <fstream>
#include <sstream>

namespace ef {
namespace {

bool take_cr(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
        return true;
    }
    return false;
}

bool is_blank(const std::string& line) {
    for (char c : line) {
        if (!std::isspace(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

}  // namespace

std::vector<std::string> MissionDocument::tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::size_t i = 0;
    const std::size_t n = line.size();
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
        if (i >= n) break;
        if (line[i] == '"') {
            std::size_t j = i + 1;
            while (j < n && line[j] != '"') ++j;
            if (j < n) ++j;
            out.push_back(line.substr(i, j - i));
            i = j;
            continue;
        }
        std::size_t j = i;
        while (j < n && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
        out.push_back(line.substr(i, j - i));
        i = j;
    }
    return out;
}

bool MissionDocument::load(const std::string& path, MissionDocument* out,
                           std::string* error) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (error) *error = "cannot read " + path;
        return false;
    }
    MissionDocument doc;
    std::string line;
    while (std::getline(in, line)) {
        const bool crlf = take_cr(line);
        if (is_blank(line)) {
            doc.blank();
            doc.lines.back().lf_only = !crlf;
            continue;
        }
        if (line[0] == ';') {
            doc.comment(line);
            doc.lines.back().lf_only = !crlf;
            continue;
        }
        auto tokens = tokenize(line);
        if (tokens.empty()) {
            doc.blank();
            doc.lines.back().lf_only = !crlf;
            continue;
        }
        MissionLine record;
        if (tokens.size() >= 2 && std::isdigit(static_cast<unsigned char>(tokens[0][0]))) {
            record.args_only = true;
            record.args = std::move(tokens);
        } else {
            record.keyword = tokens.front();
            record.args.assign(tokens.begin() + 1, tokens.end());
        }
        record.lf_only = !crlf;
        doc.lines.push_back(std::move(record));
    }
    *out = std::move(doc);
    return true;
}

bool MissionDocument::save(const std::string& path, std::string* error) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        if (error) *error = "cannot write " + path;
        return false;
    }
    for (const auto& line : lines) {
        // The first line of the file uses a bare newline; every other line
        // ends with CRLF. `lf_only` preserves that on a round trip.
        const char* eol = line.lf_only ? "\n" : "\r\n";
        if (line.args_only) {
            for (std::size_t i = 0; i < line.args.size(); ++i) {
                if (i) out << ' ';
                out << line.args[i];
            }
            out << eol;
        } else if (line.comment) {
            out << line.text << eol;
        } else if (line.keyword.empty()) {
            out << eol;
        } else {
            out << line.keyword;
            for (const auto& arg : line.args) out << ' ' << arg;
            out << eol;
        }
    }
    return static_cast<bool>(out);
}

void MissionDocument::blank() {
    MissionLine line;
    lines.push_back(std::move(line));
}

void MissionDocument::comment(const std::string& text) {
    MissionLine line;
    line.comment = true;
    line.text = text;
    lines.push_back(std::move(line));
}

void MissionDocument::values(const std::vector<std::string>& args) {
    MissionLine line;
    line.args_only = true;
    line.args = args;
    lines.push_back(std::move(line));
}

void MissionDocument::add(const std::string& keyword,
                          const std::vector<std::string>& args) {
    MissionLine line;
    line.keyword = keyword;
    line.args = args;
    lines.push_back(std::move(line));
}

void MissionDocument::add(const std::string& keyword,
                          std::initializer_list<std::string> args) {
    add(keyword, std::vector<std::string>(args));
}

MissionCounts count_missions(const MissionDocument& document) {
    MissionCounts c;
    bool in_base = false;
    for (const auto& line : document.lines) {
        const auto& k = line.keyword;
        if (k == "MISSION") {
            ++c.missions;
            if (line.args.size() >= 1) {
                if (line.args[0] == "GBASED") ++c.gbased;
                else if (line.args[0] == "PLANE") ++c.plane;
                else if (line.args[0] == "SHIP") ++c.ship;
            }
            in_base = false;
        } else if (k == "FLIGHT") {
            ++c.flights;
        } else if (k == "GROUP") {
            ++c.groups;
        } else if (k == "PIL") {
            ++c.pilots;
        } else if (k == "BASE") {
            ++c.bases;
            in_base = true;
        } else if (k == "TIMESLOT") {
            ++c.timeslots;
        } else if (k == "FLAK") {
            if (!line.args.empty()) {
                c.flak = std::atoi(line.args[0].c_str());
            }
        } else if (k == "TARGET_AREA") {
            ++c.strike_targets;
        }
        (void)in_base;
    }
    return c;
}

}  // namespace ef
