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
constexpr nx::log::Category kLog{"zombies"};
constexpr float kHalfTurnDegrees = 180.0F;
constexpr float kFullTurnDegrees = kHalfTurnDegrees * 2.0F;
/// The stream of random numbers a stuck zombie's wiggles and jumps draw from, of its body.
constexpr std::string_view kStuckStream = "zombie stuck";
/// How far a body's slot is shifted from its generation in the seed of its stuck watch's draws.
constexpr int kGenerationBits = 32;
/// The stream of random numbers how far off a zombie places what it hears draws from, of its body and the
/// tick.
constexpr std::string_view kHearingStream = "zombie hearing";
/// The seed of the draws of `entity`'s body: its slot above its generation.
[[nodiscard]] auto seedOf(nx::ecs::Entity entity) noexcept -> std::uint64_t {
    return (std::uint64_t{std::to_underlying(entity.index())} << kGenerationBits) |
           std::to_underlying(entity.generation());
}
[[nodiscard]] auto feetOf(nx::Vec3 origin) -> nx::Vec3 {
    return origin + nx::Vec3{.z = cs::kStandingBox.min.z};
}
[[nodiscard]] auto distanceSquaredAcross(nx::Vec3 first, nx::Vec3 second) -> double {
    const double x = double{first.x} - double{second.x};
    const double y = double{first.y} - double{second.y};
    return (x * x) + (y * y);
}
struct SpawnAnchor {
    nx::Vec3 feet{};
    nx::Degrees yaw{};
};
[[nodiscard]] auto distanceSquared(nx::Vec3 first, nx::Vec3 second) -> double {
    const double z = double{first.z} - double{second.z};
    return distanceSquaredAcross(first, second) + (z * z);
}
[[nodiscard]] auto nearestAnchor(std::span<const SpawnAnchor> base, nx::Vec3 point) -> const SpawnAnchor& {
    return *std::ranges::min_element(base, {}, [point](const SpawnAnchor& anchor) -> double {
        return distanceSquared(anchor.feet, point);
    });
}
[[nodiscard]] auto spawnCandidates(const nx::navigation::NavigationMesh& mesh,
                                   std::span<const SpawnAnchor> base) -> std::vector<nx::Vec3> {
    auto outline = mesh.outline();
    std::vector<nx::Vec3> candidates = std::move(outline.corners);
    candidates.reserve(candidates.size() + outline.polygons.size() + base.size());
    for (const auto& polygon : outline.polygons) {
        const auto corners = std::span{candidates}.subspan(polygon.firstCorner, polygon.cornerCount);
        double x = 0;
        double y = 0;
        double z = 0;
        for (const nx::Vec3 corner : corners) {
            x += double{corner.x};
            y += double{corner.y};
            z += double{corner.z};
        }
        const auto count = static_cast<double>(corners.size());
        candidates.push_back({.x = static_cast<float>(x / count),
                              .y = static_cast<float>(y / count),
                              .z = static_cast<float>(z / count)});
    }
    for (const SpawnAnchor& anchor : base) {
        candidates.push_back(anchor.feet);
    }
    std::ranges::sort(candidates, [](nx::Vec3 first, nx::Vec3 second) -> bool {
        return std::tie(first.x, first.y, first.z) < std::tie(second.x, second.y, second.z);
    });
    const auto duplicates = std::ranges::unique(candidates);
    candidates.erase(duplicates.begin(), duplicates.end());
    std::ranges::stable_sort(candidates, {}, [base](nx::Vec3 point) -> double {
        return distanceSquared(point, nearestAnchor(base, point).feet);
    });
    return candidates;
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
        read->fieldOfView.value() > kFullTurnDegrees || !positive(read->memoryLifetime.count()) ||
        !positive(read->arrivalDistance.value()) || !positive(read->replanDistance.value()) ||
        !positive(read->spawnRadius.value()) || !positive(read->spawnSpacing.value()) ||
        read->memoryLimit == 0 || read->perceptionBudget == 0 || read->spawnBatchLimit == 0) {
        return nx::fail(
            ZombieError::InvalidData,
            std::format(
                "{}: positive perception, memory and spawn budgets and an FOV at most 360 are required",
                source));
    }
    if (!cs::isValid(read->hearing)) {
        return nx::fail(ZombieError::InvalidData,
                        std::format("{}: each of hearing's ranges more than none and its error share none or "
                                    "more are required",
                                    source));
    }
    if (!nx::agents::isValid(read->stuck)) {
        return nx::fail(
            ZombieError::InvalidData,
            std::format("{}: stuck's interval, samples, speed, distance and give-up time more than "
                        "none, its first jump none or more, and each range's shortest more than "
                        "none, its longest no shorter, are required",
                        source));
    }
    return std::move(*read);
}

