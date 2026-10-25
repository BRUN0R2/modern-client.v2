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
constexpr std::int32_t kTurnsBackwards = 2;
constexpr std::int32_t kTurnsOneWay = 16;
constexpr std::int32_t kStaysOpen = 32; ///< "Toggle": the original's SF_DOOR_NO_AUTO_RETURN.
constexpr std::int32_t kTurnsAboutRoll = 64;
constexpr std::int32_t kTurnsAboutPitch = 128;
constexpr std::int32_t kUseOnly = 256;

/// A door's speed when its "speed" is 0 or absent: units a second, or degrees when it turns.
constexpr float kDoorSpeed = 100.0F;

/// A door's waves rise by this share of its "WaveHeight" (CBaseDoor::KeyValue).
constexpr float kWaveHeightShare = 1.0F / 8.0F;

/// The game takes the engine's growth of a brush model's bounds (kLoadedBoundsGrowth) back from a
/// door's size.
constexpr float kDoorSizeTrim = 2.0F;

// The spawnflags of a button the game reads.
constexpr std::int32_t kStaysPut = 1;         ///< SF_BUTTON_DONTMOVE.
constexpr std::int32_t kButtonToggles = 32;   ///< SF_BUTTON_TOGGLE.
constexpr std::int32_t kPressedByTouch = 256; ///< SF_BUTTON_TOUCH_ONLY.
/// A button's "speed", "wait" and "lip" when it gives none (CBaseButton::Spawn); a "wait" of -1 keeps
/// it in for good.
constexpr float kButtonSpeed = 40.0F;
constexpr Seconds kButtonWait{1.0F};
constexpr float kButtonLip = 4.0F;
constexpr Seconds kStaysInWait{-1.0F};
/// A button that would move less than this does not move.
constexpr float kLeastButtonTravel = 1.0F;

/// The spawnflag that keeps players from firing a trigger (SF_TRIGGER_NOCLIENTS).
constexpr std::int32_t kNotByPlayers = 2;
/// A trigger_multiple's "wait" when it gives none (CTriggerMultiple::Spawn), and a trigger_once's
/// (CTriggerOnce::Spawn): it fires once.
constexpr Seconds kDefaultTriggerWait{0.2F};
constexpr Seconds kFiresOnce{-1.0F};

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
    // Parity: the original compares the angle cut to a whole number.
    return Vec3{.x = std::trunc(angle) == kUp ? -kQuarterTurn : kQuarterTurn};
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
    return angleVectors(eulerOf(angles)).forward;
}

/// A door of `entity` as the game makes it, and where it starts: its origin, and its angles.
struct MadeDoor {
    Door door{};
    Vec3 origin{};
    EulerAngles angles{};
};

/// What opens and closes a door of `entity`, and how long it stays open.
[[nodiscard]] auto doorOf(const assets::BspEntity& entity, std::int32_t flags) -> Door {
    return Door{.wait = Seconds{entity.findNumber("wait").value_or(0.0F)},
                .isNamed = !entity.find("targetname").value_or("").empty() ||
                           !entity.find("master").value_or("").empty(),
                .isUseOnly = (flags & kUseOnly) != 0,
                .staysOpen = (flags & kStaysOpen) != 0,
                .isTouchable = (flags & kUseOnly) == 0};
}

[[nodiscard]] auto speedOf(const assets::BspEntity& entity) -> float {
    const float speed = entity.findNumber("speed").value_or(0.0F);
    return speed == 0.0F ? kDoorSpeed : speed;
}

/// How far a mover of a model of `bounds` goes along `direction` (CBaseDoor::Spawn,
/// CBaseButton::Spawn): its loaded size along the way it goes, less 2 and its `lip`.
[[nodiscard]] auto travelOf(Vec3 direction, const Aabb& bounds, float lip) -> float {
    const Vec3 growth{kLoadedBoundsGrowth, kLoadedBoundsGrowth, kLoadedBoundsGrowth};
    const Vec3 size = (bounds.max + growth) - (bounds.min - growth);
    const double travel = std::abs(double{direction.x * (size.x - kDoorSizeTrim)}) +
                          std::abs(double{direction.y * (size.y - kDoorSizeTrim)}) +
                          std::abs(double{direction.z * (size.z - kDoorSizeTrim)}) - double{lip};
    return static_cast<float>(travel);
}

