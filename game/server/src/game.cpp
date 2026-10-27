// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.ecs;
import nx.collision;
import nx.game;
import nx.navigation;
import cs.shared;

namespace cs {

namespace {

struct PlayerBodyLink {
    PlayerIndex player{};
};

/// The stream of random numbers a spawn's weapons draw from.
constexpr std::string_view kSpawnStream = "spawn";
/// How far above its spawn point a player spawns (GetPlayerSpawnSpot).
constexpr nx::HammerUnits kSpawnLift{1.0F};
/// How often the rules look at the restart the cvars ask for (Think's periodic part).
constexpr nx::Seconds kPeriodicThinkInterval{1.0F};
/// The stream of random numbers the sounds of a command's steps draw from.
constexpr std::string_view kFootstepStream = "footsteps";
constexpr std::string_view kBreathStream = "breath";
/// What the game's rules log, as the original's server logs the rounds.
constexpr nx::log::Category kLog{"game"};
/// The stream of random numbers a joining player's character is drawn from, and its models' animations'.
constexpr std::string_view kAppearanceStream = "appearance";
constexpr std::string_view kAnimationStream = "player animation";
/// The number PostThink compares a player's gait sequence with, ACT_FLY's.
constexpr std::uint32_t kFlyActivityNumber = 5;

[[nodiscard]] auto eyeOf(const MovementState& player) -> nx::Vec3 {
    return player.origin + nx::Vec3{.z = player.viewHeight.value()};
}

/// Whether what a trace met in `world`, `entity`, is the world (none) or a brush entity (IsBSPModel).
[[nodiscard]] auto isBrushEntityOrWorld(const nx::ecs::World& world, std::optional<nx::ecs::Entity> entity)
    -> bool {
    return !entity || world.get<nx::game::BrushModel>(*entity) != nullptr;
}

/// What a mark stamped on `entity` of `world` is on: the world for none, else the entity's brush model
/// where the game has it now.
[[nodiscard]] auto stampTargetOf(const nx::ecs::World& world, std::optional<nx::ecs::Entity> entity)
    -> StampTarget {
    if (!entity) {
        return {};
    }
    // What a mark lies on is a solid brush entity: it has its model and its place.
    const nx::game::BrushModel* const model = world.get<nx::game::BrushModel>(*entity);
    const nx::game::Transform* const transform = world.get<nx::game::Transform>(*entity);
    nx::expects(model != nullptr && transform != nullptr, "stampTargetOf: a brush entity");
    if (model == nullptr || transform == nullptr) {
        return {};
    }
    return StampTarget{.model = model->index, .origin = transform->origin, .angles = transform->angles};
}

/// Where the game has the brush entities of `world`, and which of their looks they show.
[[nodiscard]] auto brushEntityStatesOf(nx::ecs::World& world) -> std::vector<BrushEntityState> {
    std::vector<BrushEntityState> states;
    nx::ecs::Query<const nx::game::BrushModel&, const nx::game::Transform&,
                   nx::ecs::Optional<const nx::game::Waves>,
                   nx::ecs::Optional<const nx::game::AlternateTextures>>{world}
        .forEach([&states](const nx::game::BrushModel& model, const nx::game::Transform& transform,
                           const nx::game::Waves* waves, const nx::game::AlternateTextures* textures) {
            states.push_back({.model = model.index,
                              .origin = transform.origin,
                              .angles = transform.angles,
                              .waveHeight = waves != nullptr ? waves->height : nx::HammerUnits{},
                              .showsAlternateTextures = textures != nullptr && textures->isShown});
        });
    return states;
}

} // namespace

Game::Game(CreateToken /*token*/, const GameContent& content, const GameMap& map, GameCvars cvars,
           GameModels models)
    : m_content{&content}, m_map{&map}, m_cvars{std::move(cvars)}, m_models{std::move(models)},
      // The first round starts with the map, the first moment of the game's clock.
      m_rounds{Rounds::start(map.objectives(), m_cvars.roundLengths(), nx::GameTime{})},
      m_accounts{content.economy}, lightStyles{mapLightStyles(map.map(), content.lightStyles)} {}

auto Game::start(const GameContent& content, const GameMap& map, GameCvars cvars)
    -> nx::Result<std::unique_ptr<Game>, GameError> {
    GameModels models = GameModels::read(content.vfs, map.map(), content.characters);
    if (!models.missing().empty()) {
        nx::log::warning(kLog, "models the map's hostages play that cannot be read: {}",
                         models.missing() | std::views::join_with(std::string_view{", "}) |
                             std::ranges::to<std::string>());
    }
    auto game = std::make_unique<Game>(CreateToken{}, content, map, std::move(cvars), std::move(models));
    if (auto spawned = spawnGame(map.map(), content.entityClasses, game->m_world); !spawned) {
        return nx::fail(GameError::Unspawnable, std::move(spawned.error()));
    }
    if (auto registered = registerHostages(game->m_world, game->m_models.played(), content.hitGroups);
        !registered) {
        return nx::fail(GameError::Unspawnable, std::move(registered.error()));
    }
    spawnHostages(game->m_world, map.hulls(), game->m_models.hostageSpawns());
    if (auto registered = registerEntityBodies(game->m_world, content.entities); !registered) {
        return nx::fail(GameError::Unspawnable, std::move(registered.error()));
    }
    if (auto registered = game->m_world.registerComponent<PlayerBodyLink>("cs::PlayerBodyLink");
        !registered) {
        return nx::fail(GameError::Unspawnable, std::move(registered.error()));
    }
    return game;
}

auto Game::join(Team team, AutoSwitch autoSwitch) -> PlayerIndex {
    const PlayerIndex index{nx::narrow<std::uint32_t>(m_players.size())};
    // HandleMenu_ChooseAppearance's auto-select: one of the team's characters, each as likely.
    const std::vector<CharacterIndex> characters = m_content->characters.ofTeam(team);
    nx::Random appearance{index.value, kAppearanceStream};
    Player& player = m_players.emplace_back(
        Player{.team = team,
               .autoSwitch = autoSwitch,
               .character = characters.at(appearance.below(characters.size())),
               .status = PlayerStatus{.health = nx::HitPoints{}, .money = m_cvars.startingMoney()},
               .inventory = Inventory{m_content->weapons},
               .stepVoice = StepVoice{m_content->footsteps},
               .breath = Breath{m_content->breath, m_content->playerHarm}});
    player.body = m_world.spawn(nx::game::Transform{}, bodyOf(player.movement), PlayerBodyLink{index});
    nx::log::info(kLog, "a player joins the {}", nx::enumName(team));
    if (canSpawn(player)) {
        spawn(player);
    } else {
        observe(player);
    }
    checkWinConditions();
    return index;
}

auto Game::canSpawn(const Player& player) const -> bool {
    if (player.spawnsThisRound > 0) {
        return false;
    }
    const auto hasPlayers = [this](Team team) {
        return std::ranges::contains(m_players, team, &Player::team);
    };
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    return !hasPlayers(Team::Terrorists) || !hasPlayers(Team::CounterTerrorists) ||
           m_rounds.sinceStartAt(time.now, time.rate) <= kJoinSpawnWindow;
}

void Game::observe(Player& player) {
    const SpawnPoint point = nextSpawnPoint(player.team);
    standAt(player, point.origin + nx::Vec3{.z = kSpawnLift.value()});
    player.movement.isDead = true;
    *m_world.get<nx::game::Body>(player.body) = bodyOf(player.movement);
    player.turnedView = nx::EulerAngles{.yaw = point.yaw};
    player.view = *player.turnedView;
}

void Game::checkWinConditions() {
    TeamCounts counts{};
    for (const Player& player : m_players) {
        TeamCount& count = counts[player.team];
        if (player.status.isAlive()) {
            ++count.alive;
        } else {
            ++count.dead;
        }
    }
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    if (const std::optional<RoundEndReason> reason =
            m_rounds.checkWinConditions(counts, time.now, time.rate)) {
        roundEnded(*reason);
    }
}

auto Game::boxesMetBy(const Player& mover) -> std::vector<SolidBox> {
    return solidBoxesOf(m_world, mover.body);
}

auto Game::playerAt(PlayerIndex index) -> Player& {
    nx::expects(index.value < m_players.size(), "Game: a player of the game");
    return m_players[index.value];
}

void Game::give(PlayerIndex index, WeaponIndex weapon) {
    Player& player = playerAt(index);
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kSpawnStream};
    std::ignore = player.inventory.give(weapon, player.autoSwitch, time, random);
    player.movement.maxSpeed = maxSpeed(player);
}

