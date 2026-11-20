// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 velteyn

#pragma once

#include "conflict.h"

#include <cstdint>
#include <string>

namespace ef {

//! The generator's custom 32-bit additive random number generator.
//!
//! The state is two 16-bit halves held at `0xb2458` in the v2 image. The
//! default is 0x31415926. A draw computes `high' = high + low`, then
//! `low' = low + high' + carry`; an all-zero state is reset to 0x31415926.
//! The game seeds it from the DOS date/time at startup.
class CampaignRng {
public:
    explicit CampaignRng(std::uint32_t seed = 0x31415926) { seed_state(seed); }

    void seed_state(std::uint32_t seed);
    std::uint32_t state() const {
        return (static_cast<std::uint32_t>(high_) << 16) | low_;
    }
    std::uint32_t next();
    //! Uniform value in [0, n); returns 0 for n <= 0.
    int range(int n);
    //! Uniform value in [lo, hi].
    int between(int lo, int hi);

private:
    std::uint16_t high_ = 0x3141;
    std::uint16_t low_ = 0x5926;
};

struct CampaignSettings {
    int difficulty = 6;
    int influence = 1;
    std::uint32_t seed = 0x31415926;
};

//! Applies the observed target-value pass. The thousands digit is drawn as
//! `(rng % 5 + 1) * 1000` for the selected target class and combined with a
//! three-digit site value. The exact class selector is still open, so this
//! version selects every other target and draws the site value from the RNG.
int randomize_target_values(Scenario& scenario, CampaignRng& rng);

}  // namespace ef
