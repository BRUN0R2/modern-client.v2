// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The components of the game's entities (docs/03-ecs.md, section 4): what the entities made when a
/// map starts are, and what moves them.
export module nx.game:components;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.assets;
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
    assets::ModelIndex index{};
};

/// Where an entity is.
struct Transform {
    Vec3 origin{};
    EulerAngles angles{};
};

/// How solid a brush entity is, and what a point inside it is in, as the original's pev->solid and
/// pev->skin: nothing (a skin of 0, or of a number that names no contents), a ladder, or a volume.
struct BrushSolidity {
    Solid solid = Solid::Not;
    std::optional<assets::Contents> contents{};
};

/// A brush entity the game moves, as the original moves its pushers (MOVETYPE_PUSH): at its
/// velocity and its turning one, by a clock of its own that runs only while it moves or waits to act.
struct Pusher {
    Vec3 velocity{};
    Vec3 angularVelocity{}; ///< Degrees a second about the pitch, yaw and roll axes (avelocity).
    GameTime localTime{};
    std::optional<GameTime> nextThink{}; ///< When it acts next, by localTime; none while it rests.
    Vec3 destination{};                  ///< Of the move under way.
    EulerAngles destinationAngles{};     ///< Of the turn under way.
    bool isTurning = false;              ///< The move under way turns it (AngularMove), not slides it.
};

/// How far a door has gone, as the original's toggle states.
enum class DoorState : std::uint8_t {
    Closed,
    Opening,
    Open,
    Closing,
};

/// A door that slides between where it is closed and where it is open (func_door, func_water).
struct DoorSlide {
    Vec3 closed{};
    Vec3 open{};
    UnitsPerSecond speed{};
};

/// A door that turns between the angles it has closed and open (func_door_rotating).
struct DoorTurn {
    EulerAngles closed{};
    EulerAngles open{};
    DegreesPerSecond speed{};
    /// It opens away from the player that opens it: about the yaw axis, but one-way.
    bool opensAway = false;
};

/// A door (CBaseDoor, CRotDoor): how it moves, how long it stays open, and what opens it.
struct Door {
    std::variant<DoorSlide, DoorTurn> motion{};
    Seconds wait{}; ///< Open, before it closes; negative: it stays open.
    DoorState state = DoorState::Closed;
    bool isNamed = false;     ///< Others open it ("targetname", "master"): a touch does not.
    bool isUseOnly = false;   ///< Only use opens it, never a touch.
    bool staysOpen = false;   ///< "Toggle": it closes when it is opened again.
    bool isTouchable = false; ///< A touch acts now: not while it moves.
    ecs::Entity activator{};  ///< What opened it last.
    GameTime lastBlocked{};   ///< When something last stopped it, by the game's clock.
};

/// How high the waves of a brush entity's turbulent faces rise: the original's pev->scale of a brush
/// entity, which its renderer raises them by (EmitWaterPolys). A door's is an eighth of its
/// "WaveHeight" (CBaseDoor::KeyValue), the key func_water sets.
struct Waves {
    float height = 0.0F;
};

/// A body a pusher moves out of its way, and carries while it stands on it: a player's box (the
/// original's MOVETYPE_WALK).
struct Body {
    collision::Hull hull = collision::Hull::Standing;
    Aabb box{};                                      ///< About its origin.
    std::optional<assets::ModelIndex> groundModel{}; ///< The brush model of the entity it stands on.
};

/// Registers the components of nx.game in `world` (those above, NextThink, Named, Targets,
/// DelayedUse, Trigger and Button), and gives it a clock at the start of its map (TickTime, at the default
/// rate) and a TargetChain. Errors: those of ecs::World::registerComponent.
[[nodiscard]] auto registerComponents(ecs::World& world) -> Result<void, ecs::ComponentError>;

} // namespace nx::game
