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

/// A hostage thinks again this long after it thought (IdleThink's giveUpTime), and its tenth of a
/// second's part this long after it last did.
constexpr nx::Seconds kThinkInterval{1.0F / 30.0F};
constexpr nx::Seconds kFullThinkInterval{0.1F};
/// A hostage made or put back thinks first after this long at least, and at most.
constexpr nx::Seconds kLeastFirstThink{0.1F};
constexpr nx::Seconds kMostFirstThink{0.2F};
/// A hostage hurt flinches this long (m_flFlinchTime).
constexpr nx::Seconds kFlinchTime{0.75F};
/// The controllers the game sets straight ahead for a hostage: its four channels; the mouth's is not
/// an entity's controller.
constexpr std::uint32_t kHostageControllers = 4;
/// The streams a hostage's draws come from as it is made, and as the hostages are put back.
constexpr std::string_view kSpawnStream = "hostage spawn";
constexpr std::string_view kRepositionStream = "hostage reposition";
/// The stream of the draws of a hostage's think and of an attack on it, of its tick and the hostage.
constexpr std::string_view kTickStream = "hostage tick";

/// The draws of `hostage` at `now`: of its tick and of which of the map's it is.
[[nodiscard]] auto randomOf(const Hostage& hostage, nx::GameTime now) noexcept -> nx::Random {
    constexpr unsigned kHostageBits = 32;
    return nx::Random{(static_cast<std::uint64_t>(now.tick()) << kHostageBits) ^ hostage.spawn, kTickStream};
}

/// The death a hostage met in hit group `group` plays, as Counter-Strike 1.6's SetDeathActivity
/// chooses it; none for a group it does not name.
[[nodiscard]] auto deathOf(HitGroup group) noexcept -> std::optional<Activity> {
    switch (group) {
    case HitGroup::Generic:
    case HitGroup::Head:
        return Activity::DieHeadshot;
    case HitGroup::Chest:
    case HitGroup::RightArm:
        return Activity::DieSimple;
    case HitGroup::Stomach:
    case HitGroup::RightLeg:
        return Activity::DieForward;
    case HitGroup::LeftArm:
    case HitGroup::LeftLeg:
        return Activity::DieBackward;
    default:
        return std::nullopt;
    }
}

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

/// What the hostages of `world` play and take attacks by.
[[nodiscard]] auto dataOf(const nx::ecs::World& world) noexcept -> const HostageData& {
    static constexpr HostageData kNone{};
    const HostageData* const data = world.resource<HostageData>();
    nx::expects(data != nullptr, "cs: a world registerHostages made ready");
    return data != nullptr ? *data : kNone;
}

