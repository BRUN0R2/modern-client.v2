// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module mod.zombies;
import std;
import nx.core;
import nx.math;
import nx.data;
import nx.ecs;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;

namespace zombies {
namespace {
constexpr nx::log::Category zombieLog{"zombies"};
constexpr float halfTurnDegrees = 180.0F;
constexpr float fullTurnDegrees = halfTurnDegrees * 2.0F;
constexpr std::string_view walkingProfile = "player";
[[nodiscard]] auto feetOf(nx::Vec3 origin) -> nx::Vec3 {
    return origin + nx::Vec3{.z = cs::kStandingBox.min.z};
}
[[nodiscard]] auto rulesOf(const Settings& settings) -> nx::agents::Rules {
    return {.memoryLifetime = settings.memoryLifetime,
            .attackDistance = settings.body.meleeReach,
            .arrivalDistance = settings.arrivalDistance,
            .replanDistance = settings.replanDistance,
            .memoryLimit = settings.memoryLimit};
}
} // namespace

auto readSettings(std::string_view text, std::string_view source) -> nx::Result<Settings, ZombieError> {
    auto read = nx::data::parseToml<Settings>(text, source);
    if (!read) {
        return nx::fail(ZombieError::InvalidData, std::move(read.error()));
    }
    const auto positive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };
    if (!positive(read->sightRange.value()) || !positive(read->fieldOfView.value()) ||
        read->fieldOfView.value() > fullTurnDegrees || !positive(read->memoryLifetime.count()) ||
        !positive(read->arrivalDistance.value()) || !positive(read->replanDistance.value()) ||
        !positive(read->spawnDistance.value()) || !positive(read->spawnSpacing.value()) ||
        read->memoryLimit == 0 || read->perceptionBudget == 0 || read->spawnBatchLimit == 0) {
        return nx::fail(
            ZombieError::InvalidData,
            std::format(
                "{}: positive perception, memory and spawn budgets and an FOV at most 360 are required",
                source));
    }
    return std::move(*read);
}

Horde::Horde(CreateToken /*token*/, cs::EntityTypeIndex entityType, Settings rules,
             nx::navigation::NavigationMesh navigation, cs::Game& game)
    : type{entityType}, settings{std::move(rules)}, mesh{std::move(navigation)}, query{mesh}, owner{&game} {}

Horde::~Horde() {
    const auto cleared = clear();
    nx::invariant(cleared.has_value(),
                  "Horde: owned bodies and their borrowed corridors are destroyed before the mesh");
}

auto Horde::create(nx::jobs::JobSystem& jobs, cs::Game& game, const nx::game::NavigationData& navigation,
                   cs::EntityTypeIndex type, Settings settings)
    -> nx::Result<std::unique_ptr<Horde>, ZombieError> {
    const auto profile =
        std::ranges::find(navigation.profiles, walkingProfile, &nx::navigation::AgentProfile::name);
    if (profile == navigation.profiles.end()) {
        return nx::fail(ZombieError::NavigationUnavailable,
                        "the humanoid body requires the player navigation profile");
    }
    if (profile->radius.value() != cs::kStandingBox.max.x ||
        profile->standingHeight.value() != cs::kStandingBox.max.z - cs::kStandingBox.min.z ||
        profile->stepHeight != cs::MovementSettings{}.stepSize ||
        profile->minGroundNormalZ != cs::kMinGroundNormalZ) {
        return nx::fail(ZombieError::NavigationUnavailable,
                        "the navigation profile must match the standing body");
    }
    // This adapter cannot crouch. A mesh with equal heights excludes crouch-only passages.
    nx::navigation::AgentProfile standing = *profile;
    standing.crouchedHeight = standing.standingHeight;
    auto mesh =
        nx::navigation::NavigationMesh::build(jobs, game.navigationGeometry(), standing, navigation.build);
    if (!mesh) {
        return nx::fail(ZombieError::NavigationUnavailable, std::move(mesh.error()));
    }
    if (auto registered = game.registerEntityComponent<nx::agents::Agent>("nx::agents::Agent"); !registered) {
        return nx::fail(ZombieError::SpawnFailed, std::move(registered.error()));
    }
    nx::log::info(zombieLog, "navigation ready: {} polygons", mesh->polygonCount());
    return std::make_unique<Horde>(CreateToken{}, type, std::move(settings), std::move(*mesh), game);
}