void Game::setAutoSwitch(PlayerIndex index, AutoSwitch autoSwitch) {
    playerAt(index).autoSwitch = autoSwitch;
}

auto Game::toggleNoclip(PlayerIndex index) -> bool {
    MovementState& movement = playerAt(index).movement;
    const bool isFlying = movement.moveType == MoveType::Walk;
    movement.moveType = isFlying ? MoveType::Noclip : MoveType::Walk;
    return isFlying;
}

void Game::placeEye(PlayerIndex index, nx::Vec3 eye) {
    Player& player = playerAt(index);
    standAt(player, eye - nx::Vec3{.z = player.movement.viewHeight.value()});
}

auto Game::snapshotFor(PlayerIndex index) -> PlayerSnapshot {
    const Player& player = playerAt(index);
    std::vector<PlayerEntityState> others;
    for (std::uint32_t other = 0; other < m_players.size(); ++other) {
        if (other == index.value) {
            continue;
        }
        const Player& shown = m_players[other];
        const OwnedWeapon* const held = shown.inventory.held();
        const Animation& torso = shown.animation.torso;
        others.push_back(
            PlayerEntityState{.player = PlayerIndex{other},
                              .character = shown.character,
                              .weapon = held != nullptr ? std::optional{held->weapon} : std::nullopt,
                              .origin = shown.movement.origin,
                              .angles = bodyAnglesOf(shown.view),
                              .sequence = torso.sequence,
                              .frame = torso.frame,
                              .advancedAt = torso.advancedAt,
                              .framerate = torso.framerate,
                              .gaitSequence = shown.animation.gaitSequence});
    }
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    return PlayerSnapshot{.time = time.now,
                          .rate = time.rate,
                          .player = player.movement,
                          .status = player.status,
                          .inventory = player.inventory,
                          .roundTimer = player.roundTimer,
                          .turnedView = player.turnedView,
                          .wasPlaced = player.wasPlaced,
                          .sounds = player.sounds,
                          .impacts = m_impacts,
                          .brushEntities = brushEntityStatesOf(m_world),
                          .hostages = hostageStatesOf(m_world),
                          .players = std::move(others),
                          .entities = entityStatesOf(m_world),
                          .lightStyles = lightStyles};
}

