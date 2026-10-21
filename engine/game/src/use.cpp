// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;

namespace nx::game {

namespace {

/// How far from a player's origin what it uses may be (MAX_PLAYER_USE_RADIUS).
constexpr float kUseReach = 64.0F;
/// How straight ahead what a player uses must be: the cosine of the angle off (VIEW_FIELD_NARROW).
constexpr float kUseCone = 0.7F;

/// The squared distance from `point` to `box`, 0 inside it (FindEntityInSphere).
[[nodiscard]] auto squaredDistanceTo(Vec3 point, const Aabb& box) noexcept -> float {
    const Vec3 nearest{std::clamp(point.x, box.min.x, box.max.x), std::clamp(point.y, box.min.y, box.max.y),
                       std::clamp(point.z, box.min.z, box.max.z)};
    return lengthSquared(point - nearest);
}

/// Whether a player's use reaches `entity` (FCAP_IMPULSE_USE).
[[nodiscard]] auto isUsable(const ecs::World& world, ecs::Entity entity) -> bool {
    if (const Button* button = world.get<Button>(entity)) {
        return button->isUsable;
    }
    const Door* door = world.get<Door>(entity);
    return door != nullptr && door->isUseOnly;
}

} // namespace

auto playerUse(ecs::World& world, const collision::BrushHulls& hulls, ecs::Entity user, const UseView& view)
    -> std::optional<ecs::Entity> {
    const Transform* at = world.get<Transform>(user);
    expects(at != nullptr, "playerUse: a user with a Transform");
    if (at == nullptr) {
        return std::nullopt;
    }
    const Vec3 origin = at->origin;
    std::optional<ecs::Entity> closest;
    float mostAhead = kUseCone;
    ecs::Query<ecs::Entity, const BrushModel&, const Transform&>{world}.forEach(
        [&](ecs::Entity entity, const BrushModel& model, const Transform& placement) {
            if (!isUsable(world, entity)) {
                return;
            }
            const Aabb box = linkedBoxOf(hulls.boundsOf(model.index), placement.origin, placement.angles);
            if (squaredDistanceTo(origin, box) > kUseReach * kUseReach) {
                return;
            }
            const float ahead = dot(normalized(box.center() - view.eye), view.forward);
            if (ahead > mostAhead) {
                mostAhead = ahead;
                closest = entity;
            }
        });
    if (closest) {
        use(world, hulls, *closest, user);
    }
    return closest;
}

} // namespace nx::game
