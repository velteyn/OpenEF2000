// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

#include "model.h"

#include <string>
#include <vector>

namespace ef {

std::vector<std::string> tokenize(const std::string& line);
int to_int(const std::string& s);

bool parse_mission(const std::string& path, Dataset& ds);
bool parse_navig(const std::string& path, Dataset& ds);
bool parse_conflict(const std::string& path, Dataset& ds);

Dataset load_dataset(const std::string& dir);

}  // namespace ef