void Game::tick(std::span<const PlayerCommand> commands, std::span<const EntityCommand> entities) {
    m_impacts.clear();
    for (Player& player : m_players) {
        player.turnedView.reset();
        player.wasPlaced = false;
        player.sounds.clear();
    }
    for (const PlayerCommand& command : commands) {
        runCommand(playerAt(command.player), command.command);
    }
    for (const EntityAttack& strike : runEntityBodies(m_world, m_map->hulls(), entities)) {
        const PlayerBodyLink* const link = m_world.get<PlayerBodyLink>(strike.target);
        if (link != nullptr) {
            hurt(playerAt(link->player), {.damage = strike.attack.damage, .kind = HarmKind::Melee});
        } else {
            std::ignore = nx::game::attackEntity(m_world, strike.target, strike.attack);
        }
    }
    thinkRules();
    const nx::collision::BrushHulls& hulls = m_map->hulls();
    nx::game::runThinks(m_world, hulls);
    nx::game::runPushers(m_world, hulls);
    for (Player& player : m_players) {
        player.movement.origin = m_world.get<nx::game::Transform>(player.body)->origin;
        // Its weapons' sounds of the tick first, as they come before what the player makes of them.
        std::vector<SoundMade> sounds = player.inventory.takeSounds();
        std::ranges::move(player.sounds, std::back_inserter(sounds));
        player.sounds = std::move(sounds);
    }
    nx::game::advanceTick(m_world);
}

