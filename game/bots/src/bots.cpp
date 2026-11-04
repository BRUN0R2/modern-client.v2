// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.bots;

import std;
import nx.core;
import nx.math;
import nx.assets;
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
/// The stream of random numbers how far off a bot places what it hears draws from, of the player it plays.
constexpr std::string_view kHearingStream = "bot hearing";
/// The stream of random numbers whether a hurt bot panics and the spot it looks at draw from, of the player
/// it plays.
constexpr std::string_view kPanicStream = "bot panic";
/// The stream of random numbers how a bot dodges and the range it keeps draw from, of the player it plays.
constexpr std::string_view kFightingStream = "bot fighting";
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
    const nx::Vec3 way = intent.drivenWay();
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
    std::size_t decision = 0; ///< Its index among those of the tick's living bots.
};

Bots::Bots(CreateToken /*token*/, Game& owner, BotRules botRules, const nx::navigation::NavigationMesh& mesh,
           nx::jobs::JobSystem& jobSystem)
    : game{&owner}, rules{std::move(botRules)}, jobs{&jobSystem}, walked{&mesh}, deciders{mesh} {}

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
                                                  .aim = nx::Random{player.value, kAimStream},
                                                  .hearing = nx::Random{player.value, kHearingStream},
                                                  .panic = nx::Random{player.value, kPanicStream},
                                                  .fighting = nx::Random{player.value, kFightingStream}});
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

auto Bots::kickNamed(std::string_view name) -> nx::Result<void, BotKickError> {
    const auto named =
        std::ranges::find_if(bots, [this, name](PlayerIndex bot) { return game->nameOf(bot) == name; });
    if (named == bots.end()) {
        return nx::fail(BotKickError::NoSuchBot, std::format("no bot is named {}", name));
    }
    kick(named);
    return {};
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
    const std::uint32_t terroristWins = game->wins()[Team::Terrorists];
    const std::uint32_t counterTerroristWins = game->wins()[Team::CounterTerrorists];
    Team team = terrorists > counterTerrorists ? Team::Terrorists : Team::CounterTerrorists;
    if (terrorists == counterTerrorists && terroristWins != counterTerroristWins) {
        team = terroristWins > counterTerroristWins ? Team::Terrorists : Team::CounterTerrorists;
    } else if (terrorists == counterTerrorists) {
        // Teams and wins equal: one at random, RANDOM_LONG(0, 1) == 0 picking the counter-terrorists.
        nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kQuotaStream};
        team = random.below(kPlayingTeams) == 0 ? Team::CounterTerrorists : Team::Terrorists;
    }
    if (!kickFrom(team)) {
        std::ignore = kickFrom(opponentOf(team));
    }
}

template <typename Each>
void Bots::inBatches(nx::jobs::JobDesc desc, std::size_t count, const Each& each) {
    const std::size_t batchCount = jobs->batchCountOf(count, nx::agents::kSmallestBatch);
    while (queries.size() < batchCount) {
        queries.emplace_back(*walked);
    }
    jobs->forBatches(desc, count, nx::agents::kSmallestBatch,
                     [this, &each](std::size_t batch, std::size_t first, std::size_t last) {
                         for (std::size_t index = first; index < last; ++index) {
                             each(index, queries[batch]);
                         }
                     });
}

auto Bots::targetOfPlayer(const Sights& sights, PlayerIndex player) -> const PerceptionTarget* {
    // In the order of their slots.
    const auto found = std::ranges::lower_bound(
        sights.targets, player.value, {}, [](const PerceptionTarget& target) { return target.player.value; });
    return found != sights.targets.end() && found->player == player ? &*found : nullptr;
}

auto Bots::targetOfEntity(const Sights& sights, nx::ecs::Entity entity) -> const PerceptionTarget* {
    const auto found = std::ranges::lower_bound(sights.present, entity);
    return found != sights.present.end() && *found == entity
               ? sights.presentTargets[static_cast<std::size_t>(found - sights.present.begin())]
               : nullptr;
}