/// A sliding door of `entity`, its model's bounds `bounds`, at `origin`, as CBaseDoor::Spawn makes it.
[[nodiscard]] auto slidingDoorOf(const assets::BspEntity& entity, const Aabb& bounds, Vec3 origin)
    -> MadeDoor {
    const std::int32_t flags = entity.findInteger("spawnflags").value_or(0);
    const Vec3 direction = moveDirectionOf(anglesOf(entity));
    const float travel = travelOf(direction, bounds, entity.findNumber("lip").value_or(0.0F));
    DoorSlide slide{
        .closed = origin, .open = origin + (direction * travel), .speed = UnitsPerSecond{speedOf(entity)}};
    Vec3 start = origin;
    if ((flags & kStartsOpen) != 0) {
        // It starts where it would open to, and opens back to where the map put it.
        start = slide.open;
        slide.open = slide.closed;
        slide.closed = start;
    }
    MadeDoor made{.door = doorOf(entity, flags), .origin = start};
    made.door.motion = slide;
    return made;
}

/// A rotating door of `entity` at `origin`, as CRotDoor::Spawn makes it: turning about the yaw
/// axis, or the roll or pitch one, by its "distance" in degrees from the angles the map gives it.
[[nodiscard]] auto rotatingDoorOf(const assets::BspEntity& entity, Vec3 origin) -> MadeDoor {
    const std::int32_t flags = entity.findInteger("spawnflags").value_or(0);
    Vec3 axis = (flags & kTurnsAboutRoll) != 0    ? Vec3{.z = 1.0F}
                : (flags & kTurnsAboutPitch) != 0 ? Vec3{.x = 1.0F}
                                                  : Vec3{.y = 1.0F};
    if ((flags & kTurnsBackwards) != 0) {
        axis = axis * -1.0F;
    }
    const Vec3 closed = anglesOf(entity);
    const Vec3 open = closed + (axis * entity.findNumber("distance").value_or(0.0F));
    DoorTurn turn{
        .closed = eulerOf(closed), .open = eulerOf(open), .speed = DegreesPerSecond{speedOf(entity)}};
    Vec3 start = closed;
    if ((flags & kStartsOpen) != 0) {
        // Parity: the original puts it open but swaps its ends wrongly, both closed then.
        start = open;
        turn.open = turn.closed;
        axis = axis * -1.0F;
    }
    turn.opensAway = (flags & kTurnsOneWay) == 0 && axis.y != 0.0F;
    MadeDoor made{.door = doorOf(entity, flags), .origin = origin, .angles = eulerOf(start)};
    made.door.motion = turn;
    return made;
}

/// The button of `entity`, its model's bounds `bounds`, at `origin`, as CBaseButton::Spawn makes it.
[[nodiscard]] auto buttonOf(const assets::BspEntity& entity, const Aabb& bounds, Vec3 origin,
                            std::int32_t flags) -> Button {
    const float speed = entity.findNumber("speed").value_or(0.0F);
    const Seconds wait{entity.findNumber("wait").value_or(0.0F)};
    const float lip = entity.findNumber("lip").value_or(0.0F);
    const Vec3 direction = moveDirectionOf(anglesOf(entity));
    Vec3 in = origin + (direction * travelOf(direction, bounds, lip == 0.0F ? kButtonLip : lip));
    if (length(in - origin) < kLeastButtonTravel || (flags & kStaysPut) != 0) {
        in = origin;
    }
    const bool isTouchOnly = (flags & kPressedByTouch) != 0;
    return Button{.out = origin,
                  .in = in,
                  .speed = UnitsPerSecond{speed == 0.0F ? kButtonSpeed : speed},
                  .wait = wait == Seconds{} ? kButtonWait : wait,
                  .staysIn = wait == kStaysInWait,
                  .isToggle = (flags & kButtonToggles) != 0,
                  .isTouchOnly = isTouchOnly,
                  .isUsable = entity.findNumber("health").value_or(0.0F) <= 0.0F,
                  .isTouchable = isTouchOnly};
}

