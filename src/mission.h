// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

// Lossless reader/writer for the generated mission file (`MISSION.CFG`).
//
// The format is line oriented: every line is a keyword followed by
// whitespace-separated tokens, and quoted strings keep their quotes. Comment
// lines start with ';' and are preserved verbatim, including their spacing.
// Everything else normalises to single spaces, which reproduces the shipped
// files byte for byte.

#include <cstdint>
#include <string>
#include <vector>

namespace ef {

struct MissionLine {
    std::string keyword;              //!< empty for a blank line
    std::vector<std::string> args;
    bool comment = false;             //!< keep `text` verbatim
    bool lf_only = false;             //!< the first line ends with LF, not CRLF
    bool args_only = false;           //!< numeric rows (the FLAK box list)
    std::string text;
};

class MissionDocument {
public:
    std::vector<MissionLine> lines;

    static bool load(const std::string& path, MissionDocument* out,
                     std::string* error);
    bool save(const std::string& path, std::string* error) const;

    void clear() { lines.clear(); }
    void blank();
    void comment(const std::string& text);
    void values(const std::vector<std::string>& args);
    void add(const std::string& keyword,
             const std::vector<std::string>& args = {});
    void add(const std::string& keyword,
             std::initializer_list<std::string> args);

    //! Splits one line into tokens, keeping quoted strings intact.
    static std::vector<std::string> tokenize(const std::string& line);
};

//! Aggregate view used by the generator tests and the statistics screen.
struct MissionCounts {
    int missions = 0;
    int gbased = 0;
    int plane = 0;
    int ship = 0;
    int flights = 0;
    int groups = 0;
    int bases = 0;
    int flak = 0;
    int pilots = 0;
    int strike_targets = 0;
    int timeslots = 0;
};

MissionCounts count_missions(const MissionDocument& document);

}  // namespace ef
