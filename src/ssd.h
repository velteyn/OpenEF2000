// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

// Supershape (`.ssd`) label table.
//
// After its bytecode an installation model carries a run of 20-byte records:
//
//   char name[8];   // "TARGPOS"
//   uint16 pad;
//   uint16 kind;    // 51 = target element, 0xFFFF = defence point
//   int16  x;
//   int16  pad;   // second half of the x slot
//   int16  y;
//   int16  z;
//
// The coordinates are in model space. The campaign generator uses the `51`
// entries as a target's sub-elements and the `0xFFFF` entries as its defence
// ring, rotated by the area's heading.

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace ef {

struct SsLabel {
    std::uint16_t kind = 0;
    std::int32_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

//! Marker kinds.
inline constexpr std::uint16_t kSsTargetElement = 51;
inline constexpr std::uint16_t kSsDefencePoint = 0xFFFF;

//! Extracts the label run from an `.ssd` image. Returns an empty vector when
//! the file carries no table.
std::vector<SsLabel> parse_ssd_labels(std::span<const std::uint8_t> data);

//! Rotates a model-space offset by a heading in {0, 90, 180, 270} degrees,
//! matching how the generator places ring and target points.
void rotate_by_heading(int heading, int* x, int* y);

}  // namespace ef
