// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.game;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;

namespace cs {

namespace {

constexpr nx::log::Category kLog{"bots"};
/// The navigation profile a bot walks by, a standing player's.
constexpr std::string_view kPlayerProfile = "player";
/// The stream of random numbers a bot's hunt draws its places from, of the player it plays.
constexpr std::string_view kHuntStream = "bot hunt";
/// The stream of random numbers the team a bot leaves to keep bot_quota is drawn from, of the tick.
constexpr std::string_view kQuotaStream = "bot quota";
/// The teams a bot may play in, which MaintainBotQuota draws one of.
constexpr std::uint32_t kPlayingTeams = 2;

/// The view that faces `point` from `eye`, rolled none; `view` itself when they are one point.
[[nodiscard]] auto viewFacing(nx::Vec3 eye, nx::Vec3 point, nx::EulerAngles view) noexcept
    -> nx::EulerAngles {
    const nx::Vec3 way = point - eye;
    return nx::length(way) == 0.0F ? view : nx::vectorAngles(way);
}

/// Turns `command` of a player that knows itself as `self` the way `intent` looks, or along the way it
/// moves, and moves it that way at its full speed (GetMoveSpeed of a running bot: pev->maxspeed).
void steer(UserCommand& command, const nx::agents::Intent& intent, const PlayerOwnState& self) noexcept {
    const nx::Vec3 way = intent.direction;
    const bool moves = nx::length(way) > 0.0F;
    if (intent.lookAt) {
        command.viewAngles = viewFacing(self.eye, *intent.lookAt, self.view);
    } else if (moves) {
        command.viewAngles = nx::EulerAngles{.yaw = nx::vectorAngles(way).yaw};
    }
    if (moves) {
        const nx::AngleVectors axes = nx::angleVectors(nx::EulerAngles{.yaw = command.viewAngles.yaw});
        command.move.forward = self.maxSpeed * nx::dot(way, axes.forward);
        command.move.side = self.maxSpeed * nx::dot(way, axes.right);
    }
}

} // namespace

auto Bots::create(nx::jobs::JobSystem& jobs, Game& game, const nx::game::NavigationData& navigation,
                  BotRules rules) -> nx::Result<std::unique_ptr<Bots>, BotError> {
    const auto profile =
        std::ranges::find(navigation.profiles, kPlayerProfile, &nx::navigation::AgentProfile::name);
    if (profile == navigation.profiles.end()) {
        return nx::fail(BotError::NavigationUnavailable, "a bot walks by the player navigation profile");
    }
    if (profile->radius.value() != kStandingBox.max.x ||
        profile->standingHeight.value() != kStandingBox.max.z - kStandingBox.min.z ||
        profile->stepHeight != MovementSettings{}.stepSize ||
        profile->minGroundNormalZ != kMinGroundNormalZ) {
        return nx::fail(BotError::NavigationUnavailable,
                        "the player navigation profile must match a standing player");
    }
    // A bot does not duck yet: its mesh leaves out what only a ducked player passes.
    nx::navigation::AgentProfile standing = *profile;
    standing.crouchedHeight = standing.standingHeight;
    auto mesh =
        nx::navigation::NavigationMesh::build(jobs, game.navigationGeometry(), standing, navigation.build);
    if (!mesh) {
        return nx::fail(BotError::NavigationUnavailable, std::move(mesh.error()));
    }
    if (auto registered = game.registerEntityComponent<nx::agents::Agent>("nx::agents::Agent"); !registered) {
        return nx::fail(BotError::Unregistered, std::move(registered.error()));
    }
    if (auto registered = game.registerEntityComponent<BotDriver>("cs::BotDriver"); !registered) {
        return nx::fail(BotError::Unregistered, std::move(registered.error()));
    }
    nx::log::info(kLog, "navigation ready: {} polygons", mesh->polygonCount());
    return std::make_unique<Bots>(CreateToken{}, game, std::move(rules), std::move(*mesh));
}

Bots::Bots(CreateToken /*token*/, Game& owner, BotRules botRules, nx::navigation::NavigationMesh navigation)
    : game{&owner}, rules{std::move(botRules)}, mesh{std::move(navigation)}, query{mesh} {}

Bots::~Bots() {
    kickAll();
}

auto Bots::add(std::optional<Team> team) -> PlayerIndex {
    const Team joined = team.value_or(game->defaultTeam());
    const PlayerIndex player = game->join(joined, AutoSwitch::ToHeavier);
    game->attachPlayerComponent(player, nx::agents::Agent{.brain = nx::agents::Brain{brainRulesOf(rules)}});
    game->attachPlayerComponent(player, BotDriver{.hunt = nx::Random{player.value, kHuntStream}});
    bots.push_back(player);
    nx::log::info(kLog, "a bot joins the {}", nx::enumName(joined));
    return player;
}

void Bots::kickAll() {
    while (!bots.empty()) {
        kick(std::prev(bots.end()));
    }
}

void Bots::kick(std::vector<PlayerIndex>::const_iterator bot) {
    game->leave(*bot);
    bots.erase(bot);
}

auto Bots::kickFrom(Team team) -> bool {
    const auto isOf = [this, team](PlayerIndex bot) {
        return game->ownStateOf(bot).team == team;
    };
    // A dead bot first, then any, each in the order they joined, as the engine's slots go.
    auto chosen = std::ranges::find_if(
        bots, [this, &isOf](PlayerIndex bot) { return isOf(bot) && !game->ownStateOf(bot).isAlive; });
    if (chosen == bots.end()) {
        chosen = std::ranges::find_if(bots, isOf);
    }
    if (chosen == bots.end()) {
        return false;
    }
    kick(chosen);
    return true;
}

void Bots::keepQuota(std::uint32_t quota) {
    const nx::game::TickTime time = game->time();
    if (time.now < nextQuotaCheck) {
        return;
    }
    nextQuotaCheck = time.now.after(rules.quotaInterval, time.rate);
    if (quota > bots.size()) {
        std::ignore = add(std::nullopt);
        return;
    }
    if (quota == bots.size()) {
        return;
    }
    const TeamCounts counts = game->teamCounts();
    const std::uint32_t terrorists = counts[Team::Terrorists].players();
    const std::uint32_t counterTerrorists = counts[Team::CounterTerrorists].players();
    Team team = terrorists > counterTerrorists ? Team::Terrorists : Team::CounterTerrorists;
    if (terrorists == counterTerrorists) {
        // Teams equal: one at random, RANDOM_LONG(0, 1) == 0 picking the counter-terrorists.
        nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kQuotaStream};
        team = random.below(kPlayingTeams) == 0 ? Team::CounterTerrorists : Team::Terrorists;
    }
    if (!kickFrom(team)) {
        std::ignore = kickFrom(opponentOf(team));
    }
}

