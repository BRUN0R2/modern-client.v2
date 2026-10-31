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
import :weapons;
import :characters;
import :lightStyles;
import :entityCatalog;
import :hitGroups;

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

/// The blood a hit sprays where it met a body (TE_BLOODSPRITE): the point, the way the hit went, the
/// palette entry of its color, its size (spraySizeOf), and where on the body it met it. The original's
/// carries neither the way nor the hit group, which the enhanced look sprays it by.
struct BloodSpray {
    nx::Vec3 position{};
    nx::Vec3 direction{};
    std::uint8_t color = 0;
    std::uint32_t size = 0;
    HitGroup hitGroup = HitGroup::Generic;

    [[nodiscard]] inline auto operator==(const BloodSpray&) const noexcept -> bool = default;
};

/// The stream of blood a hit makes a head without a helmet shed where it met it (TE_BLOODSTREAM): the
/// point, the way the hit went, the palette entry of its color, and its speed, in whole units a second
/// (streamSpeedOf).
struct BloodStream {
    nx::Vec3 position{};
    nx::Vec3 direction{};
    std::uint8_t color = 0;
    std::uint32_t speed = 0;

    [[nodiscard]] inline auto operator==(const BloodStream&) const noexcept -> bool = default;
};

/// A splash of streaks, a helmet's sparks (TE_STREAK_SPLASH): the point, the way they fly off, the
/// streak color of their color, how many, how fast, and the most each is drawn faster or slower than
/// that, as much on each axis (HelmetSparks).
struct StreakSplash {
    nx::Vec3 position{};
    nx::Vec3 direction{};
    std::uint8_t color = 0;
    std::uint32_t count = 0;
    nx::UnitsPerSecond speed{};
    nx::UnitsPerSecond spread{};

    [[nodiscard]] inline auto operator==(const StreakSplash&) const noexcept -> bool = default;
};

/// Where the blood a hit shed met what is solid behind it: the point, what it is on, and the decal it
/// leaves there, of BloodRules::decals.
struct BloodMark {
    nx::Vec3 position{};
    StampTarget on{};
    std::uint32_t decal = 0;

    [[nodiscard]] inline auto operator==(const BloodMark&) const noexcept -> bool = default;
};

/// What the server tells a client of another player for it to draw, as the original's entity state
/// carries it: which player it is, the character it plays as, the weapon in its hands (its player model:
/// models/p_<weapon>.mdl), where it is, how its body is turned (its pitch a third of its view's,
/// negated), the sequence its torso plays, where in it, when and at what framerate, the gait sequence
/// its legs play, and whether it is dead, its body a corpse (deadflag); the client turns its legs and its
/// aim itself (processGait).
struct PlayerEntityState {
    PlayerIndex player{};
    CharacterIndex character{};
    std::optional<WeaponIndex> weapon{};
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
    nx::assets::SequenceIndex sequence{};
    float frame = 0.0F; ///< Of kFrameScale.
    nx::GameTime advancedAt{};
    float framerate = 1.0F;
    nx::assets::SequenceIndex gaitSequence{};
    bool isDead = false;

    [[nodiscard]] inline auto operator==(const PlayerEntityState&) const noexcept -> bool = default;
};

/// What the server tells a player's client after a tick: the moment of the game; the player, where it
/// is and how it moves, what it has and its weapons, and what its HUD was told of the round's time;
/// what the tick did to it and made it make; where the tick's shots and the blood they shed met what is
/// solid, and the blood they sprayed; and the world
/// it sees: the brush entities, the hostages, the other players and the light-style patterns.
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
    std::vector<Impact> impacts{};              ///< Of the tick's shots, in their order.
    std::vector<BloodMark> bloodMarks{};        ///< Of the tick's hits, in their order.
    std::vector<BloodSpray> bloodSprays{};      ///< Of the tick's hits, in their order.
    std::vector<BloodStream> bloodStreams{};    ///< Of the tick's hits in the head, in their order.
    std::vector<StreakSplash> streakSplashes{}; ///< Of the tick, in their order: the helmets' sparks.
    std::vector<BrushEntityState> brushEntities{};
    std::vector<HostageState> hostages{};
    std::vector<PlayerEntityState> players{};
    std::vector<EntityState> entities{};
    MapLightStyles lightStyles{}; ///< The server's current patterns, evaluated by the client.
};

} // namespace cs