auto Bots::commands() -> std::vector<PlayerCommand> {
    const nx::game::TickTime time = game->time();
    const std::vector<PerceptionTarget> targets = game->perceptionTargets();
    // The enemies there still are, living: a bot forgets the others (nx::agents::Brain::forgetAbsent).
    std::vector<const PerceptionTarget*> presentTargets =
        targets | std::views::transform([](const PerceptionTarget& target) { return &target; }) |
        std::ranges::to<std::vector>();
    std::ranges::sort(presentTargets, {}, &PerceptionTarget::entity);
    const std::vector<nx::ecs::Entity> present =
        presentTargets | std::views::transform(&PerceptionTarget::entity) | std::ranges::to<std::vector>();
    // Nothing moves while the bots perceive: what is solid to their sights is gathered once.
    const std::vector<nx::game::SolidBrushEntity> solids = game->sightSolids();
    const std::vector<GameNoise> noises = game->noises();
    const Sights sights{.time = time,
                        .targets = targets,
                        .present = present,
                        .presentTargets = presentTargets,
                        .solids = solids,
                        .noises = noises};
    // The bodies the bots steer around, players' and others'.
    const std::vector<nx::agents::Body> bodies = bodiesOf(game->movingBodies());
    std::vector<Turn> turns;
    turns.reserve(bots.size());
    for (const PlayerIndex bot : bots) {
        turns.push_back(beginTurn(bot, time));
    }
    // What a bot perceives does not depend on what another perceives or decides, nor its decision on
    // another's.
    inBatches({.label = "bots.perceive", .priority = nx::jobs::Priority::Critical}, turns.size(),
              [this, &turns, &sights](std::size_t index, nx::navigation::NavigationQuery& query) {
                  if (turns[index].self.isAlive) {
                      perceiveInTurn(turns[index], sights, query);
                  }
              });
    std::vector<nx::agents::DecisionRequest> requests;
    requests.reserve(turns.size());
    for (Turn& turn : turns) {
        if (turn.self.isAlive) {
            const PerceptionTarget* const self = targetOfPlayer(sights, turn.player);
            turn.decision = requests.size();
            requests.push_back({.brain = &turn.agent->brain,
                                .feet = turn.self.feet,
                                .velocity = turn.self.velocity,
                                .maxSpeed = turn.self.maxSpeed,
                                .body = self != nullptr ? std::optional{self->entity} : std::nullopt,
                                .stuck = &turn.agent->stuck,
                                .elapsed = turn.command.duration});
        }
    }
    std::vector<nx::agents::Decision> decisions(requests.size());
    deciders.decide(*jobs, requests, decisions, bodies);
    inBatches({.label = "bots.commands", .priority = nx::jobs::Priority::Critical}, turns.size(),
              [this, &turns, &decisions, &sights](std::size_t index, nx::navigation::NavigationQuery& query) {
                  Turn& turn = turns[index];
                  if (turn.self.isAlive) {
                      finishTurn(turn, decisions[turn.decision], sights, query);
                  }
              });
    std::vector<PlayerCommand> made;
    made.reserve(turns.size());
    for (const Turn& turn : turns) {
        made.push_back(PlayerCommand{.player = turn.player, .command = turn.command});
    }
    return made;
}

auto Bots::perceive(PlayerIndex player, nx::agents::Agent& agent, const PlayerOwnState& self,
                    const Sights& sights) -> std::vector<nx::agents::Observation> {
    const std::span<const PerceptionTarget> targets = sights.targets;
    const std::span<const nx::agents::Memory> memories = agent.brain.memories();
    const bool looksAround =
        looksAroundAt(player, sights.time.now, sights.time.rate, rules.sightInterval) && !targets.empty();
    const auto isSeen = [](const nx::agents::Memory& memory) {
        return memory.isVisible;
    };
    std::vector<nx::agents::Observation> seen;
    if (!looksAround && std::ranges::none_of(memories, isSeen)) {
        return seen;
    }
    const ViewCone cone = viewConeOf(self.eye, self.view.yaw, rules.fieldOfView);
    const nx::assets::LeafSet leaves = game->leavesSeenFrom(self.eye);
    // The first spot of it in its cone that a line reaches is the one it aims at.
    const auto lookAt = [&](const PerceptionTarget& target) {
        for (const nx::Vec3 spot : sightSpotsOf(target, self.eye, rules.spots)) {
            if (cone.holds(spot) && game->isInLeaves(leaves, spot) &&
                game->seesSpot(sights.solids, self.eye, spot)) {
                seen.push_back({.entity = target.entity, .position = target.feet, .aimAt = spot});
                return;
            }
        }
    };
    for (const nx::agents::Memory& memory : memories) {
        if (memory.isVisible) {
            if (const PerceptionTarget* const target = targetOfEntity(sights, memory.observation.entity)) {
                lookAt(*target);
            }
        }
    }
    if (!looksAround) {
        return seen;
    }
    const std::size_t budget = std::min<std::size_t>(targets.size(), rules.perceptionBudget);
    for (std::size_t looked = 0; looked < budget; ++looked) {
        const PerceptionTarget& target = targets[(agent.nextCandidate + looked) % targets.size()];
        const auto isLookedAt = [&target](const nx::agents::Memory& memory) {
            return memory.isVisible && memory.observation.entity == target.entity;
        };
        if (target.player != player && target.team != self.team &&
            std::ranges::none_of(memories, isLookedAt)) {
            lookAt(target);
        }
    }
    agent.nextCandidate = (agent.nextCandidate + budget) % targets.size();
    return seen;
}