auto Bots::commands() -> std::vector<PlayerCommand> {
    const std::vector<PerceptionTarget> targets = game->perceptionTargets();
    std::vector<PlayerCommand> made;
    made.reserve(bots.size());
    for (const PlayerIndex bot : bots) {
        made.push_back(PlayerCommand{.player = bot, .command = commandOf(bot, targets)});
    }
    return made;
}

auto Bots::perceive(PlayerIndex player, nx::agents::Agent& agent, const PlayerOwnState& self,
                    std::span<const PerceptionTarget> targets) -> std::vector<nx::agents::Observation> {
    std::vector<nx::agents::Observation> seen;
    const std::size_t budget = std::min<std::size_t>(targets.size(), rules.perceptionBudget);
    for (std::size_t looked = 0; looked < budget; ++looked) {
        const PerceptionTarget& target = targets[(agent.nextCandidate + looked) % targets.size()];
        if (target.player == player || target.team == self.team) {
            continue;
        }
        // The first spot of it in its cone that a line reaches is the one it aims at.
        for (const nx::Vec3 spot : sightSpotsOf(target, self.eye, rules.spots)) {
            if (isInViewCone(self.eye, self.view.yaw, rules.fieldOfView, spot) &&
                game->seesSpot(self.eye, spot)) {
                seen.push_back({.entity = target.entity, .position = target.feet, .aimAt = spot});
                break;
            }
        }
    }
    agent.nextCandidate = targets.empty() ? 0 : (agent.nextCandidate + budget) % targets.size();
    return seen;
}

void Bots::hunt(nx::agents::Agent& agent, BotDriver& driver, const nx::agents::Decision& decision,
                nx::Vec3 feet) {
    if (decision.behavior == nx::agents::Behavior::Blocked && !decision.intent.lookAt) {
        agent.brain.setGoal(std::nullopt); // no way to its hunt's place: it draws another
    }
    const bool hasNothingToDo =
        decision.behavior == nx::agents::Behavior::Idle || decision.behavior == nx::agents::Behavior::Blocked;
    if (agent.brain.goal() || !hasNothingToDo) {
        return;
    }
    if (const auto place = query.randomPoint(feet, rules.huntRadius, driver.hunt)) {
        agent.brain.setGoal(*place);
    }
}

auto Bots::commandOf(PlayerIndex player, std::span<const PerceptionTarget> targets) -> UserCommand {
    const nx::game::TickTime time = game->time();
    const PlayerOwnState self = game->ownStateOf(player);
    nx::agents::Agent* const agent = game->playerComponent<nx::agents::Agent>(player);
    BotDriver* const driver = game->playerComponent<BotDriver>(player);
    nx::invariant(agent != nullptr && driver != nullptr, "Bots: a bot's body is an agent with its driver");
    driver->lastCommand = CommandNumber{driver->lastCommand.value + 1};
    UserCommand command{
        .number = driver->lastCommand, .duration = nx::tickLengthOf(time.rate), .viewAngles = self.view};
    if (!self.isAlive) {
        driver->wasAlive = false;
        return command;
    }
    if (!driver->wasAlive) {
        // Spawned again: what it knew and where it went are of its last life.
        agent->brain = nx::agents::Brain{brainRulesOf(rules)};
        driver->hasFired = false;
        driver->wasAlive = true;
    }
    agent->brain.perceive(perceive(player, *agent, self, targets), time.now, time.rate);
    const nx::agents::Decision decision = agent->brain.decide(self.feet, query);
    agent->behavior = decision.behavior;
    hunt(*agent, *driver, decision, self.feet);
    steer(command, decision.intent, self);
    // TogglePrimaryAttack: pressed every other command, so that a pistol fires each round.
    driver->hasFired = decision.intent.attacks && !driver->hasFired;
    if (driver->hasFired) {
        command.buttons.set(Button::Attack);
    }
    return command;
}

} // namespace cs