void Game::runCommand(Player& player, const UserCommand& command) {
    const nx::collision::BrushHulls& hulls = m_map->hulls();
    // SV_RunCmd: where the player looks, and the seed of what it does.
    player.view = command.viewAngles;
    player.randomSeed = command.number;
    // The weapon the command selects is drawn before it moves the player, at the speed it allows, as
    // the original's CmdStart selects it.
    player.inventory.select(command, nx::game::tickTimeOf(m_world));
    player.movement.maxSpeed = maxSpeed(player);
    preThink(player, command);
    // After PreThink, which may kill it, as the original's returns then, and its PostThink skips the dead.
    const bool isAlive = player.status.isAlive();
    const MovementState before = player.movement;
    const std::vector<BrushEntity> entities = brushEntitiesOf(m_world);
    const MovementResult moved =
        simulateMovement(player.movement, command, MovementSettings{}, hulls, entities, boxesMetBy(player));
    player.movement = moved.state;
    // A dead player's body is not solid (SOLID_NOT): it touches nothing.
    if (isAlive) {
        hearMovement(player, moved.sounds, entities, command);
        nx::game::touchBrushEntities(m_world, hulls, moved.touchedModels, player.body);
    }
    m_world.get<nx::game::Transform>(player.body)->origin = player.movement.origin;
    *m_world.get<nx::game::Body>(player.body) = bodyOf(player.movement);
    if (isAlive) {
        m_world.get<nx::game::BoxSolidity>(player.body)->box =
            player.movement.isDucked ? kDuckedBox : kStandingBox;
    }
    if (isAlive) {
        nx::game::touchTriggers(m_world, hulls, player.body);
        postThink(player, command, before);
    }
    // As the original's PostThink ends, dead or alive (m_afButtonLast, StudioProcessGait).
    player.lastButtons = command.buttons;
    animateAfterCommand(player, frameTimeOf(command));
}

void Game::preThink(Player& player, const UserCommand& command) {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    if (player.status.isAlive()) {
        nx::Random breathRandom{command.number.value, kBreathStream};
        for (const BreathEvent& event :
             player.breath.breathe(player.movement.waterLevel, player.movement.waterType,
                                   player.movement.moveType, player.timedHarms, time, breathRandom)) {
            if (const auto* const sound = std::get_if<SoundMade>(&event)) {
                player.sounds.push_back(*sound);
            } else {
                hurt(player, std::get<PlayerHarm>(event));
            }
        }
    }
    if (player.status.isAlive()) {
        // Jump: the button going down on the ground, out of deep water and of a jump out of water.
        const MovementState& movement = player.movement;
        if (command.buttons.test(Button::Jump) && !player.lastButtons.test(Button::Jump) &&
            movement.isOnGround && movement.waterLevel < WaterLevel::Waist &&
            movement.waterJumpTime <= nx::Milliseconds{}) {
            animate(player, PlayerAction::Jump);
        }
        // Duck.
        if (command.buttons.test(Button::Duck)) {
            animate(player, PlayerAction::Walk);
        }
    } else if (!player.animation.torso.isFinished) {
        // PlayerDeathThink: its death plays on.
        advanceFrame(player.animation.torso, time.now, time.rate);
    }
    // Dead or not, as the original's PreThink looks at the kept harms before it weighs a death; the dead
    // are given back none.
    healPlayer(player.status,
               player.timedHarms.check(m_content->playerHarm.drowning.givenBack, time.now, time.rate));
}

