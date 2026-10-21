// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.game;

import std;
import nx.core;
import nx.math;
import nx.data;
import nx.ecs;
import nx.assets;

namespace nx::game {

namespace {

// The spawnflags of a door the game reads.
constexpr std::int32_t kStartsOpen = 1;
constexpr std::int32_t kStaysOpen = 32; ///< "Toggle": the original's SF_DOOR_NO_AUTO_RETURN.
constexpr std::int32_t kUseOnly = 256;

/// A door's speed when its "speed" is 0 or absent.
constexpr UnitsPerSecond kDoorSpeed{100.0F};

/// The engine grows a brush model's bounds by a unit on every side when it loads them, and the
/// game takes it back from a door's size.
constexpr float kLoadedBoundsGrowth = 1.0F;
constexpr float kDoorSizeTrim = 2.0F;

/// The "angles" of an entity; else its "angle", which the original's engine turns into angles:
/// a yaw, -1 for up, any other negative for down.
[[nodiscard]] auto anglesOf(const assets::BspEntity& entity) -> Vec3 {
    if (const std::optional<Vec3> angles = entity.findVector("angles")) {
        return *angles;
    }
    constexpr float kUp = -1.0F;
    constexpr float kQuarterTurn = 90.0F;
    const float angle = entity.findNumber("angle").value_or(0.0F);
    if (angle >= 0.0F) {
        return Vec3{.y = angle};
    }
    return Vec3{.x = static_cast<int>(angle) == static_cast<int>(kUp) ? -kQuarterTurn : kQuarterTurn};
}

/// The way a mover goes for `angles` (the original's SetMovedir): (0 -1 0) up, (0 -2 0) down, else
/// the way they face.
[[nodiscard]] auto moveDirectionOf(Vec3 angles) -> Vec3 {
    constexpr Vec3 kUpAngles{.y = -1.0F};
    constexpr Vec3 kDownAngles{.y = -2.0F};
    if (angles == kUpAngles) {
        return Vec3{.z = 1.0F};
    }
    if (angles == kDownAngles) {
        return Vec3{.z = -1.0F};
    }
    return angleVectors(
               EulerAngles{.pitch = Degrees{angles.x}, .yaw = Degrees{angles.y}, .roll = Degrees{angles.z}})
        .forward;
}

/// A sliding door of `entity`, its model's bounds `bounds`, at `origin`, as CBaseDoor::Spawn makes
/// it; and where it starts.
struct MadeDoor {
    SlidingDoor door{};
    Vec3 origin{};
};

[[nodiscard]] auto slidingDoorOf(const assets::BspEntity& entity, const Aabb& bounds, Vec3 origin)
    -> MadeDoor {
    const std::int32_t flags = entity.findInteger("spawnflags").value_or(0);
    const Vec3 direction = moveDirectionOf(anglesOf(entity));
    const Vec3 size = (bounds.max + Vec3{kLoadedBoundsGrowth, kLoadedBoundsGrowth, kLoadedBoundsGrowth}) -
                      (bounds.min - Vec3{kLoadedBoundsGrowth, kLoadedBoundsGrowth, kLoadedBoundsGrowth});
    const float lip = entity.findNumber("lip").value_or(0.0F);
    // How far it goes: its size along the way it goes, less its lip.
    const double travel = std::abs(double{direction.x * (size.x - kDoorSizeTrim)}) +
                          std::abs(double{direction.y * (size.y - kDoorSizeTrim)}) +
                          std::abs(double{direction.z * (size.z - kDoorSizeTrim)}) - double{lip};
    MadeDoor made{.door = SlidingDoor{.closed = origin,
                                      .open = origin + (direction * static_cast<float>(travel)),
                                      .wait = Seconds{entity.findNumber("wait").value_or(0.0F)},
                                      .isNamed = !entity.find("targetname").value_or("").empty() ||
                                                 !entity.find("master").value_or("").empty(),
                                      .isUseOnly = (flags & kUseOnly) != 0,
                                      .staysOpen = (flags & kStaysOpen) != 0,
                                      .isTouchable = (flags & kUseOnly) == 0},
                  .origin = origin};
    const float speed = entity.findNumber("speed").value_or(0.0F);
    made.door.speed = speed == 0.0F ? kDoorSpeed : UnitsPerSecond{speed};
    if ((flags & kStartsOpen) != 0) {
        // Parity: it starts where it would open to, and opens back to where the map put it.
        made.origin = made.door.open;
        made.door.open = made.door.closed;
        made.door.closed = made.origin;
    }
    return made;
}

} // namespace

auto readEntityClasses(std::string_view text, std::string_view sourceName)
    -> Result<EntityClasses, GameError> {
    auto classes = data::parseToml<EntityClasses>(text, sourceName);
    if (!classes) {
        return fail(GameError::ConfigInvalid, std::string{sourceName}, std::move(classes.error()));
    }
    std::set<std::string_view> seen;
    for (const BrushClass& brushClass : classes->brushClasses) {
        if (!seen.insert(brushClass.classname).second) {
            return fail(GameError::ConfigInvalid,
                        std::format("{}: the brush class {:?} twice", sourceName, brushClass.classname));
        }
    }
    return std::move(*classes);
}

auto spawnBrushEntities(ecs::World& world, std::span<const assets::BspEntity> entities,
                        std::span<const assets::BspModel> models, const EntityClasses& classes)
    -> Result<std::size_t, GameError> {
    std::size_t spawned = 0;
    for (const assets::BspEntity& entity : entities) {
        const std::optional<std::uint32_t> model = entity.findBrushModel();
        const std::string_view classname = entity.find("classname").value_or("");
        const auto brushClass = std::ranges::find(classes.brushClasses, classname, &BrushClass::classname);
        if (!model || brushClass == classes.brushClasses.end()) {
            continue;
        }
        if (*model == 0 || *model >= models.size()) {
            return fail(GameError::UnknownModel,
                        std::format("{} \"*{}\" of {} brush models", classname, *model, models.size()));
        }
        const std::int32_t contents = brushClass->contents.value_or(entity.findInteger("skin").value_or(0));
        const std::int32_t flags = entity.findInteger("spawnflags").value_or(0);
        const bool isNotSolid = (flags & brushClass->notSolidFlags.value_or(0)) != 0 ||
                                (brushClass->skinMakesNotSolid.value_or(false) && contents != 0);
        const BrushSolidity solidity{.solid = isNotSolid ? Solid::Not : brushClass->solid,
                                     .contents = contents};
        const Vec3 origin = entity.findVector("origin").value_or(Vec3{});
        if (brushClass->behavior == BrushBehavior::SlidingDoor) {
            const MadeDoor made = slidingDoorOf(entity, models[*model].bounds, origin);
            std::ignore = world.spawn(BrushModel{.index = *model}, Transform{.origin = made.origin}, solidity,
                                      Pusher{}, made.door);
        } else {
            std::ignore = world.spawn(BrushModel{.index = *model}, Transform{.origin = origin}, solidity);
        }
        ++spawned;
    }
    return spawned;
}

} // namespace nx::game
