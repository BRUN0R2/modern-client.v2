// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: the rules a stuck agent's watch follows as data/cstrike/bots.toml gives them, and a watch
// of them. Include after `import std;`, `import nx.core;` and `import nx.agents;`.

namespace nx::test {

/// Condition Zero's: samples each 0.1 s, five of them, stuck below 20 units a second, unstuck 75 away;
/// a wiggle of 0.5 to 1.5 s, the first jump within 0.5 s, then each 1 to 2 s; given up stuck 4.5 s, its
/// way searched once around where it got stuck, which costs twenty times its own.
[[nodiscard]] inline auto stuckRules() -> nx::agents::StuckRules {
    return {.sampleInterval = nx::Seconds{0.1F},
            .samples = 5,
            .stuckSpeed = nx::UnitsPerSecond{20.0F},
            .unstuckDistance = nx::HammerUnits{75.0F},
            .wiggle = {.shortest = nx::Seconds{0.5F}, .longest = nx::Seconds{1.5F}},
            .firstJump = nx::Seconds{0.5F},
            .jumpInterval = {.shortest = nx::Seconds{1.0F}, .longest = nx::Seconds{2.0F}},
            .giveUpTime = nx::Seconds{4.5F},
            .avoidedCost = 20.0F,
            .reroutes = 1};
}

/// A watch of `rules`, its draws of the seed `seed`.
[[nodiscard]] inline auto stuckWatch(nx::agents::StuckRules rules = stuckRules(), std::uint64_t seed = 1)
    -> nx::agents::StuckWatch {
    return nx::agents::StuckWatch{rules, nx::Random{seed, "agents tests"}};
}

} // namespace nx::test
