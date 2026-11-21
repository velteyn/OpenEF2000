// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "fonts.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

namespace ef {
namespace {

std::vector<std::string> font_candidates() {
    std::vector<std::string> out;
    if (const char* override_path = std::getenv("OPENEF2000_FONT")) {
        if (*override_path != '\0') out.emplace_back(override_path);
    }

#if defined(_WIN32)
    std::string windir = "C:/Windows";
    if (const char* env = std::getenv("WINDIR")) {
        if (*env != '\0') windir = env;
    }
    for (const char* name : {"segoeui.ttf", "arial.ttf", "tahoma.ttf", "verdana.ttf"}) {
        out.push_back(windir + "/Fonts/" + name);
    }
#elif defined(__APPLE__)
    for (const char* path : {
             "/System/Library/Fonts/Supplemental/Arial.ttf",
             "/System/Library/Fonts/Supplemental/Verdana.ttf",
             "/System/Library/Fonts/Helvetica.ttc",
             "/Library/Fonts/Arial.ttf",
         }) {
        out.emplace_back(path);
    }
#else
    for (const char* path : {
             "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
             "/usr/share/fonts/TTF/DejaVuSans.ttf",
             "/usr/share/fonts/dejavu/DejaVuSans.ttf",
             "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
             "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
             "/usr/share/fonts/truetype/freefont/FreeSans.ttf",
             "/usr/share/fonts/truetype/unifont/unifont.ttf",
         }) {
        out.emplace_back(path);
    }
#endif
    return out;
}

}  // namespace

const char* ui_font_path() {
    static const std::string resolved = [] {
        std::error_code ec;
        for (const auto& candidate : font_candidates()) {
            if (std::filesystem::is_regular_file(candidate, ec)) return candidate;
        }
        return std::string{};
    }();
    return resolved.empty() ? nullptr : resolved.c_str();
}

}  // namespace ef
