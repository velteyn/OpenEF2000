// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

namespace ef {

//! Path of a TrueType font the host provides, or nullptr when none of the
//! usual candidates exists. The original's own fonts are bitmap images in the
//! archives; SDL_ttf is only used for the research overlays and labels, so a
//! system font is enough. Set `OPENEF2000_FONT` to override the search.
const char* ui_font_path();

}  // namespace ef
