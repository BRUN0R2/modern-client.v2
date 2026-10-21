// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.ecs;

namespace nx::game {

namespace {

/// What a world without a clock reads: the start of a map at the default rate.
constexpr TickTime kStart{};

} // namespace

auto tickTimeOf(const ecs::World& world) noexcept -> const TickTime& {
    const TickTime* clock = world.resource<TickTime>();
    expects(clock != nullptr, "tickTimeOf: a world registerComponents made ready");
    return clock != nullptr ? *clock : kStart;
}

void advanceTick(ecs::World& world) noexcept {
    TickTime* clock = world.resource<TickTime>();
    expects(clock != nullptr, "advanceTick: a world registerComponents made ready");
    if (clock != nullptr) {
        clock->now = clock->now.ticksLater(1);
    }
}

} // namespace nx::game
