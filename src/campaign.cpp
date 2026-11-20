// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#include "campaign.h"

namespace ef {

void CampaignRng::seed_state(std::uint32_t seed) {
    low_ = static_cast<std::uint16_t>(seed & 0xFFFFu);
    high_ = static_cast<std::uint16_t>(seed >> 16);
    if (low_ == 0 && high_ == 0) {
        low_ = 0x5926;
        high_ = 0x3141;
    }
}

std::uint32_t CampaignRng::next() {
    const std::uint16_t old_low = low_;
    const std::uint16_t old_high = high_;
    const std::uint32_t add = static_cast<std::uint32_t>(old_high) + old_low;
    const std::uint16_t new_high = static_cast<std::uint16_t>(add);
    const std::uint16_t new_low =
        static_cast<std::uint16_t>(old_low + new_high + (add >> 16));
    high_ = new_high;
    low_ = new_low;
    if (high_ == 0 && low_ == 0) {
        low_ = 0x5926;
        high_ = 0x3141;
    }
    return state();
}

int CampaignRng::range(int n) {
    if (n <= 1) return 0;
    return static_cast<int>(next() % static_cast<std::uint32_t>(n));
}

int CampaignRng::between(int lo, int hi) {
    if (hi <= lo) return lo;
    return lo + range(hi - lo + 1);
}

int randomize_target_values(Scenario& scenario, CampaignRng& rng) {
    // The original selects a target class/status byte (codes '1', 'I', 0x0b,
    // 'V' in the sampled image). That selector is not decoded yet; use a
    // deterministic half of the areas so the distribution matches the capture
    // while keeping the pass reproducible from the seed.
    int changed = 0;
    for (std::size_t i = 0; i < scenario.areas.size(); ++i) {
        auto& area = scenario.areas[i];
        const int site = rng.range(1000);
        if ((i % 2) == 0) {
            area.value = (rng.range(5) + 1) * 1000 + site;
            ++changed;
        } else {
            area.value = 1000;
        }
    }
    return changed;
}

}  // namespace ef
