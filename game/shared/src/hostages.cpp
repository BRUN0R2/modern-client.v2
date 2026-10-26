// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.ecs;
import nx.collision;
import nx.game;

namespace cs {

namespace {

/// How much an entity's box grows each way as the engine links it (SV_LinkEdict), and a line's as it
/// bounds the way (SV_MoveBounds).
constexpr float kLinkMargin = 1.0F;
/// A hostage thinks again this long after it thought (IdleThink's giveUpTime).
constexpr nx::Seconds kThinkInterval{1.0F / 30.0F};
/// A hostage made or put back thinks first after this long at least, and at most.
constexpr nx::Seconds kLeastFirstThink{0.1F};
constexpr nx::Seconds kMostFirstThink{0.2F};
/// The controllers the game sets straight ahead for a hostage: its four channels; the mouth's is not
/// an entity's controller.
constexpr std::uint32_t kHostageControllers = 4;
/// The streams a hostage's draws come from as it is made, and as the hostages are put back.
constexpr std::string_view kSpawnStream = "hostage spawn";
constexpr std::string_view kRepositionStream = "hostage reposition";

/// The hostages of `world` in the map's order, as the original's entities are.
[[nodiscard]] auto hostagesOf(nx::ecs::World& world) -> std::vector<nx::ecs::Entity> {
    std::vector<std::pair<std::uint32_t, nx::ecs::Entity>> found;
    nx::ecs::Query<nx::ecs::Entity, const Hostage&>{world}.forEach(
        [&found](nx::ecs::Entity entity, const Hostage& hostage) {
            found.emplace_back(hostage.spawn, entity);
        });
    std::ranges::sort(found, {}, &std::pair<std::uint32_t, nx::ecs::Entity>::first);
    return found | std::views::values | std::ranges::to<std::vector>();
}

/// The models the hostages of `world` play.
[[nodiscard]] auto modelsOf(const nx::ecs::World& world) noexcept -> std::span<const PlayedModel> {
    const HostageModels* const models = world.resource<HostageModels>();
    nx::expects(models != nullptr, "cs: a world registerHostages made ready");
    return models != nullptr ? models->models : std::span<const PlayedModel>{};
}

/// When a hostage made or put back at `now` first thinks: 0.1 to 0.2 seconds on, drawn from `random`
/// (RANDOM_FLOAT).
[[nodiscard]] auto firstThinkOf(nx::GameTime now, nx::TickRate rate, nx::Random& random) noexcept
    -> nx::GameTime {
    const float drawn = kLeastFirstThink.count() + (static_cast<float>(random.nextUnit()) *
                                                    (kMostFirstThink.count() - kLeastFirstThink.count()));
    return now.after(nx::Seconds{drawn}, rate);
}

/// The controllers of a hostage of `model`: its head straight ahead, 0 degrees (CHostage::Spawn,
/// SetBoneController).
[[nodiscard]] auto controllersOf(const nx::assets::MdlView& model) -> ControllerSettings {
    ControllerSettings settings;
    for (std::uint32_t channel = 0; channel < kHostageControllers; ++channel) {
        const auto found =
            std::ranges::find(model.boneControllers(), channel, &nx::assets::BoneController::channel);
        if (found != model.boneControllers().end()) {
            settings.channels.at(channel) = controllerSettingOf(*found, 0.0F);
        }
    }
    return settings;
}

/// `hostage` set to do `activity` at `now`, playing `model`, as CHostage::SetActivity sets it: unless it
/// does it already, the sequence lookupActivity draws from `random` for it, from its first frame when it
/// is another sequence (a walk and a run go on into each other, with following), at its own speed from
/// `now`; nothing when the model has no sequence for it.
void setActivity(Hostage& hostage, Animation& animation, const PlayedModel& model, Activity activity,
                 nx::GameTime now, nx::Random& random) {
    if (hostage.activity == activity) {
        return;
    }
    const std::span<const nx::assets::StudioSequence> sequences = model.view->sequences();
    const std::optional<nx::assets::SequenceIndex> sequence = lookupActivity(sequences, activity, random);
    if (!sequence) {
        return;
    }
    if (animation.sequence != *sequence) {
        animation.frame = 0.0F;
        animation.sequence = *sequence;
    }
    hostage.activity = activity;
    resetSequence(animation, sequences[sequence->value], now);
}

/// A hostage's think (CHostage::IdleThink), at `at`: again a thirtieth of a second on, its frame
/// advanced to `at`. What it does every tenth of a second (following, rescue, the activity its speed
/// gives it, the radar) comes with what moves it.
void thinkHostage(nx::ecs::World& world, const nx::collision::BrushHulls& /*hulls*/, nx::ecs::Entity entity,
                  nx::GameTime at) {
    const nx::TickRate rate = nx::game::tickTimeOf(world).rate;
    nx::game::setNextThink(world, entity, at.after(kThinkInterval, rate));
    if (Animation* const animation = world.get<Animation>(entity)) {
        advanceFrame(*animation, at, rate);
    }
}

/// The bones of `hostage`, playing `model` as `animation` says, where `transform` places it, as the
/// server poses them for its traces (SV_StudioSetupBones): at its frame, its sequence's motion kept,
/// turned by its angles, the pitch negated as the engine's studio transform negates it (R_StudioHull).
[[nodiscard]] auto serverBonesOf(const Hostage& hostage, const Animation& animation, const PlayedModel& model,
                                 const nx::game::Transform& transform) -> std::vector<nx::Mat3x4> {
    const nx::assets::StudioSequence& sequence = model.view->sequences()[animation.sequence.value];
    const nx::EulerAngles turn{
        .pitch = -transform.angles.pitch, .yaw = transform.angles.yaw, .roll = transform.angles.roll};
    return poseBones(skeletonOf(*model.view), sequence, model.animations[animation.sequence.value],
                     {.frame = serverFrameOf(sequence, animation.frame),
                      .controllers = hostage.controllers,
                      .motion = SequenceMotion::Kept,
                      .placement = nx::affine(nx::rotationMatrix(turn), transform.origin)});
}

/// The server's way of `hull` from `start` to `end` through `hostage` alone, before what it met is
/// weighed; none for a model without hitboxes, which a line does not meet.
[[nodiscard]] auto traceHostage(const Hostage& hostage, const Animation& animation, const PlayedModel& model,
                                const nx::game::Transform& transform, nx::collision::Hull hull,
                                nx::Vec3 start, nx::Vec3 end) -> std::optional<nx::collision::Trace> {
    if (hull != nx::collision::Hull::Point) {
        return nx::collision::traceBox(nx::collision::grownByHull(kHostageBox, hull), transform.origin, start,
                                       end);
    }
    if (model.view->hitboxes().empty()) {
        return std::nullopt; // not met, where the original's would be all in the solid: the game's have them
    }
    const std::vector<nx::Mat3x4> bones = serverBonesOf(hostage, animation, model, transform);
    return nx::collision::traceHitboxes(placedHitboxesOf(model.view->hitboxes(), bones), start, end).trace;
}

} // namespace

auto registerHostages(nx::ecs::World& world, std::span<const PlayedModel> models)
    -> nx::Result<void, nx::ecs::ComponentError> {
    const std::array registered{world.registerComponent<Hostage>("cs::Hostage"),
                                world.registerComponent<Animation>("cs::Animation")};
    for (const auto& component : registered) {
        if (!component) {
            return std::unexpected{component.error()};
        }
    }
    world.insertResource(HostageModels{.models = models});
    return {};
}

void spawnHostages(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                   std::span<const HostageSpawn> spawns) {
    const std::span<const PlayedModel> models = modelsOf(world);
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    for (std::size_t index = 0; index < spawns.size(); ++index) {
        const HostageSpawn& spawn = spawns[index];
        if (spawn.model >= models.size()) {
            continue;
        }
        const PlayedModel& model = models[spawn.model];
        nx::Random random{index, kSpawnStream};
        Hostage hostage{.spawn = nx::narrow<std::uint32_t>(index),
                        .model = spawn.model,
                        .startAngles = spawn.angles,
                        .controllers = controllersOf(*model.view)};
        Animation animation{};
        setActivity(hostage, animation, model, Activity::Idle, time.now, random);
        hostage.start = nx::game::droppedToFloor(world, hulls, kHostageBox, spawn.origin);
        const nx::ecs::Entity made =
            world.spawn(nx::game::Transform{.origin = hostage.start, .angles = spawn.angles}, hostage,
                        animation, nx::game::ThinkMethod{.think = &thinkHostage});
        nx::game::setNextThink(world, made, firstThinkOf(time.now, time.rate, random));
    }
}

void repositionHostages(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                        const HostageBonus& bonus) {
    const std::span<const PlayedModel> models = modelsOf(world);
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    nx::Random random{static_cast<std::uint64_t>(time.now.tick()), kRepositionStream};
    nx::Money counted{};
    for (const nx::ecs::Entity entity : hostagesOf(world)) {
        if (counted >= bonus.upTo) {
            break;
        }
        // Each is still held: none is rescued or dead yet.
        counted = nx::addSaturated(counted, bonus.perHostage);
        Hostage& hostage = *world.get<Hostage>(entity);
        nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
        transform.origin = nx::game::droppedToFloor(world, hulls, kHostageBox, hostage.start);
        transform.angles = hostage.startAngles;
        if (hostage.model < models.size()) {
            setActivity(hostage, *world.get<Animation>(entity), models[hostage.model], Activity::Idle,
                        time.now, random);
        }
        nx::game::setNextThink(world, entity, firstThinkOf(time.now, time.rate, random));
    }
}

auto hostageBoxes(nx::ecs::World& world) -> std::vector<SolidBox> {
    std::vector<SolidBox> boxes;
    for (const nx::ecs::Entity entity : hostagesOf(world)) {
        boxes.push_back(
            SolidBox{.origin = world.get<nx::game::Transform>(entity)->origin, .box = kHostageBox});
    }
    return boxes;
}

auto traceHostages(nx::ecs::World& world, nx::collision::Hull hull, nx::Vec3 start, nx::Vec3 end,
                   const nx::collision::Trace& sofar) -> std::optional<nx::collision::Trace> {
    const std::span<const PlayedModel> models = modelsOf(world);
    const nx::Vec3 margin{.x = kLinkMargin, .y = kLinkMargin, .z = kLinkMargin};
    const nx::Aabb traced = nx::collision::hullBoxOf(hull);
    const nx::Aabb way{.min = nx::componentMin(start, end) + traced.min - margin,
                       .max = nx::componentMax(start, end) + traced.max + margin};
    nx::collision::Trace kept = sofar;
    bool isMet = false;
    for (const nx::ecs::Entity entity : hostagesOf(world)) {
        const Hostage& hostage = *world.get<Hostage>(entity);
        const nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
        if (hostage.model >= models.size() ||
            !way.intersects(kHostageBox.translated(transform.origin).expanded(margin))) {
            continue;
        }
        const std::optional<nx::collision::Trace> hit = traceHostage(
            hostage, *world.get<Animation>(entity), models[hostage.model], transform, hull, start, end);
        if (hit && nx::collision::keepFirstStop(kept, *hit)) {
            isMet = true;
        }
    }
    return isMet ? std::optional{kept} : std::nullopt;
}

auto hostagesShownAt(nx::ecs::World& world, nx::GameTime time) -> std::vector<ShownHostage> {
    const std::span<const PlayedModel> models = modelsOf(world);
    const nx::TickRate rate = nx::game::tickTimeOf(world).rate;
    std::vector<ShownHostage> shown;
    for (const nx::ecs::Entity entity : hostagesOf(world)) {
        const Hostage& hostage = *world.get<Hostage>(entity);
        if (hostage.model >= models.size()) {
            continue;
        }
        const Animation& animation = *world.get<Animation>(entity);
        const nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
        const nx::assets::StudioSequence& sequence =
            models[hostage.model].view->sequences()[animation.sequence.value];
        shown.push_back(ShownHostage{.spawn = hostage.spawn,
                                     .origin = transform.origin,
                                     .angles = transform.angles,
                                     .sequence = animation.sequence,
                                     .frame = estimatedFrameOf(sequence, animation.frame,
                                                               time.since(animation.advancedAt, rate),
                                                               animation.framerate),
                                     .controllers = hostage.controllers});
    }
    return shown;
}

} // namespace cs