auto Horde::spawn(nx::Vec3 origin, nx::Degrees yaw, std::uint32_t count) -> nx::Result<void, ZombieError> {
    cs::Game& game = *owner;
    if (count == 0 || count > settings.spawnBatchLimit) {
        return nx::fail(ZombieError::SpawnFailed,
                        std::format("count must be from 1 to {}", settings.spawnBatchLimit));
    }
    std::vector<nx::ecs::Entity> born;
    const nx::AngleVectors axes = nx::angleVectors({.yaw = yaw});
    for (std::uint32_t index = 0; index < count; ++index) {
        const float sideways = (static_cast<float>(index) - static_cast<float>(count - 1) / 2.0F) *
                               settings.spawnSpacing.value();
        const nx::Vec3 requested =
            origin + axes.forward * settings.spawnDistance.value() + axes.right * sideways;
        auto point = query.nearestPoint(feetOf(requested));
        nx::Result<nx::ecs::Entity, cs::EntityError> made =
            point ? game.createEntity(type, {.origin = *point - nx::Vec3{.z = cs::kStandingBox.min.z},
                                             .angles = {.yaw = nx::Degrees{yaw.value() + halfTurnDegrees}}})
                  : nx::Result<nx::ecs::Entity, cs::EntityError>{
                        nx::fail(cs::EntityError::InvalidPlacement, std::move(point.error()))};
        if (!made) {
            for (const nx::ecs::Entity entity : born) {
                const auto removed = game.removeEntity(entity);
                nx::invariant(removed.has_value(), "Horde: rollback owns every newly created body");
            }
            return nx::fail(ZombieError::SpawnFailed,
                            "the batch was rolled back; aim at open walkable ground",
                            std::move(made.error()));
        }
        born.push_back(*made);
    }
    for (const nx::ecs::Entity entity : born) {
        auto attached = game.attachEntityComponent(
            entity, nx::agents::Agent{.brain = nx::agents::Brain{rulesOf(settings)}});
        nx::invariant(attached.has_value(), "Horde: a newly created body accepts its brain");
        agents.push_back(entity);
    }
    return {};
}

auto Horde::commands() -> std::vector<cs::EntityCommand> {
    cs::Game& game = *owner;
    const std::vector<cs::EntityState> bodies = game.entities();
    const std::vector<cs::PerceptionTarget> targets = game.perceptionTargets();
    const nx::game::TickTime time = game.time();
    std::vector<cs::EntityCommand> commands;
    const float minimumDot =
        std::cos(settings.fieldOfView.value() * std::numbers::pi_v<float> / fullTurnDegrees);
    for (const nx::ecs::Entity entity : agents) {
        const auto found = std::ranges::lower_bound(bodies, entity, {}, &cs::EntityState::entity);
        if (found == bodies.end() || found->entity != entity || found->health <= nx::HitPoints{}) {
            continue;
        }
        nx::agents::Agent* const component = game.entityComponent<nx::agents::Agent>(entity);
        nx::invariant(component != nullptr, "Horde: an attached brain");
        nx::agents::Agent& agent = *component;
        std::vector<nx::agents::Observation> seen;
        const nx::Vec3 forward = nx::angleVectors(found->angles).forward;
        const nx::Vec3 eye = found->origin + nx::Vec3{.z = cs::kStandingViewHeight.value()};
        const std::size_t budget = std::min(targets.size(), std::size_t{settings.perceptionBudget});
        for (std::size_t checked = 0; checked < budget; ++checked) {
            const cs::PerceptionTarget& candidate = targets[(agent.nextCandidate + checked) % targets.size()];
            const nx::Vec3 difference = candidate.eye - eye;
            const float distance = nx::length(difference);
            if (distance > settings.sightRange.value() ||
                (distance > 0.0F && nx::dot(forward, difference / distance) < minimumDot) ||
                !game.sees(entity, eye, candidate)) {
                continue;
            }
            seen.push_back({.entity = candidate.entity, .position = candidate.feet, .aimAt = candidate.eye});
        }
        agent.nextCandidate = targets.empty() ? 0 : (agent.nextCandidate + budget) % targets.size();
        agent.brain.perceive(seen, time.now, time.rate);
        const nx::agents::Decision decision = agent.brain.decide(feetOf(found->origin), query);
        if (agent.behavior != decision.behavior) {
            if (transitionLogs.allow()) {
                const std::string failure = decision.navigationError
                                                ? std::format(", {}", nx::enumName(*decision.navigationError))
                                                : std::string{};
                nx::log::info(zombieLog, "agent {}:{}: {} -> {}{} ({} transitions suppressed)",
                              std::to_underlying(entity.index()), std::to_underlying(entity.generation()),
                              nx::enumName(agent.behavior), nx::enumName(decision.behavior), failure,
                              transitionLogs.takeSuppressed());
            }
            agent.behavior = decision.behavior;
        }
        cs::UserCommand command{.duration = nx::tickLengthOf(time.rate), .viewAngles = found->angles};
        nx::Vec3 aim = decision.intent.direction;
        if (decision.intent.attacks && decision.intent.lookAt) {
            aim = *decision.intent.lookAt - eye;
            command.buttons.set(cs::Button::Attack);
        } else if (nx::length(decision.intent.direction) > 0.0F) {
            command.move.forward = settings.body.speed;
        }
        if (nx::length(aim) > 0.0F) {
            command.viewAngles.yaw =
                nx::Degrees{std::atan2(aim.y, aim.x) * halfTurnDegrees / std::numbers::pi_v<float>};
            command.viewAngles.pitch = nx::Degrees{-std::atan2(aim.z, std::hypot(aim.x, aim.y)) *
                                                   halfTurnDegrees / std::numbers::pi_v<float>};
        }
        commands.push_back({.entity = entity, .command = command});
    }
    return commands;
}

auto Horde::clear() -> nx::Result<void, ZombieError> {
    cs::Game& game = *owner;
    for (const nx::ecs::Entity entity : agents) {
        if (auto removed = game.removeEntity(entity); !removed) {
            // A body removed by another native consumer already satisfies clear's postcondition.
            if (removed.error().code != cs::EntityError::UnknownEntity) {
                return nx::fail(ZombieError::SpawnFailed, std::move(removed.error()));
            }
        }
    }
    agents.clear();
    return {};
}
} // namespace zombies
