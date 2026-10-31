// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The first generic entity body: a standing humanoid, moved and attacked by commands (ADR-0028).
export module cs.server:entityBodies;
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.game;
import cs.shared;

export namespace cs {

/// What an entity's body is doing, which its model plays.
enum class EntityMotion : std::uint8_t {
    Idle,      ///< Standing, not commanded to move.
    Walking,   ///< Moving by its command.
    Attacking, ///< Striking, until its melee interval ends.
    Dead,      ///< Killed: no longer solid nor moved, its death played.
};

/// An entity's body of a type of the catalog: how it moves, its health, how its model plays, what it
/// is doing, when it may strike again, what it keeps of the hits that knocked it back, and how far it
/// walked on the ground since its last step.
struct EntityBody {
    EntityTypeIndex type{};
    MovementState movement{};
    nx::HitPoints health{};
    Animation animation{};
    EntityMotion motion = EntityMotion::Idle;
    nx::GameTime attacksAgainAt{};
    KnockedBack knockedBack{};
    nx::HammerUnits walked{};
};

/// The command an entity's body is driven by in a tick, as a player's client sends its own.
struct EntityCommand {
    nx::ecs::Entity entity{};
    UserCommand command{};
};

/// A strike of an entity's body on `target`, which the game deals as its kind takes it.
struct EntityAttack {
    nx::ecs::Entity target{};
    nx::game::Attack attack{};
};

/// Requires nx.game's registered components and a catalog that outlives the world. Errors: the
/// component registry's ComponentError; no body can be spawned before registration succeeds.
[[nodiscard]] auto registerEntityBodies(nx::ecs::World& world, const EntityCatalog& catalog)
    -> nx::Result<void, nx::ecs::ComponentError>;
/// Refuses a nonfinite, non-upright or occupied placement before making an entity. Drops at most the engine's
/// drop-to-floor distance. Requires registered body data and a type issued by its catalog.
/// Errors: InvalidPlacement. The returned handle is the ECS generation, never a player index.
[[nodiscard]] auto spawnEntityBody(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                                   EntityTypeIndex type, nx::game::Transform placement)
    -> nx::Result<nx::ecs::Entity, EntityError>;
/// What the entities' bodies did in a tick: the strikes they made, and the steps they took, in their
/// order.
struct EntityBodiesTick {
    std::vector<EntityAttack> attacks{};
    std::vector<EntityStep> steps{};
};

/// Each body advances once in the world's tick, whether commanded or resting; corpses animate too. A living
/// body of a type with a stride steps each time it walked a stride on the ground (EntityDefinition::stride),
/// the rest of its way kept toward its next step.
[[nodiscard]] auto runEntityBodies(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                                   std::span<const EntityCommand> commands) -> EntityBodiesTick;
/// States, including corpses, ordered by complete generational handles; requires registered body data.
[[nodiscard]] auto entityStatesOf(nx::ecs::World& world) -> std::vector<EntityState>;

} // namespace cs
