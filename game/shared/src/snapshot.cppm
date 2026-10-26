// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the server tells a player's client after each tick (docs/01-architecture.md, section 2.3;
/// docs/08-networking.md): data the client draws, hears and shows, as the original's server sends its
/// clients their own player's state, the entities they see and the events of the frame. The server
/// fills it; nothing in it points back into the server.
export module cs.shared:snapshot;

import std;
import nx.core;
import nx.math;
import nx.assets;
import :movement;
import :player;
import :inventory;
import :rounds;
import :sounds;
import :hostages;
import :lightStyles;

export namespace cs {

/// Where the game has a brush entity, and which of its looks it shows: what a client draws it by.
struct BrushEntityState {
    nx::assets::ModelIndex model{};
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
    /// How high the waves of its turbulent faces rise (nx::game::Waves); none for none.
    nx::HammerUnits waveHeight{};
    /// It shows its textures' alternate sequences (nx::game::AlternateTextures).
    bool showsAlternateTextures = false;

    [[nodiscard]] inline auto operator==(const BrushEntityState&) const noexcept -> bool = default;
};

/// What a mark is stamped on: the world, or the brush entity of model `model` where the game has it as
/// it is stamped.
struct StampTarget {
    nx::assets::ModelIndex model = nx::assets::kWorldModel;
    nx::Vec3 origin{};
    nx::EulerAngles angles{};

    [[nodiscard]] inline auto operator==(const StampTarget&) const noexcept -> bool = default;
};

/// Where a shot met what is solid: the point, and what it is on, which a bullet leaves its mark on.
struct Impact {
    nx::Vec3 position{};
    StampTarget on{};

    [[nodiscard]] inline auto operator==(const Impact&) const noexcept -> bool = default;
};

/// What the server tells a player's client after a tick: the moment of the game; the player, where it
/// is and how it moves, what it has and its weapons, and what its HUD was told of the round's time;
/// what the tick did to it and made it make; where the tick's shots met what is solid; and the world
/// it sees: the brush entities, the hostages and the light-style patterns.
struct PlayerSnapshot {
    nx::GameTime time{}; ///< After the tick.
    nx::TickRate rate{nx::kDefaultTickRate};
    MovementState player{};
    PlayerStatus status{};
    Inventory inventory;
    RoundTimer roundTimer{};
    /// The way the game turned its view in the tick, as the original's fixangle turns it (the way the
    /// spawn point it spawned at faces); none.
    std::optional<nx::EulerAngles> turnedView{};
    /// The game stood it somewhere in the tick (it spawned, or a tool stood it): what shows of it goes
    /// there at once, not between the ticks.
    bool wasPlaced = false;
    /// The sounds it made in the tick, which it hears, each with the moment it was made: its weapons',
    /// then its breath's, its feet's, its use's, its pain's and its death's.
    std::vector<SoundMade> sounds{};
    std::vector<Impact> impacts{}; ///< Of the tick's shots, in their order.
    std::vector<BrushEntityState> brushEntities{};
    std::vector<HostageState> hostages{};
    MapLightStyles lightStyles{}; ///< The server's current patterns, evaluated by the client.
};

} // namespace cs
