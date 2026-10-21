// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The components of the game's entities (docs/03-ecs.md, section 4): what the entities made when a
/// map starts are, and what moves them.
export module nx.game:components;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

export namespace nx::game {

/// How solid an entity is, as the original's pev->solid.
enum class Solid : std::uint8_t {
    Not, ///< Nothing collides with it.
    Bsp, ///< Its brush model stops what moves into it.
    Count,
};

/// A brush entity's shape: brush model `index` of the map ("model" "*3").
struct BrushModel {
    std::uint32_t index = 0;
};

/// Where an entity is.
struct Transform {
    Vec3 origin{};
    EulerAngles angles{};
};

/// How solid a brush entity is, and the contents a point inside it is in, as the original's
/// pev->solid and pev->skin: 0 for none, -16 a ladder, else a value of nx::assets::Contents.
struct BrushSolidity {
    Solid solid = Solid::Not;
    std::int32_t contents = 0;
};

/// A brush entity the game moves, as the original moves its pushers (MOVETYPE_PUSH): at its
/// velocity, by a clock of its own that runs only while it moves or waits to act.
struct Pusher {
    Vec3 velocity{};
    Seconds localTime{};
    std::optional<Seconds> nextThink{}; ///< When it acts next, by localTime; none while it rests.
    Vec3 destination{};                 ///< Of the move under way.
};

/// How far a door has gone, as the original's toggle states.
enum class DoorState : std::uint8_t {
    Closed,
    Opening,
    Open,
    Closing,
};

/// A door that slides between where it is closed and where it is open (func_door, func_water).
struct SlidingDoor {
    Vec3 closed{};
    Vec3 open{};
    UnitsPerSecond speed{};
    Seconds wait{}; ///< Open, before it closes; negative: it stays open.
    DoorState state = DoorState::Closed;
    bool isNamed = false;     ///< Others open it ("targetname", "master"): a touch does not.
    bool isUseOnly = false;   ///< Only use opens it, never a touch.
    bool staysOpen = false;   ///< "Toggle": it closes when it is opened again.
    bool isTouchable = false; ///< A touch acts now: not while it moves.
    Seconds lastBlocked{};    ///< When something last stopped it, by the game's time.
};

/// A body a pusher moves out of its way, and carries while it stands on it: a player's box (the
/// original's MOVETYPE_WALK).
struct Body {
    collision::Hull hull = collision::Hull::Standing;
    Vec3 mins{}; ///< Of its box, about its origin.
    Vec3 maxs{};
    std::optional<std::uint32_t> groundModel{}; ///< The brush model of the entity it stands on.
};

/// Registers the components above in `world`. Errors: those of ecs::World::registerComponent.
[[nodiscard]] auto registerComponents(ecs::World& world) -> Result<void, ecs::ComponentError>;

} // namespace nx::game