Horde::Horde(CreateToken /*token*/, cs::EntityTypeIndex entityType, Settings rules,
             const nx::navigation::NavigationMesh& mesh, nx::jobs::JobSystem& jobSystem, cs::Game& game)
    : type{entityType}, settings{std::move(rules)}, jobs{&jobSystem}, query{mesh}, deciders{mesh},
      owner{&game} {}

Horde::~Horde() {
    const auto cleared = clear();
    nx::invariant(cleared.has_value(),
                  "Horde: owned bodies and their borrowed corridors are destroyed while the mesh is there");
}

auto Horde::create(cs::Game& game, const nx::navigation::NavigationMesh& mesh, nx::jobs::JobSystem& jobs,
                   cs::EntityTypeIndex type, Settings settings)
    -> nx::Result<std::unique_ptr<Horde>, ZombieError> {
    if (auto registered = game.registerEntityComponent<nx::agents::Agent>("nx::agents::Agent"); !registered) {
        return nx::fail(ZombieError::SpawnFailed, std::move(registered.error()));
    }
    nx::log::info(kLog, "zombies walk {} polygons", mesh.polygonCount());
    return std::make_unique<Horde>(CreateToken{}, type, std::move(settings), mesh, jobs, game);
}

auto Horde::spawn(std::span<const cs::SpawnPoint> base, std::uint32_t count)
    -> nx::Result<void, ZombieError> {
    cs::Game& game = *owner;
    if (count == 0 || count > settings.spawnBatchLimit) {
        return nx::fail(ZombieError::SpawnFailed,
                        std::format("count must be from 1 to {}", settings.spawnBatchLimit));
    }
    if (base.empty()) {
        return nx::fail(ZombieError::SpawnFailed, "the selected base has no map spawn points");
    }
    std::vector<SpawnAnchor> anchors;
    anchors.reserve(base.size());
    for (const cs::SpawnPoint& spawn : base) {
        if (!std::isfinite(spawn.origin.x) || !std::isfinite(spawn.origin.y) ||
            !std::isfinite(spawn.origin.z) || !std::isfinite(spawn.yaw.value())) {
            return nx::fail(ZombieError::SpawnFailed, "finite base origins and yaws are required");
        }
        auto feet = query.nearestPoint(feetOf(spawn.origin));
        if (!feet) {
            return nx::fail(ZombieError::SpawnFailed, "a base spawn point is not near walkable ground",
                            std::move(feet.error()));
        }
        anchors.push_back({.feet = *feet, .yaw = spawn.yaw});
    }
    std::vector<nx::ecs::Entity> born;
    std::vector<nx::Vec3> occupied;
    for (const auto& body : game.entities()) {
        if (body.health > nx::HitPoints{}) {
            occupied.push_back(body.origin);
        }
    }
    const auto rollback = [&game, &born] -> void {
        for (const nx::ecs::Entity entity : born) {
            const auto removed = game.removeEntity(entity);
            nx::invariant(removed.has_value(), "Horde: rollback owns every newly created body");
        }
    };
    const double radius = double{settings.spawnRadius.value()};
    const double spacing = double{settings.spawnSpacing.value()};
    for (const nx::Vec3 candidate : spawnCandidates(query.mesh(), anchors)) {
        auto point = query.nearestPoint(candidate);
        if (!point) {
            rollback();
            return nx::fail(ZombieError::SpawnFailed, std::move(point.error()));
        }
        const SpawnAnchor& anchor = nearestAnchor(anchors, *point);
        if (distanceSquared(*point, anchor.feet) > radius * radius ||
            std::ranges::any_of(occupied, [&](nx::Vec3 other) -> bool {
                return distanceSquaredAcross(*point, other) < spacing * spacing;
            })) {
            continue;
        }
        auto reachable = query.isReachable(anchor.feet, *point);
        if (!reachable) {
            rollback();
            return nx::fail(ZombieError::SpawnFailed, std::move(reachable.error()));
        }
        if (!*reachable) {
            continue;
        }
        auto made = game.createEntity(
            type, {.origin = *point - nx::Vec3{.z = cs::kStandingBox.min.z}, .angles = {.yaw = anchor.yaw}});
        if (!made) {
            if (made.error().code == cs::EntityError::InvalidPlacement) {
                continue;
            }
            rollback();
            return nx::fail(ZombieError::SpawnFailed, std::move(made.error()));
        }
        born.push_back(*made);
        occupied.push_back(*point);
        if (born.size() == count) {
            break;
        }
    }
    if (born.size() != count) {
        rollback();
        return nx::fail(
            ZombieError::SpawnFailed,
            std::format("only {} of {} clear reachable positions were found; the batch was rolled back",
                        born.size(), count));
    }
    for (const nx::ecs::Entity entity : born) {
        const std::uint64_t seed = seedOf(entity);
        auto attached = game.attachEntityComponent(
            entity, nx::agents::Agent{
                        .brain = nx::agents::Brain{rulesOf(settings)},
                        .stuck = nx::agents::StuckWatch{settings.stuck, nx::Random{seed, kStuckStream}}});
        nx::invariant(attached.has_value(), "Horde: a newly created body accepts its brain");
        agents.push_back(entity);
    }
    return {};
}