void Bots::hunt(nx::agents::Agent& agent, BotDriver& driver, const nx::agents::Decision& decision,
                nx::Vec3 feet, nx::navigation::NavigationQuery& query) {
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
                     const Sights& sights, nx::Seconds duration) -> nx::Vec3 {
    const nx::game::TickTime time = sights.time;
    const BotSkill skill = rules.skillLevels[driver.skillLevel].skill;
    nx::invariant(decision.intent.lookAt.has_value() && decision.target.has_value(),
                  "Bots: an attack looks at the target it pursues");
    // The enemy it attacks it sees: it is among the targets of the tick.
    const PerceptionTarget* const enemy = targetOfEntity(sights, *decision.target);
    nx::invariant(enemy != nullptr, "Bots: the enemy a bot attacks is alive");
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

auto Bots::beginTurn(PlayerIndex player, nx::game::TickTime time) -> Turn {
    Turn turn{.player = player,
              .agent = game->playerComponent<nx::agents::Agent>(player),
              .driver = game->playerComponent<BotDriver>(player),
              .self = game->ownStateOf(player)};
    nx::invariant(turn.agent != nullptr && turn.driver != nullptr,
                  "Bots: a bot's body is an agent with its driver");
    BotDriver* const driver = turn.driver;
    driver->lastCommand = CommandNumber{driver->lastCommand.value + 1};
    turn.command = UserCommand{
        .number = driver->lastCommand, .duration = nx::tickLengthOf(time.rate), .viewAngles = turn.self.view};
    return turn;
}

void Bots::perceiveInTurn(Turn& turn, const Sights& sights, nx::navigation::NavigationQuery& query) {
    const nx::game::TickTime time = sights.time;
    const PlayerIndex player = turn.player;
    nx::agents::Agent* const agent = turn.agent;
    BotDriver* const driver = turn.driver;
    if (driver->lifeSpawnedAt != turn.self.spawnedAt) {
        // Spawned again, killed or alive at a round's start (SpawnBot's ResetValues): what it knew, where it
        // went, how it turned, aimed and fired are of its last life.
        const BotSkillLevel& level = rules.skillLevels[driver->skillLevel];
        agent->brain = nx::agents::Brain{brainRulesOf(rules, level.reactionTime)};
        agent->stuck.reset();
        driver->turn = {};
        driver->aimError = {};
        driver->trigger = {};
        driver->panicSpot.reset();
        driver->combat = spawnedCombat(rules.combat, driver->fighting);
        driver->lifeSpawnedAt = turn.self.spawnedAt;
    }
    agent->brain.forgetAbsent(sights.present);
    agent->brain.perceive(perceive(player, *agent, turn.self, sights), time.now, time.rate);
    agent->brain.sense(heardBy({.team = turn.self.team, .ear = turn.self.eye}, sights.noises, rules.hearing,
                               query, driver->hearing),
                       time.now, time.rate);
    // Felt where it is, last: what it touches it knows better than what it hears.
    if (const PerceptionTarget* const self = targetOfPlayer(sights, player)) {
        const Toucher toucher{.entity = self->entity,
                              .team = self->team,
                              .origin = self->origin,
                              .box = self->isDucked ? kDuckedBox : kStandingBox};
        agent->brain.sense(touchedBy(toucher, sights.targets), time.now, time.rate);
    }
    if (const std::optional<nx::Vec3> spot = panic(*agent, *driver, turn.self)) {
        driver->panicSpot = spot;
    }
}

void Bots::fight(UserCommand& command, BotDriver& driver, const PerceptionTarget& body,
                 const PlayerOwnState& self, nx::ecs::Entity enemy, const Sights& sights) {
    const nx::game::TickTime time = sights.time;
    const PerceptionTarget* const foe = targetOfEntity(sights, enemy);
    nx::invariant(foe != nullptr, "Bots: a bot attacks an enemy alive");
    const BotSkillLevel& level = rules.skillLevels[driver.skillLevel];
    // OnEnter's line from its origin crouched, lower by as much standing, to its enemy's eye.
    const bool seesCrouched =
        !driver.combat.isFighting &&
        game->seesSpot(sights.solids,
                       body.origin -
                           nx::Vec3{.z = body.isDucked ? 0.0F : rules.combat.crouchSightDrop.value()},
                       foe->eye);
    const BotFightMove move = fightMove(driver.combat,
                                        {.origin = body.origin,
                                         .velocity = self.velocity,
                                         .enemyOrigin = foe->origin,
                                         .enemyView = foe->view,
                                         .skill = level.skill,
                                         .aggression = level.aggression,
                                         .holdsKnife = self.heldSlot == WeaponSlot::Knife,
                                         .seesCrouched = seesCrouched,
                                         .maxSpeed = self.maxSpeed,
                                         .now = time.now,
                                         .rate = time.rate},
                                        rules.combat, driver.fighting);
    command.move.forward = move.forward;
    command.move.side = move.side;
    if (move.jumps) {
        command.buttons.set(Button::Jump);
    }
    if (move.crouches) {
        command.buttons.set(Button::Duck);
    }
}

auto Bots::panic(const nx::agents::Agent& agent, BotDriver& driver, const PlayerOwnState& self)
    -> std::optional<nx::Vec3> {
    const std::optional<HurtFrom>& hurt = self.hurtFrom;
    if (!hurt || hurt->team == self.team || driver.panicSpot) {
        return std::nullopt;
    }
    const BotPanicRules& panicRules = rules.panic;
    const BotSkill skill = rules.skillLevels[driver.skillLevel].skill;
    // TakeDamage: it panics unless it sees an enemy as it attacks, and then at random, the less skilled
    // the likelier.
    const bool seesEnemy = std::ranges::any_of(agent.brain.memories(), &nx::agents::Memory::isVisible);
    const bool attacks = agent.behavior == nx::agents::Behavior::Attack;
    const float unskill = 1.0F - skill.value();
    if (attacks && seesEnemy && driver.panic.between(0.0F, 1.0F) >= panicRules.chance * unskill * unskill) {
        return std::nullopt;
    }
    // Panic: the way it looks and across it, level.
    const nx::AngleVectors axes = nx::angleVectors(nx::EulerAngles{.yaw = self.view.yaw});
    const nx::Vec3 ahead = axes.forward;
    const nx::Vec3 across{.x = -ahead.y, .y = ahead.x};
    nx::Vec3 offset{};
    if (skill >= panicRules.skilledFrom) {
        constexpr float kHalfRightAngleCosine = 0.7071F; // c45: within 45 degrees of a way
        const float shift = driver.panic.between(-panicRules.shift.value(), panicRules.shift.value());
        const float along = nx::dot(hurt->toward, ahead);
        if (along > kHalfRightAngleCosine) {
            offset = (ahead * panicRules.near.value()) + (across * shift);
        } else if (along < -kHalfRightAngleCosine) {
            offset = (ahead * -panicRules.near.value()) + (across * shift);
        } else if (nx::dot(hurt->toward, across) > 0.0F) {
            offset = (across * panicRules.near.value()) + (ahead * shift);
        } else {
            offset = (across * -panicRules.near.value()) + (ahead * shift);
        }
    } else {
        const float side = driver.panic.between(-panicRules.farSide.value(), panicRules.farSide.value());
        offset = (ahead * -panicRules.far.value()) + (across * side);
    }
    const float height = driver.panic.between(-panicRules.height.value(), panicRules.height.value());
    return self.eye + nx::Vec3{.x = offset.x, .y = offset.y, .z = height};
}

void Bots::finishTurn(Turn& turn, const nx::agents::Decision& decision, const Sights& sights,
                      nx::navigation::NavigationQuery& query) {
    const nx::game::TickTime time = sights.time;
    const PlayerOwnState& self = turn.self;
    nx::agents::Agent* const agent = turn.agent;
    BotDriver* const driver = turn.driver;
    UserCommand& command = turn.command;
    const BotSkillLevel& level = rules.skillLevels[driver->skillLevel];
    agent->behavior = decision.behavior;
    hunt(*agent, *driver, decision, self.feet, query);
    const bool attacks = decision.behavior == nx::agents::Behavior::Attack;
    const std::optional<nx::Vec3> aimSpot =
        attacks ? std::optional{aimSpotOf(*driver, self, decision, sights, command.duration)} : std::nullopt;
    // Hurt, it looks where it panics to, above all but an enemy it attacks (SetLookAt's PRIORITY_HIGH).
    if (attacks) {
        driver->panicSpot.reset();
    }
    const std::optional<nx::Vec3> looked = aimSpot ? aimSpot : driver->panicSpot;
    const nx::EulerAngles wanted = viewWanted(decision.intent, looked, self);
    command.viewAngles =
        turnView(self.view, wanted, driver->turn, attacks ? rules.look.attacking : rules.look.roaming,
                 rules.look, command.duration);
    if (driver->panicSpot && std::abs(nx::normalizeAngle(command.viewAngles.yaw - wanted.yaw).value()) <=
                                 rules.panic.lookedWithin.value()) {
        driver->panicSpot.reset();
    }
    moveAlong(command, decision.intent, self.maxSpeed);
    if (attacks) {
        const PerceptionTarget* const body = targetOfPlayer(sights, turn.player);
        nx::invariant(body != nullptr, "Bots: a bot attacks alive");
        fight(command, *driver, *body, self, *decision.target, sights);
    } else {
        endFight(driver->combat);
    }
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
}

} // namespace cs