/// The models the hostages of `world` play.
[[nodiscard]] auto modelsOf(const nx::ecs::World& world) noexcept -> std::span<const PlayedModel> {
    return dataOf(world).models;
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
/// advanced to `at`; then, a tenth of a second after its part of a tenth of a second last ran, that
/// part: idling once it no longer flinches (the activity its speed gives it, still). Following, rescue
/// and the radar come with what moves it.
void thinkHostage(nx::ecs::World& world, const nx::collision::BrushHulls& /*hulls*/, nx::ecs::Entity entity,
                  nx::GameTime at) {
    const nx::TickRate rate = nx::game::tickTimeOf(world).rate;
    nx::game::setNextThink(world, entity, at.after(kThinkInterval, rate));
    Hostage* const hostage = world.get<Hostage>(entity);
    Animation* const animation = world.get<Animation>(entity);
    if (hostage == nullptr || animation == nullptr) {
        return;
    }
    advanceFrame(*animation, at, rate);
    if (hostage->nextFullThink > at) {
        return;
    }
    hostage->nextFullThink = at.after(kFullThinkInterval, rate);
    const std::span<const PlayedModel> models = modelsOf(world);
    if (at >= hostage->flinchUntil && hostage->model < models.size()) {
        nx::Random random = randomOf(*hostage, at);
        setActivity(*hostage, *animation, models[hostage->model], Activity::Idle, at, random);
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

/// A trace of a box `traced` from `start` to `end` through hostage `entity` of `world` (its
/// ClipMethod), as SV_HullForStudioModel chooses: a line through its hitboxes, its bones posed where its
/// last think left its frame with its sequence's motion kept, all in solid and not stopped for a model
/// without any, as the original's loop over none leaves it; a box through its box grown by the one
/// traced.
[[nodiscard]] auto clipHostage(nx::ecs::World& world, nx::ecs::Entity entity, const nx::Aabb& traced,
                               nx::Vec3 start, nx::Vec3 end) -> nx::collision::Trace {
    const nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
    const nx::Aabb box{.min = kHostageBox.min - traced.max, .max = kHostageBox.max - traced.min};
    const std::span<const PlayedModel> models = modelsOf(world);
    const Hostage& hostage = *world.get<Hostage>(entity);
    if (traced.max != traced.min || hostage.model >= models.size()) {
        return nx::collision::traceBox(box, transform.origin, start, end);
    }
    const PlayedModel& model = models[hostage.model];
    if (model.view->hitboxes().empty()) {
        return nx::collision::Trace{.fraction = 1.0F, .end = end, .isAllSolid = true};
    }
    const std::vector<nx::Mat3x4> bones =
        serverBonesOf(hostage, *world.get<Animation>(entity), model, transform);
    const nx::collision::HitboxTrace hit =
        nx::collision::traceHitboxes(placedHitboxesOf(model.view->hitboxes(), bones), start, end);
    nx::collision::Trace met = hit.trace;
    met.hitGroup = model.view->hitboxes()[hit.hitbox].group;
    return met;
}

/// `hostage`, of `entity`, solid as its box again and taking attacks (made whole, or put back).
void makeSolid(nx::ecs::World& world, nx::ecs::Entity entity) {
    if (!world.has<nx::game::BoxSolidity>(entity)) {
        world.add(entity, nx::game::BoxSolidity{.box = kHostageBox});
        world.add(entity, nx::game::ClipMethod{.clip = &clipHostage});
    }
}

} // namespace

auto harmHostage(nx::ecs::World& world, nx::ecs::Entity entity, const nx::game::Attack& attack)
    -> nx::game::Harm {
    Hostage& hostage = *world.get<Hostage>(entity);
    if (hostage.isDead) {
        return {};
    }
    // TraceAttack, then TakeDamage.
    hostage.lastHitGroup = hitGroupOf(attack.trace);
    const HitGroupRules* const shares = dataOf(world).hitGroups;
    const nx::Damage dealt = shares != nullptr
                                 ? scaled(scaled(attack.damage, shares->monster.of(hostage.lastHitGroup)),
                                          shares->hostage.of(hostage.lastHitGroup))
                                 : attack.damage;
    const nx::Damage taken{std::min(dealt.value(), hostage.health.value())};
    hostage.health = hostage.health - taken;
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    const std::span<const PlayedModel> models = modelsOf(world);
    nx::Random random = randomOf(hostage, time.now);
    Animation& animation = *world.get<Animation>(entity);
    if (hostage.health > nx::HitPoints{}) {
        hostage.flinchUntil = time.now.after(kFlinchTime, time.rate);
        if (hostage.model < models.size()) {
            setActivity(hostage, animation, models[hostage.model], Activity::SmallFlinch, time.now, random);
        }
        return nx::game::Harm{.damage = taken};
    }
    hostage.health = nx::HitPoints{};
    hostage.isDead = true;
    if (const std::optional<Activity> death = deathOf(hostage.lastHitGroup);
        death && hostage.model < models.size()) {
        setActivity(hostage, animation, models[hostage.model], *death, time.now, random);
    }
    // No longer solid (SOLID_NOT), nor thinking.
    world.remove<nx::game::BoxSolidity>(entity);
    world.remove<nx::game::ClipMethod>(entity);
    world.remove<nx::game::NextThink>(entity);
    return nx::game::Harm{.damage = taken, .isKilled = true};
}

auto hostageHarmCost(const nx::game::Harm& harm, const HostageHarmCosts& costs) noexcept -> nx::Money {
    // The account loses its cost for each whole point of the damage (int()).
    const auto points = static_cast<std::int32_t>(harm.damage.value());
    const nx::Money damaged{nx::mulSaturated(costs.perDamage.value(), points)};
    return harm.isKilled ? nx::addSaturated(damaged, costs.perKill) : damaged;
}

auto registerHostages(nx::ecs::World& world, std::span<const PlayedModel> models,
                      const HitGroupRules& hitGroups) -> nx::Result<void, nx::ecs::ComponentError> {
    const std::array registered{world.registerComponent<Hostage>("cs::Hostage"),
                                world.registerComponent<Animation>("cs::Animation")};
    for (const auto& component : registered) {
        if (!component) {
            return std::unexpected{component.error()};
        }
    }
    world.insertResource(HostageData{.models = models, .hitGroups = &hitGroups});
    return {};
}

auto mapHostagesOf(const nx::assets::BspView& map) -> std::vector<MapHostage> {
    std::vector<MapHostage> hostages;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        if (entity.find("classname") != kHostageClass) {
            continue;
        }
        const nx::Vec3 angles = nx::game::anglesOf(entity);
        const std::string_view model = entity.find("model").value_or("");
        hostages.push_back(MapHostage{
            .model = std::string{model.empty() ? kDefaultHostageModel : model},
            .origin = entity.findVector("origin").value_or(nx::Vec3{}),
            .angles = {.pitch = nx::Degrees{angles.x},
                       .yaw = nx::Degrees{angles.y},
                       .roll = nx::Degrees{angles.z}},
            .skin = static_cast<std::uint32_t>(std::max(entity.findInteger("skin").value_or(0), 0))});
    }
    return hostages;
}

void spawnHostages(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                   std::span<const HostageSpawn> spawns) {
    const std::span<const PlayedModel> models = modelsOf(world);
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    for (const HostageSpawn& spawn : spawns) {
        if (spawn.model >= models.size()) {
            continue;
        }
        const PlayedModel& model = models[spawn.model];
        nx::Random random{spawn.spawn, kSpawnStream};
        Hostage hostage{.spawn = spawn.spawn,
                        .model = spawn.model,
                        .startAngles = spawn.angles,
                        .controllers = controllersOf(*model.view)};
        Animation animation{};
        setActivity(hostage, animation, model, Activity::Idle, time.now, random);
        hostage.start = nx::game::droppedToFloor(world, hulls, kHostageBox, spawn.origin, std::nullopt);
        const nx::GameTime firstThink = firstThinkOf(time.now, time.rate, random);
        hostage.nextFullThink = firstThinkOf(time.now, time.rate, random);
        const nx::ecs::Entity made = world.spawn(
            nx::game::Transform{.origin = hostage.start, .angles = spawn.angles}, hostage, animation,
            nx::game::BoxSolidity{.box = kHostageBox}, nx::game::ClipMethod{.clip = &clipHostage},
            nx::game::DamageMethod{.takeAttack = &harmHostage},
            nx::game::Classified{.classification = nx::game::Classification::HumanPassive},
            nx::game::ThinkMethod{.think = &thinkHostage});
        nx::game::setNextThink(world, made, firstThink);
    }
}

auto repositionHostages(nx::ecs::World& world, const nx::collision::BrushHulls& hulls,
                        const HostageBonus& bonus) -> std::uint32_t {
    const std::span<const PlayedModel> models = modelsOf(world);
    const nx::game::TickTime time = nx::game::tickTimeOf(world);
    nx::Random random{static_cast<std::uint64_t>(time.now.tick()), kRepositionStream};
    nx::Money counted{};
    std::uint32_t held = 0;
    for (const nx::ecs::Entity entity : hostagesOf(world)) {
        if (counted >= bonus.upTo) {
            break;
        }
        // A hostage still held counts: one alive, none being rescued yet.
        if (!world.get<Hostage>(entity)->isDead) {
            counted = nx::addSaturated(counted, bonus.perHostage);
            ++held;
        }
        makeSolid(world, entity);
        Hostage& hostage = *world.get<Hostage>(entity);
        hostage.health = kHostageHealth;
        hostage.isDead = false;
        nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
        transform.origin = nx::game::droppedToFloor(world, hulls, kHostageBox, hostage.start, entity);
        transform.angles = hostage.startAngles;
        if (hostage.model < models.size()) {
            setActivity(hostage, *world.get<Animation>(entity), models[hostage.model], Activity::Idle,
                        time.now, random);
        }
        const nx::GameTime firstThink = firstThinkOf(time.now, time.rate, random);
        hostage.nextFullThink = firstThinkOf(time.now, time.rate, random);
        nx::game::setNextThink(world, entity, firstThink);
    }
    return held;
}

auto hostageStatesOf(nx::ecs::World& world) -> std::vector<HostageState> {
    std::vector<HostageState> states;
    for (const nx::ecs::Entity entity : hostagesOf(world)) {
        const Hostage& hostage = *world.get<Hostage>(entity);
        const Animation& animation = *world.get<Animation>(entity);
        const nx::game::Transform& transform = *world.get<nx::game::Transform>(entity);
        states.push_back(HostageState{.spawn = hostage.spawn,
                                      .origin = transform.origin,
                                      .angles = transform.angles,
                                      .sequence = animation.sequence,
                                      .frame = animation.frame,
                                      .advancedAt = animation.advancedAt,
                                      .framerate = animation.framerate,
                                      .controllers = hostage.controllers});
    }
    return states;
}

auto shownHostageOf(const HostageState& state, const nx::assets::StudioSequence& sequence, nx::GameTime time,
                    nx::TickRate rate) -> ShownHostage {
    return ShownHostage{
        .spawn = state.spawn,
        .origin = state.origin,
        .angles = state.angles,
        .sequence = state.sequence,
        .frame = estimatedFrameOf(sequence, state.frame, time.since(state.advancedAt, rate), state.framerate),
        .controllers = state.controllers};
}

} // namespace cs