void Game::postThink(Player& player, const UserCommand& command, const MovementState& before) {
    const nx::collision::BrushHulls& hulls = m_map->hulls();
    const MovementState& movement = player.movement;
    // Each shot goes as far as a bullet does, and where it meets the world or a brush entity it leaves
    // its mark.
    Shooter shooter{
        .eye = eyeOf(movement),
        .view = command.viewAngles,
        .punch = movement.punch,
        .horizontalSpeed = nx::UnitsPerSecond{static_cast<float>(std::sqrt(double{
            (movement.velocity.x * movement.velocity.x) + (movement.velocity.y * movement.velocity.y)}))},
        .isOnGround = movement.isOnGround,
        .isDucked = movement.isDucked,
        .canAttack = !m_rounds.isFrozen()};
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    // What the attacks meet: what is solid, the world, the brush entities and the hostages; a body is
    // an entity the original's Classify makes other than none (a hostage, a vehicle).
    const WeaponTracer tracer = [this, &hulls, &player](nx::collision::Hull hull, nx::Vec3 start,
                                                        nx::Vec3 end) {
        nx::game::ServerTrace trace = nx::game::hullTrace(hull, start, end);
        trace.ignored = player.body;
        const nx::game::SolidHit solid = nx::game::traceSolidsHit(m_world, hulls, trace);
        const nx::game::Classified* const classified =
            solid.entity ? m_world.get<nx::game::Classified>(*solid.entity) : nullptr;
        return WeaponHit{.trace = solid.trace,
                         .entity = solid.entity,
                         .isBrushModel = isBrushEntityOrWorld(m_world, solid.entity),
                         .isBody = classified != nullptr &&
                                   classified->classification != nx::game::Classification::None};
    };
    // ItemPostFrame: unless the player is held back, the use key going down uses what is in front
    // (ImpulseCommands), before the weapon's frame.
    if (command.buttons.test(Button::Use) && !player.lastButtons.test(Button::Use) &&
        !player.inventory.isHeldBack()) {
        use(player, command);
    }
    const WeaponAttacks attacks = player.inventory.frame(command, shooter, tracer, time);
    // As its weapon's code asks the player's model to play it.
    if (attacks.motions.test(HolderMotion::Attacked)) {
        animate(player, PlayerAction::Attack1);
    }
    if (attacks.motions.test(HolderMotion::Reloaded)) {
        animate(player, PlayerAction::Reload);
    }
    for (const Blow& blow : attacks.blows) {
        attackEntity(player, blow.entity,
                     nx::game::Attack{.attacker = player.body,
                                      .damage = blow.damage,
                                      .direction = blow.direction,
                                      .trace = blow.trace});
    }
    const HitMaterial materialOf = [this](const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end) {
        return materialOfHit(hit, start, end);
    };
    for (const Shot& shot : attacks.shots) {
        // What its round meets, as FireBullets3 follows it: a mark on the world or a brush entity
        // (DecalGunshot), none on a hostage; and its damage to what takes it.
        for (const BulletHit& hit : followRound(shot, tracer, materialOf)) {
            if (hit.isBrushModel) {
                m_impacts.push_back(
                    Impact{.position = hit.trace.end, .on = stampTargetOf(m_world, hit.entity)});
            }
            if (hit.entity) {
                attackEntity(player, *hit.entity,
                             nx::game::Attack{.attacker = player.body,
                                              .damage = hit.damage,
                                              .direction = hit.direction,
                                              .trace = hit.trace});
            }
        }
    }
    player.movement.punch = shooter.punch;
    if (const std::optional<nx::Damage> damage =
            landingDamageOf(m_content->playerHarm.fall, before, player.movement)) {
        land(player, *damage);
    }
}

void Game::use(Player& player, const UserCommand& command) {
    const std::optional<nx::ecs::Entity> used = nx::game::playerUse(
        m_world, m_map->hulls(), player.body,
        {.eye = eyeOf(player.movement), .forward = nx::angleVectors(command.viewAngles).forward});
    makeSound(player, used ? m_content->use.found : m_content->use.nothing);
}

void Game::land(Player& player, nx::Damage damage) {
    // Weighed before the damage is cut to its whole points.
    if (damage.value() > player.status.health.value()) {
        makeSound(player, m_content->playerHarm.splat);
    }
    hurt(player, PlayerHarm{.damage = damage, .kind = HarmKind::Fall});
    player.movement.punch.pitch = nx::Degrees{};
}

void Game::hurt(Player& player, const PlayerHarm& harm) {
    if (!player.status.isAlive()) {
        return;
    }
    makeSound(player, m_content->playerHarm.pain);
    if (harmPlayer(player.status, player.timedHarms, harm).isKilled) {
        die(player);
    }
}

void Game::die(Player& player) {
    nx::log::info(kLog, "a player dies");
    makeSound(player, m_content->playerHarm.death);
    player.movement.isDead = true;
    m_world.remove<nx::game::BoxSolidity>(player.body);
    *m_world.get<nx::game::Body>(player.body) = bodyOf(player.movement);
    animate(player, PlayerAction::Die);
    // Its weapons go with it (PackDeadPlayerItems; the box it drops comes with dropping, TODO.md).
    player.inventory.removeAll();
    checkWinConditions();
}

void Game::animate(Player& player, PlayerAction action) {
    const PlayedModel* const model = m_models.characterModel(player.character);
    if (model == nullptr) {
        return;
    }
    const OwnedWeapon* const held = player.inventory.held();
    const MovementState& movement = player.movement;
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    nx::Random random{player.randomSeed.value, kAnimationStream};
    setPlayerAnimation(
        player.animation, action,
        PlayerAnimationContext{
            .model = model->view,
            .weaponAnimation =
                held != nullptr
                    ? std::string_view{m_content->weapons.weapon(held->weapon).models.playerAnimation}
                    : std::string_view{},
            .isDucked = movement.isDucked,
            .isOnGround = movement.isOnGround,
            .waterLevel = movement.waterLevel,
            .speed = nx::UnitsPerSecond{std::hypot(movement.velocity.x, movement.velocity.y)},
            .now = time.now,
            .rate = time.rate},
        random);
}

