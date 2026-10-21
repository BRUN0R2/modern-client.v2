// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;

namespace nx::game {

auto linkedBoxOf(const Aabb& bounds, Vec3 origin, EulerAngles angles) -> Aabb {
    const Vec3 growth{kLoadedBoundsGrowth, kLoadedBoundsGrowth, kLoadedBoundsGrowth};
    const Vec3 link{kLinkGrowth, kLinkGrowth, kLinkGrowth};
    const Vec3 low = bounds.min - growth;
    const Vec3 high = bounds.max + growth;
    if (angles == EulerAngles{}) {
        return Aabb{.min = origin + low - link, .max = origin + high + link};
    }
    const float reach = std::max({std::abs(low.x), std::abs(high.x), std::abs(low.y), std::abs(high.y),
                                  std::abs(low.z), std::abs(high.z)});
    const Vec3 cube{reach, reach, reach};
    return Aabb{.min = origin - cube - link, .max = origin + cube + link};
}

auto linkedBoxOf(const Body& body, Vec3 origin) -> Aabb {
    const Vec3 link{kLinkGrowth, kLinkGrowth, kLinkGrowth};
    return Aabb{.min = origin + body.box.min - link, .max = origin + body.box.max + link};
}

} // namespace nx::game