/// What `entity` fires (CBaseDelay::KeyValue): its "target", "killtarget" and "delay".
[[nodiscard]] auto targetsOf(const assets::BspEntity& entity) -> Targets {
    return Targets{.target = std::string{entity.find("target").value_or("")},
                   .killTarget = std::string{entity.find("killtarget").value_or("")},
                   .delay = Seconds{entity.findNumber("delay").value_or(0.0F)}};
}

/// The trigger of `entity`, of `behavior` (CTriggerMultiple::Spawn, CTriggerOnce::Spawn).
[[nodiscard]] auto triggerOf(const assets::BspEntity& entity, BrushBehavior behavior, std::int32_t flags)
    -> Trigger {
    const Seconds wait{entity.findNumber("wait").value_or(0.0F)};
    return Trigger{.wait = behavior == BrushBehavior::TriggerOnce ? kFiresOnce
                           : wait == Seconds{}                    ? kDefaultTriggerWait
                                                                  : wait,
                   .isTouchedByPlayers = (flags & kNotByPlayers) == 0};
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
        const std::optional<assets::ModelIndex> model = entity.findBrushModel();
        const std::string_view classname = entity.find("classname").value_or("");
        const auto brushClass = std::ranges::find(classes.brushClasses, classname, &BrushClass::classname);
        if (!model || brushClass == classes.brushClasses.end()) {
            continue;
        }
        if (*model == assets::kWorldModel || model->value >= models.size()) {
            return fail(GameError::UnknownModel,
                        std::format("{} \"*{}\" of {} brush models", classname, model->value, models.size()));
        }
        const std::int32_t skin = brushClass->contents.value_or(entity.findInteger("skin").value_or(0));
        const std::int32_t flags = entity.findInteger("spawnflags").value_or(0);
        const bool isNotSolid = (flags & brushClass->notSolidFlags.value_or(0)) != 0 ||
                                (brushClass->skinMakesNotSolid.value_or(false) && skin != 0);
        const BrushSolidity solidity{.solid = isNotSolid ? Solid::Not : brushClass->solid,
                                     .contents = assets::contentsNumbered(skin)};
        const Vec3 origin = entity.findVector("origin").value_or(Vec3{});
        ecs::Entity made{};
        switch (brushClass->behavior.value_or(BrushBehavior::Count)) {
        case BrushBehavior::SlidingDoor:
        case BrushBehavior::RotatingDoor: {
            const MadeDoor door = *brushClass->behavior == BrushBehavior::SlidingDoor
                                      ? slidingDoorOf(entity, models[model->value].bounds, origin)
                                      : rotatingDoorOf(entity, origin);
            made = world.spawn(BrushModel{.index = *model},
                               Transform{.origin = door.origin, .angles = door.angles}, solidity, Pusher{},
                               door.door, targetsOf(entity),
                               Waves{.height = HammerUnits{entity.findNumber("WaveHeight").value_or(0.0F) *
                                                           kWaveHeightShare}});
            break;
        }
        case BrushBehavior::Button:
            made =
                world.spawn(BrushModel{.index = *model}, Transform{.origin = origin}, solidity, Pusher{},
                            buttonOf(entity, models[model->value].bounds, origin, flags), targetsOf(entity));
            break;
        case BrushBehavior::Wall:
            made = world.spawn(BrushModel{.index = *model}, Transform{.origin = origin}, solidity, Wall{});
            break;
        case BrushBehavior::Trigger:
        case BrushBehavior::TriggerOnce:
            made = world.spawn(BrushModel{.index = *model}, Transform{.origin = origin}, solidity,
                               triggerOf(entity, *brushClass->behavior, flags), targetsOf(entity));
            break;
        case BrushBehavior::Count:
            made = world.spawn(BrushModel{.index = *model}, Transform{.origin = origin}, solidity);
            break;
        }
        world.add(made, AlternateTextures{.isShown = entity.findNumber("frame").value_or(0.0F) != 0.0F});
        if (const std::string_view name = entity.find("targetname").value_or(""); !name.empty()) {
            world.add(made, Named{.name = std::string{name}});
        }
        ++spawned;
    }
    return spawned;
}

} // namespace nx::game