void Game::animateAfterCommand(Player& player, nx::Seconds frameTime) {
    const PlayedModel* const model = m_models.characterModel(player.character);
    if (model == nullptr) {
        return;
    }
    if (player.status.isAlive()) {
        const MovementState& movement = player.movement;
        if (movement.velocity.x != 0.0F || movement.velocity.y != 0.0F) {
            if (movement.isOnGround || movement.waterLevel > WaterLevel::Feet) {
                animate(player, PlayerAction::Walk);
            }
        } else if (player.animation.gaitSequence.value != kFlyActivityNumber) {
            animate(player, PlayerAction::Idle);
        }
        const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
        advanceFrame(player.animation.torso, time.now, time.rate);
    }
    processGait(player.animation.gait, *model->view, player.animation.gaitSequence, player.movement.origin,
                bodyAnglesOf(player.view), frameTime, player.animation.torso.framerate);
}

void Game::makeSound(Player& player, const GameSound& sound) {
    player.sounds.push_back(
        SoundMade{.sound = &sound, .at = nx::game::tickTimeOf(m_world).now, .volume = sound.volume});
}

void Game::thinkRules() {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    const RoundEvents events = m_rounds.think(time.now, time.rate, m_cvars.roundLengths());
    if (events.test(RoundEvent::FreezeEnded)) {
        nx::log::info(kLog, "the round's freeze is over");
        for (Player& player : m_players) {
            player.roundTimer = m_rounds.timerAt(time.now, time.rate);
            player.movement.maxSpeed = maxSpeed(player);
        }
    }
    if (const std::optional<RoundEndReason> reason = m_rounds.end();
        events.test(RoundEvent::Ended) && reason) {
        roundEnded(*reason);
    }
    std::uint32_t hostagesHeld = 0;
    if (events.test(RoundEvent::RoundStarted) || events.test(RoundEvent::GameRestarted)) {
        // RestartRound puts the hostages back as it counts what they bring, before it pays.
        hostagesHeld = repositionHostages(m_world, m_map->hulls(), m_content->economy.hostageBonus);
    }
    if (events.test(RoundEvent::RoundStarted)) {
        nx::log::info(kLog, "a round starts");
        payForRound(hostagesHeld);
    }
    if (events.test(RoundEvent::GameRestarted)) {
        nx::log::info(kLog, "the game restarts");
        m_accounts.restartGame();
        for (Player& player : m_players) {
            player.status = PlayerStatus{.money = m_cvars.startingMoney()};
            player.inventory.removeAll();
            equip(player);
        }
    }
    if (events.test(RoundEvent::RoundStarted) || events.test(RoundEvent::GameRestarted)) {
        // RestartRound counts the spawns of the round anew before the players spawn again.
        for (Player& player : m_players) {
            player.spawnsThisRound = 0;
        }
        for (Player& player : m_players) {
            spawn(player);
        }
        // After the players spawn, as the original's RestartRound cleans the map up (CleanUpMap).
        nx::game::restartBrushEntities(m_world, m_map->hulls());
    }
    // Last, as the original's Think reads them once a second after the round's checks.
    if (time.now > m_nextPeriodicThink) {
        if (const std::optional<nx::Seconds> delay = m_cvars.takeRestart()) {
            nx::log::info(kLog, "the game restarts in {}", *delay);
            m_rounds.restartGameIn(*delay, time.now, time.rate);
        }
        m_nextPeriodicThink = time.now.after(kPeriodicThinkInterval, time.rate);
    }
}

void Game::roundEnded(RoundEndReason reason) {
    nx::log::info(kLog, "the round is over: {}", nx::enumName(reason));
    m_accounts.roundEnded(reason, m_map->objectives());
    const std::optional<Team> winner = winnerOf(reason);
    const bool paysLivingLosers = m_content->economy.roundEnd(reason).paysLivingLosers;
    for (Player& player : m_players) {
        if (winner && *winner != player.team && player.status.isAlive() && !paysLivingLosers) {
            player.isPaidNextRound = false;
        }
    }
}

