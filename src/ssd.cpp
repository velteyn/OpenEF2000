// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "ssd.h"

namespace ef {
namespace {

constexpr std::size_t kRecord = 20;
constexpr std::size_t kNameLength = 8;
constexpr std::size_t kMinRun = 2;

bool printable_name(const std::uint8_t* p) {
    int letters = 0;
    for (std::size_t i = 0; i < kNameLength; ++i) {
        const std::uint8_t c = p[i];
        if (c == 0) return letters > 0;
        if (c < 0x20 || c > 0x7E) return false;
        ++letters;
    }
    return letters > 0;
}

std::int16_t read_i16(const std::uint8_t* p) {
    return static_cast<std::int16_t>(static_cast<std::uint16_t>(p[0]) |
                                     (static_cast<std::uint16_t>(p[1]) << 8));
}

}  // namespace

std::vector<SsLabel> parse_ssd_labels(std::span<const std::uint8_t> data) {
    // The table is a contiguous run of records whose eight-byte name is
    // printable. Take the longest such run so stray text in the bytecode does
    // not win.
    std::vector<SsLabel> best;
    for (std::size_t start = 0; start + kRecord * kMinRun <= data.size(); ++start) {
        if (!printable_name(data.data() + start)) continue;
        if (!printable_name(data.data() + start + kRecord)) continue;
        std::vector<SsLabel> run;
        std::size_t p = start;
        while (p + kRecord <= data.size() && printable_name(data.data() + p)) {
            const std::uint8_t* r = data.data() + p;
            SsLabel label;
            label.kind = static_cast<std::uint16_t>(read_i16(r + 10));
            label.x = read_i16(r + 12);
            label.y = read_i16(r + 16);
            label.z = read_i16(r + 18);
            run.push_back(label);
            p += kRecord;
        }
        if (run.size() > best.size()) best = std::move(run);
    }
    return best;
}

void rotate_by_heading(int heading, int* x, int* y) {
    if (x == nullptr || y == nullptr) return;
    const int h = ((heading % 360) + 360) % 360;
    const int ox = *x, oy = *y;
    switch (h) {
        case 90:
            *x = oy;
            *y = -ox;
            break;
        case 180:
            *x = -ox;
            *y = -oy;
            break;
        case 270:
            *x = -oy;
            *y = ox;
            break;
        default:
            break;
    }
}

}  // namespace ef
