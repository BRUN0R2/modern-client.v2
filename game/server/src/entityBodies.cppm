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

enum class EntityMotion : std::uint8_t { Idle, Walking, Attacking, Dead };

struct EntityBody {
    EntityTypeIndex type{};
    MovementState movement{};
    nx::HitPoints health{};
    Animation animation{};
    EntityMotion motion = EntityMotion::Idle;
    nx::GameTime attacksAgainAt{};
};

struct EntityCommand {
    nx::ecs::Entity entity{};
    UserCommand command{};
};

struct EntityAttack {
    nx::ecs::Entity target{};
    nx::game::Attack attack{};
};

[[nodiscard]] auto registerEntityBodies(nx::ecs::World& world, const EntityCatalog& catalog)
    -> nx::Result<void, nx::ecs::ComponentError>;
/// Refuses a nonfinite, non-upright or occupied placement before making an entity. Drops at most the engine's
/// drop-to-floor distance. The returned handle is the ECS generation, never a player index.
[[nodiscard]] auto spawnEntityBody(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                                   EntityTypeIndex type, nx::game::Transform placement)
    -> nx::Result<nx::ecs::Entity, EntityError>;
/// Each body advances once in the world's tick, whether commanded or resting; corpses animate too.
[[nodiscard]] auto runEntityBodies(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                                   std::span<const EntityCommand> commands) -> std::vector<EntityAttack>;
[[nodiscard]] auto entityStatesOf(nx::ecs::World& world) -> std::vector<EntityState>;

} // namespace cs