void Game::attackEntity(Player& player, nx::ecs::Entity entity, const nx::game::Attack& attack) {
    const nx::game::Harm harm = nx::game::attackEntity(m_world, entity, attack);
    if (m_world.has<Hostage>(entity)) {
        // The hostage's TakeDamage takes it from its attacker's account.
        player.status.money = addMoney(player.status.money,
                                       nx::Money{} - hostageHarmCost(harm, m_content->economy.hostageHarm));
    }
}

void Game::payForRound(std::uint32_t hostagesHeld) {
    const nx::EnumArray<Team, nx::Money> paid = m_accounts.roundStarts(m_map->objectives(), hostagesHeld);
    for (Player& player : m_players) {
        if (player.isPaidNextRound) {
            player.status.money = addMoney(player.status.money, paid[player.team]);
        }
    }
}

void Game::spawn(Player& player) {
    if (!player.status.isAlive()) {
        // Killed, it spawns without armor (Spawn), given what a player of its team spawns with
        // (GiveDefaultItems), as its weapons went with it.
        player.status.armor = nx::ArmorPoints{};
        equip(player);
    }
    const SpawnPoint point = nextSpawnPoint(player.team);
    standAt(player, point.origin + nx::Vec3{.z = kSpawnLift.value()});
    player.status.health = kSpawnHealth;
    player.isPaidNextRound = true;
    ++player.spawnsThisRound;
    player.inventory.spawned();
    player.turnedView = nx::EulerAngles{.yaw = point.yaw};
    player.view = *player.turnedView;
    spawnAnimation(player.animation);
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    player.breath.fill(time);
    player.timedHarms.spawned();
    player.roundTimer = m_rounds.timerAt(time.now, time.rate);
    if (!m_world.has<nx::game::BoxSolidity>(player.body)) {
        m_world.add(player.body, nx::game::BoxSolidity{.box = kStandingBox});
    }
}

void Game::equip(Player& player) {
    const Loadout* const loadout = m_content->loadouts.find(player.team);
    if (loadout == nullptr) {
        return;
    }
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kSpawnStream};
    for (const std::string& weapon : loadout->weapons) {
        // readLoadouts checked that the catalog has each.
        std::ignore =
            player.inventory.give(*m_content->weapons.indexOf(weapon), player.autoSwitch, time, random);
    }
    for (const LoadoutAmmo& ammo : loadout->ammo) {
        std::ignore = player.inventory.giveAmmo(ammo.id, ammo.rounds);
    }
}

auto Game::nextSpawnPoint(Team team) -> SpawnPoint {
    const std::span<const SpawnPoint> points = m_map->spawnPoints(team);
    if (points.empty()) {
        const std::span<const SpawnPoint> terrorists = m_map->spawnPoints(Team::Terrorists);
        return terrorists.empty() ? SpawnPoint{} : terrorists.front();
    }
    std::optional<std::size_t>& last = m_lastSpawnPoints[team];
    last = last ? (*last + 1) % points.size() : 0;
    return points[*last];
}

auto Game::maxSpeed(const Player& player) const -> nx::UnitsPerSecond {
    return m_rounds.isFrozen() ? kFreezeSpeed : player.inventory.maxSpeed();
}

void Game::standAt(Player& player, nx::Vec3 origin) {
    player.movement = MovementState{.origin = origin};
    player.movement.maxSpeed = maxSpeed(player);
    m_world.get<nx::game::Transform>(player.body)->origin = player.movement.origin;
    *m_world.get<nx::game::Body>(player.body) = bodyOf(player.movement);
    player.wasPlaced = true;
}

void Game::hearMovement(Player& player, std::span<const MovementSound> sounds,
                        std::span<const BrushEntity> entities, const UserCommand& command) {
    const nx::GameTime now = nx::game::tickTimeOf(m_world).now;
    nx::Random random{command.number.value, kFootstepStream};
    for (const MovementSound& sound : sounds) {
        if (std::holds_alternative<Splash>(sound)) {
            makeSound(player, m_content->footsteps.splash);
            continue;
        }
        if (!m_cvars.doStepsSound()) {
            continue;
        }
        const Footstep& step = std::get<Footstep>(sound);
        const TextureMaterial ground =
            step.ground ? groundMaterial(*step.ground, entities) : TextureMaterial::Concrete;
        if (const std::optional<SoundMade> made = player.stepVoice.soundOf(step, ground, now, random)) {
            player.sounds.push_back(*made);
        }
    }
}

