// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.agents;
import cs.shared;

namespace cs {

namespace {

/// Whether `first` and `second`, boxes about the origins `firstOrigin` and `secondOrigin`, come within
/// kTouchMargin of each other along every axis.
[[nodiscard]] auto areTouching(nx::Vec3 firstOrigin, const nx::Aabb& first, nx::Vec3 secondOrigin,
                               const nx::Aabb& second) noexcept -> bool {
    const nx::Vec3 firstLeast = firstOrigin + first.min;
    const nx::Vec3 firstMost = firstOrigin + first.max;
    const nx::Vec3 secondLeast = secondOrigin + second.min;
    const nx::Vec3 secondMost = secondOrigin + second.max;
    return firstLeast.x <= secondMost.x + kTouchMargin && secondLeast.x <= firstMost.x + kTouchMargin &&
           firstLeast.y <= secondMost.y + kTouchMargin && secondLeast.y <= firstMost.y + kTouchMargin &&
           firstLeast.z <= secondMost.z + kTouchMargin && secondLeast.z <= firstMost.z + kTouchMargin;
}

} // namespace

auto touchedBy(const Toucher& toucher, std::span<const PerceptionTarget> targets)
    -> std::vector<nx::agents::Observation> {
    std::vector<nx::agents::Observation> touched;
    for (const PerceptionTarget& target : targets) {
        if (target.entity == toucher.entity || toucher.team == target.team) {
            continue;
        }
        const nx::Aabb& box = target.isDucked ? kDuckedBox : kStandingBox;
        if (areTouching(toucher.origin, toucher.box, target.origin, box)) {
            touched.push_back({.entity = target.entity, .position = target.feet, .aimAt = target.eye});
        }
    }
    return touched;
}

} // namespace cs