auto Horde::commands() -> std::vector<cs::EntityCommand> {
    cs::Game& game = *owner;
    const std::vector<cs::EntityState> bodies = game.entities();
    const std::vector<cs::PerceptionTarget> targets = game.perceptionTargets();
    const std::vector<cs::GameNoise> noises = game.noises();
    // The players there still are, living: a zombie forgets the others (nx::agents::Brain::forgetAbsent).
    std::vector<nx::ecs::Entity> present =
        targets | std::views::transform(&cs::PerceptionTarget::entity) | std::ranges::to<std::vector>();
    std::ranges::sort(present);
    // Nothing moves while the horde perceives: what is solid to its sights is gathered once.
    const nx::game::Solids solids = game.sightSolids();
    const nx::game::TickTime time = game.time();
    const float minimumDot =
        std::cos(settings.fieldOfView.value() * std::numbers::pi_v<float> / kFullTurnDegrees);
    // A body alive this tick, its agent and its eye, between what it perceives and what it decides.
    struct Turn {
        nx::ecs::Entity entity{};
        const cs::EntityState* body = nullptr;
        nx::agents::Agent* agent = nullptr;
        nx::Vec3 eye{};
    };
    std::vector<Turn> turns;
    std::vector<nx::agents::DecisionRequest> requests;
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
                !game.sees(solids, entity, eye, candidate)) {
                continue;
            }
            seen.push_back({.entity = candidate.entity, .position = candidate.feet, .aimAt = candidate.eye});
        }
        agent.nextCandidate = targets.empty() ? 0 : (agent.nextCandidate + budget) % targets.size();
        agent.brain.forgetAbsent(present);
        agent.brain.perceive(seen, time.now, time.rate);
        // A zombie plays in no team: it hears every player.
        nx::Random hearing{seedOf(entity) ^ std::bit_cast<std::uint64_t>(time.now.tick()), kHearingStream};
        agent.brain.sense(cs::heardBy({.ear = eye}, noises, settings.hearing, query, hearing), time.now,
                          time.rate);
        // Felt where they are, last: the players its body touches.
        agent.brain.sense(
            cs::touchedBy({.entity = entity, .origin = found->origin, .box = cs::kStandingBox}, targets),
            time.now, time.rate);
        turns.push_back({.entity = entity, .body = &*found, .agent = &agent, .eye = eye});
        requests.push_back({.brain = &agent.brain,
                            .feet = feetOf(found->origin),
                            .velocity = found->velocity,
                            .maxSpeed = settings.body.speed,
                            .body = entity,
                            .stuck = &agent.stuck,
                            .elapsed = nx::tickLengthOf(time.rate)});
    }
    // The bodies the horde steers around, players' and its own.
    const std::vector<nx::agents::Body> steeredAround =
        game.movingBodies() | std::views::transform([](const cs::MovingBody& body) {
            return nx::agents::Body{
                .entity = body.entity, .feet = body.feet, .velocity = body.velocity, .radius = body.radius};
        }) |
        std::ranges::to<std::vector>();
    // What a body perceives does not depend on what another decides, nor a decision on another's.
    std::vector<nx::agents::Decision> decisions(requests.size());
    deciders.decide(*jobs, requests, decisions, steeredAround);
    std::vector<cs::EntityCommand> commands;
    commands.reserve(turns.size());
    for (std::size_t index = 0; index < turns.size(); ++index) {
        const auto [entity, found, component, eye] = turns[index];
        nx::agents::Agent& agent = *component;
        const nx::agents::Decision& decision = decisions[index];
        if (agent.behavior != decision.behavior) {
            if (transitionLogs.allow()) {
                const std::string failure = decision.navigationError
                                                ? std::format(", {}", nx::enumName(*decision.navigationError))
                                                : std::string{};
                nx::log::info(kLog, "agent {}:{}: {} -> {}{} ({} transitions suppressed)",
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
            // It faces the way it moves, steered or wiggling: its model walks ahead only.
            command.move.forward = settings.body.speed * decision.intent.speedShare;
        }
        if (decision.intent.jumps) {
            command.buttons.set(cs::Button::Jump);
        }
        if (nx::length(aim) > 0.0F) {
            command.viewAngles.yaw =
                nx::Degrees{std::atan2(aim.y, aim.x) * kHalfTurnDegrees / std::numbers::pi_v<float>};
            command.viewAngles.pitch = nx::Degrees{-std::atan2(aim.z, std::hypot(aim.x, aim.y)) *
                                                   kHalfTurnDegrees / std::numbers::pi_v<float>};
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