auto Game::groundMaterial(const StepGround& ground, std::span<const BrushEntity> entities) const
    -> TextureMaterial {
    const nx::Vec3 below = ground.origin - nx::Vec3{.z = kStepTextureDepth.value()};
    if (std::holds_alternative<BoxSolid>(ground.ground)) {
        return TextureMaterial::Concrete; // a box has no model, so no texture (PM_TraceTexture)
    }
    const std::optional<nx::collision::ModelIndex> model = modelOf(ground.ground);
    if (!model) {
        return m_map->materialAlong(m_content->materials, nx::assets::kWorldModel, ground.origin, below);
    }
    const auto entity = std::ranges::find(entities, *model, &BrushEntity::model);
    if (entity == entities.end()) {
        return TextureMaterial::Concrete; // what it stood on is gone: the original finds no texture
    }
    // Into the space of the entity's model, as TraceTexture turns the segment.
    const auto inModelSpace = [&entity](nx::Vec3 point) {
        const nx::Vec3 moved = point - entity->origin;
        return entity->angles == nx::EulerAngles{} ? moved
                                                   : nx::intoAxes(nx::angleVectors(entity->angles), moved);
    };
    return m_map->materialAlong(m_content->materials, entity->model, inModelSpace(ground.origin),
                                inModelSpace(below));
}

auto Game::materialOfHit(const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end) const
    -> std::optional<TextureMaterial> {
    if (hit.isBody) {
        return TextureMaterial::Flesh;
    }
    if (!hit.entity) {
        return m_map->materialAlong(m_content->materials, nx::assets::kWorldModel, start, end);
    }
    const nx::game::BrushModel* const model = m_world.get<nx::game::BrushModel>(*hit.entity);
    const nx::game::Transform* const transform = m_world.get<nx::game::Transform>(*hit.entity);
    if (model == nullptr || transform == nullptr) {
        return std::nullopt; // no texture is named on what is not a brush model
    }
    // Into the space of the entity's model, as TraceTexture turns the segment.
    const auto inModelSpace = [transform](nx::Vec3 point) {
        const nx::Vec3 moved = point - transform->origin;
        return transform->angles == nx::EulerAngles{}
                   ? moved
                   : nx::intoAxes(nx::angleVectors(transform->angles), moved);
    };
    return m_map->materialAlong(m_content->materials, model->index, inModelSpace(start), inModelSpace(end));
}

auto Game::createEntity(EntityTypeIndex type, nx::game::Transform placement)
    -> nx::Result<nx::ecs::Entity, EntityError> {
    return spawnEntityBody(m_world, m_map->hulls(), type, placement);
}

auto Game::removeEntity(nx::ecs::Entity entity) -> nx::Result<void, EntityError> {
    if (!m_world.isAlive(entity) || !m_world.has<EntityBody>(entity)) {
        return nx::fail(EntityError::UnknownEntity, "a live entity-body handle is required");
    }
    m_world.destroy(entity);
    return {};
}

auto Game::entities() -> std::vector<EntityState> {
    return entityStatesOf(m_world);
}

auto Game::perceptionTargets() const -> std::vector<PerceptionTarget> {
    std::vector<PerceptionTarget> targets;
    for (const Player& player : m_players) {
        if (player.status.isAlive()) {
            targets.push_back(
                {.entity = player.body, .origin = player.movement.origin, .eye = eyeOf(player.movement)});
        }
    }
    return targets;
}

auto Game::sees(nx::ecs::Entity observer, nx::Vec3 eye, const PerceptionTarget& target) -> bool {
    const nx::game::SolidHit hit = nx::game::traceSolidsHit(
        m_world, m_map->hulls(), {.start = eye, .end = target.eye, .ignored = observer});
    return hit.entity == target.entity ||
           (hit.trace.fraction >= 1.0F && !hit.trace.startsSolid && !hit.trace.isAllSolid);
}

auto Game::navigationGeometry() -> nx::navigation::Geometry {
    return nx::game::navigationGeometryOf(m_map->map(), m_world);
}

auto Game::time() const -> nx::game::TickTime {
    return nx::game::tickTimeOf(m_world);
}

} // namespace cs
