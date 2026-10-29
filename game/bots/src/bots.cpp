// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.ecs;
import nx.game;
import nx.navigation;
import nx.agents;
import cs.shared;
import cs.server;

namespace cs {

namespace {

constexpr nx::log::Category kLog{"bots"};
/// The stream of random numbers a bot's hunt draws its places from, of the player it plays.
constexpr std::string_view kHuntStream = "bot hunt";
/// The stream of random numbers a stuck bot's wiggles and jumps draw from, of the player it plays.
constexpr std::string_view kStuckStream = "bot stuck";
/// The stream of random numbers the team a bot leaves to keep bot_quota is drawn from, of the tick.
constexpr std::string_view kQuotaStream = "bot quota";
/// The stream of random numbers a bot's profile is drawn from, of the tick it joins and the bots in the
/// game.
constexpr std::string_view kProfileStream = "bot profile";
/// The stream of random numbers a bot's aim's errors and its pauses between presses draw from, of the
/// player it plays.
constexpr std::string_view kAimStream = "bot aim";
/// How far the tick a bot joins at is shifted from the count of bots in the seed of its profile's draw.
constexpr int kCountBits = 32;
/// The teams a bot may play in, which MaintainBotQuota draws one of.
constexpr std::uint32_t kPlayingTeams = 2;

/// The view that faces `point` from `eye`, rolled none; `view` itself when they are one point.
[[nodiscard]] auto viewFacing(nx::Vec3 eye, nx::Vec3 point, nx::EulerAngles view) noexcept
    -> nx::EulerAngles {
    const nx::Vec3 way = point - eye;
    return nx::length(way) == 0.0F ? view : nx::vectorAngles(way);
}

/// Where a player that knows itself as `self` wants to look: at `aimSpot`, else where `intent` looks,
/// else ahead along its way, level, however it steers or wiggles (SetLookAngles of m_lookAheadAngle),
/// else where it looks.
[[nodiscard]] auto viewWanted(const nx::agents::Intent& intent, std::optional<nx::Vec3> aimSpot,
                              const PlayerOwnState& self) noexcept -> nx::EulerAngles {
    if (const std::optional<nx::Vec3> looked = aimSpot ? aimSpot : intent.lookAt) {
        return viewFacing(self.eye, *looked, self.view);
    }
    if (nx::length(intent.ahead) > 0.0F) {
        return nx::EulerAngles{.yaw = nx::vectorAngles(intent.ahead).yaw};
    }
    return self.view;
}

/// Moves `command`, its view turned, the way `intent` moves at its share of `maxSpeed`, its full speed
/// (GetMoveSpeed of a running bot: pev->maxspeed), and jumps as it does.
void moveAlong(UserCommand& command, const nx::agents::Intent& intent, nx::UnitsPerSecond maxSpeed) noexcept {
    if (intent.jumps) {
        command.buttons.set(Button::Jump);
    }
    const nx::Vec3 way = intent.direction;
    if (nx::length(way) == 0.0F) {
        return;
    }
    const nx::AngleVectors axes = nx::angleVectors(nx::EulerAngles{.yaw = command.viewAngles.yaw});
    const nx::UnitsPerSecond speed = maxSpeed * intent.speedShare;
    command.move.forward = speed * nx::dot(way, axes.forward);
    command.move.side = speed * nx::dot(way, axes.right);
}

/// `moving`, the game's, as the agents steer around them.
[[nodiscard]] auto bodiesOf(std::span<const MovingBody> moving) -> std::vector<nx::agents::Body> {
    return moving | std::views::transform([](const MovingBody& body) {
               return nx::agents::Body{.entity = body.entity,
                                       .feet = body.feet,
                                       .velocity = body.velocity,
                                       .radius = body.radius};
           }) |
           std::ranges::to<std::vector>();
}

/// A profile of the bots' rules: the index of its level of skill and its name.
struct ProfileOf {
    std::size_t level = 0;
    std::string_view name;
};

/// The profiles of the levels of `levels`, those of `difficulty` alone when it is given, in their order.
[[nodiscard]] auto profilesOf(std::span<const BotSkillLevel> levels, std::optional<BotDifficulty> difficulty)
    -> std::vector<ProfileOf> {
    std::vector<ProfileOf> profiles;
    for (const auto& [index, level] : std::views::enumerate(levels)) {
        if (!difficulty || std::ranges::contains(level.difficulties, *difficulty)) {
            for (const std::string& name : level.profiles) {
                profiles.push_back({.level = static_cast<std::size_t>(index), .name = name});
            }
        }
    }
    return profiles;
}

/// The name of a bot of the profile `profile` after `prefix`, the prefix and a blank before it
/// (UTIL_ConstructBotNetName).
[[nodiscard]] auto botNameOf(std::string_view profile, std::string_view prefix) -> std::string {
    return prefix.empty() ? std::string{profile} : std::format("{} {}", prefix, profile);
}

} // namespace

auto Bots::create(Game& game, const nx::navigation::NavigationMesh& mesh, nx::jobs::JobSystem& jobs,
                  BotRules rules) -> nx::Result<std::unique_ptr<Bots>, BotError> {
    if (auto registered = game.registerEntityComponent<nx::agents::Agent>("nx::agents::Agent"); !registered) {
        return nx::fail(BotError::Unregistered, std::move(registered.error()));
    }
    if (auto registered = game.registerEntityComponent<BotDriver>("cs::BotDriver"); !registered) {
        return nx::fail(BotError::Unregistered, std::move(registered.error()));
    }
    nx::log::info(kLog, "bots walk {} polygons", mesh.polygonCount());
    return std::make_unique<Bots>(CreateToken{}, game, std::move(rules), mesh, jobs);
}

struct Bots::Turn {
    PlayerIndex player{};
    nx::agents::Agent* agent = nullptr;
    BotDriver* driver = nullptr;
    PlayerOwnState self{};
    UserCommand command{};
};

Bots::Bots(CreateToken /*token*/, Game& owner, BotRules botRules, const nx::navigation::NavigationMesh& mesh,
           nx::jobs::JobSystem& jobSystem)
    : game{&owner}, rules{std::move(botRules)}, jobs{&jobSystem}, query{mesh}, deciders{mesh} {}

Bots::~Bots() {
    kickAll();
}

auto Bots::add(std::optional<Team> team, BotDifficulty difficulty, std::string_view prefix) -> PlayerIndex {
    const std::vector<ProfileOf> ofDifficulty = profilesOf(rules.skillLevels, difficulty);
    nx::invariant(!ofDifficulty.empty(), "Bots: profiles for each difficulty");
    std::vector<ProfileOf> free =
        ofDifficulty |
        std::views::filter([this](const ProfileOf& profile) { return !isInGame(profile.name); }) |
        std::ranges::to<std::vector>();
    const std::span<const ProfileOf> drawn = free.empty() ? std::span{ofDifficulty} : std::span{free};
    nx::Random random{(std::bit_cast<std::uint64_t>(game->time().now.tick()) << kCountBits) ^ bots.size(),
                      kProfileStream};
    const ProfileOf profile = drawn[random.below(drawn.size())];
    return join(profile.level, profile.name, team, prefix);
}

auto Bots::addProfile(std::string_view profile, std::optional<Team> team, std::string_view prefix)
    -> nx::Result<PlayerIndex, BotJoinError> {
    const std::vector<ProfileOf> every = profilesOf(rules.skillLevels, std::nullopt);
    const auto named = std::ranges::find_if(
        every, [profile](const ProfileOf& each) { return nx::equalsIgnoringCase(each.name, profile); });
    if (named == every.end()) {
        return nx::fail(BotJoinError::UnknownProfile, std::format("no profile is named {}", profile));
    }
    if (isInGame(named->name)) {
        return nx::fail(BotJoinError::ProfileInGame, std::format("{} is in the game already", named->name));
    }
    return join(named->level, named->name, team, prefix);
}

auto Bots::join(std::size_t level, std::string_view profile, std::optional<Team> team,
                std::string_view prefix) -> PlayerIndex {
    const BotSkillLevel& skill = rules.skillLevels.at(level);
    const PlayerIndex player =
        game->join(team.value_or(game->defaultTeam()), AutoSwitch::ToHeavier, botNameOf(profile, prefix));
    game->attachPlayerComponent(
        player, nx::agents::Agent{
                    .brain = nx::agents::Brain{brainRulesOf(rules, skill.reactionTime)},
                    .stuck = nx::agents::StuckWatch{rules.stuck, nx::Random{player.value, kStuckStream}}});
    game->attachPlayerComponent(player, BotDriver{.profile = std::string{profile},
                                                  .skillLevel = level,
                                                  .hunt = nx::Random{player.value, kHuntStream},
                                                  .aim = nx::Random{player.value, kAimStream}});
    bots.push_back(player);
    nx::log::info(kLog, "{} plays at the {} level", game->nameOf(player), skill.name);
    return player;
}

auto Bots::isInGame(std::string_view profile) -> bool {
    const bool isPlayed = std::ranges::any_of(bots, [this, profile](PlayerIndex bot) {
        return game->playerComponent<BotDriver>(bot)->profile == profile;
    });
    const std::optional<PlayerIndex> named = game->playerNamed(profile);
    return isPlayed || (named && game->playerComponent<BotDriver>(*named) == nullptr);
}

auto Bots::skillLevelOf(PlayerIndex player) -> const BotSkillLevel& {
    const BotDriver* const driver = game->playerComponent<BotDriver>(player);
    nx::expects(driver != nullptr, "Bots::skillLevelOf: a bot's player");
    return rules.skillLevels[driver->skillLevel];
}

void Bots::kickAll() {
    while (!bots.empty()) {
        kick(std::prev(bots.end()));
    }
}

auto Bots::kickNamed(std::string_view name) -> bool {
    const auto named =
        std::ranges::find_if(bots, [this, name](PlayerIndex bot) { return game->nameOf(bot) == name; });
    if (named == bots.end()) {
        return false;
    }
    kick(named);
    return true;
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

void Bots::keepQuota(std::uint32_t quota, BotDifficulty difficulty, std::string_view prefix) {
    const nx::game::TickTime time = game->time();
    if (time.now < nextQuotaCheck) {
        return;
    }
    nextQuotaCheck = time.now.after(rules.quotaInterval, time.rate);
    if (quota > bots.size()) {
        std::ignore = add(std::nullopt, difficulty, prefix);
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
    // The enemies there still are, living: a bot forgets the others (nx::agents::Brain::forgetAbsent).
    std::vector<nx::ecs::Entity> present =
        targets | std::views::transform(&PerceptionTarget::entity) | std::ranges::to<std::vector>();
    std::ranges::sort(present);
    // Nothing moves while the bots perceive: what is solid to their sights is gathered once.
    const nx::game::Solids solids = game->sightSolids();
    const Sights sights{.targets = targets, .present = present, .solids = solids};
    // The bodies the bots steer around, players' and others'.
    const std::vector<nx::agents::Body> bodies = bodiesOf(game->movingBodies());
    std::vector<Turn> turns;
    turns.reserve(bots.size());
    std::vector<nx::agents::DecisionRequest> requests;
    for (const PlayerIndex bot : bots) {
        Turn& turn = turns.emplace_back(beginTurn(bot, sights));
        if (turn.self.isAlive) {
            const auto self = std::ranges::find(targets, bot, &PerceptionTarget::player);
            requests.push_back({.brain = &turn.agent->brain,
                                .feet = turn.self.feet,
                                .velocity = turn.self.velocity,
                                .maxSpeed = turn.self.maxSpeed,
                                .body = self != targets.end() ? std::optional{self->entity} : std::nullopt,
                                .stuck = &turn.agent->stuck,
                                .elapsed = turn.command.duration});
        }
    }
    // What a bot perceives does not depend on what another decides, nor a decision on another's.
    std::vector<nx::agents::Decision> decisions(requests.size());
    deciders.decide(*jobs, requests, decisions, bodies);
    std::vector<PlayerCommand> made;
    made.reserve(turns.size());
    auto decision = decisions.begin();
    for (Turn& turn : turns) {
        made.push_back(PlayerCommand{.player = turn.player,
                                     .command = turn.self.isAlive ? finishTurn(turn, *decision++, targets)
                                                                  : turn.command});
    }
    return made;
}

auto Bots::perceive(PlayerIndex player, nx::agents::Agent& agent, const PlayerOwnState& self,
                    const Sights& sights) -> std::vector<nx::agents::Observation> {
    const std::span<const PerceptionTarget> targets = sights.targets;
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
                game->seesSpot(sights.solids, self.eye, spot)) {
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

auto Bots::aimSpotOf(BotDriver& driver, const PlayerOwnState& self, const nx::agents::Decision& decision,
                     std::span<const PerceptionTarget> targets, nx::Seconds duration) -> nx::Vec3 {
    const nx::game::TickTime time = game->time();
    const BotSkill skill = rules.skillLevels[driver.skillLevel].skill;
    nx::invariant(decision.intent.lookAt.has_value() && decision.target.has_value(),
                  "Bots: an attack looks at the target it pursues");
    // The enemy it attacks it sees: it is among the targets of the tick.
    const auto enemy = std::ranges::find(targets, *decision.target, &PerceptionTarget::entity);
    nx::invariant(enemy != targets.end(), "Bots: the enemy a bot attacks is alive");
    const nx::Vec3 spot = leadSpot(*decision.intent.lookAt, enemy->velocity, self.velocity, skill, rules.aim);
    settleAimError(driver.aimError,
                   {.skill = skill,
                    .range = nx::HammerUnits{nx::length(spot - self.eye)},
                    .isTurning = isTurningFaster(driver.turn, rules.aim.steadyTurn),
                    .now = time.now,
                    .rate = time.rate,
                    .duration = duration},
                   rules.aim, driver.aim);
    return spot + driver.aimError.offset;
}

auto Bots::beginTurn(PlayerIndex player, const Sights& sights) -> Turn {
    const nx::game::TickTime time = game->time();
    Turn turn{.player = player,
              .agent = game->playerComponent<nx::agents::Agent>(player),
              .driver = game->playerComponent<BotDriver>(player),
              .self = game->ownStateOf(player)};
    nx::invariant(turn.agent != nullptr && turn.driver != nullptr,
                  "Bots: a bot's body is an agent with its driver");
    nx::agents::Agent* const agent = turn.agent;
    BotDriver* const driver = turn.driver;
    driver->lastCommand = CommandNumber{driver->lastCommand.value + 1};
    turn.command = UserCommand{
        .number = driver->lastCommand, .duration = nx::tickLengthOf(time.rate), .viewAngles = turn.self.view};
    if (!turn.self.isAlive) {
        return turn;
    }
    if (driver->lifeSpawnedAt != turn.self.spawnedAt) {
        // Spawned again, killed or alive at a round's start (SpawnBot's ResetValues): what it knew, where it
        // went, how it turned, aimed and fired are of its last life.
        const BotSkillLevel& level = rules.skillLevels[driver->skillLevel];
        agent->brain = nx::agents::Brain{brainRulesOf(rules, level.reactionTime)};
        agent->stuck.reset();
        driver->turn = {};
        driver->aimError = {};
        driver->trigger = {};
        driver->lifeSpawnedAt = turn.self.spawnedAt;
    }
    agent->brain.forgetAbsent(sights.present);
    agent->brain.perceive(perceive(player, *agent, turn.self, sights), time.now, time.rate);
    return turn;
}

auto Bots::finishTurn(Turn& turn, const nx::agents::Decision& decision,
                      std::span<const PerceptionTarget> targets) -> UserCommand {
    const nx::game::TickTime time = game->time();
    const PlayerOwnState& self = turn.self;
    nx::agents::Agent* const agent = turn.agent;
    BotDriver* const driver = turn.driver;
    UserCommand& command = turn.command;
    const BotSkillLevel& level = rules.skillLevels[driver->skillLevel];
    agent->behavior = decision.behavior;
    hunt(*agent, *driver, decision, self.feet);
    const bool attacks = decision.behavior == nx::agents::Behavior::Attack;
    const std::optional<nx::Vec3> aimSpot =
        attacks ? std::optional{aimSpotOf(*driver, self, decision, targets, command.duration)} : std::nullopt;
    command.viewAngles =
        turnView(self.view, viewWanted(decision.intent, aimSpot, self), driver->turn,
                 attacks ? rules.look.attacking : rules.look.roaming, rules.look, command.duration);
    moveAlong(command, decision.intent, self.maxSpeed);
    const bool presses = pullTrigger(driver->trigger,
                                     {.pursued = decision.target,
                                      .eye = self.eye,
                                      .viewYaw = command.viewAngles.yaw,
                                      .aimSpot = aimSpot,
                                      .heldSlot = self.heldSlot,
                                      .skill = level.skill,
                                      .attackDelay = level.attackDelay,
                                      .now = time.now,
                                      .rate = time.rate},
                                     rules.fire, driver->aim);
    if (presses) {
        command.buttons.set(Button::Attack);
    }
    return command;
}

} // namespace cs
