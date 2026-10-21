// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game's clock (docs/07-simulation-and-physics.md, section 1): when its world is, in ticks, and
/// how long a tick lasts, a resource of the world that registerComponents gives it.
export module nx.game:clock;

import std;
import nx.core;
import nx.ecs;

export namespace nx::game {

/// When a world's game is, and how fast it ticks (`sv_tickrate`): a resource of the world.
struct TickTime {
    TickRate rate = kDefaultTickRate;
    GameTime now{};

    /// How long a tick lasts.
    [[nodiscard]] inline auto delta() const noexcept -> Seconds { return tickLengthOf(rate); }
};

/// The clock of `world`, which registerComponents gives it.
[[nodiscard]] auto tickTimeOf(const ecs::World& world) noexcept -> const TickTime&;

/// Moves the clock of `world` on to the next tick.
void advanceTick(ecs::World& world) noexcept;

} // namespace nx::game
