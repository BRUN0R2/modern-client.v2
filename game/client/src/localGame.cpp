// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.render;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// A user command each this long, the classic rate of 100 a second (docs/07, section 2.2), a tick of
/// the game's clock with each; a frame catches up on this much time at most, so that a stalled one
/// does not run away.
constexpr nx::Milliseconds kCommandInterval = nx::tickLengthOf(nx::kDefaultTickRate);
constexpr nx::Seconds kMaxCatchUp{0.25F};
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

[[nodiscard]] auto eyeOf(const MovementState& player) -> nx::Vec3 {
    return player.origin + nx::Vec3{.z = player.viewHeight.value()};
}

/// Whether what a trace met in `world`, `entity`, is the world (none) or a brush entity (IsBSPModel).
[[nodiscard]] auto isBrushEntityOrWorld(const nx::ecs::World& world, std::optional<nx::ecs::Entity> entity)
    -> bool {
    return !entity || world.get<nx::game::BrushModel>(*entity) != nullptr;
}

} // namespace

LocalGame::LocalGame(CreateToken /*token*/, const GameContent& content, const Level& level, Team team,
                     AutoSwitch autoSwitch, GameCvars cvars)
    : m_content{&content}, m_level{&level}, m_team{team}, m_autoSwitch{autoSwitch}, m_cvars{std::move(cvars)},
      // The first round starts with the map, the first moment of the game's clock.
      m_rounds{Rounds::start(level.objectives(), m_cvars.roundLengths(), nx::GameTime{})},
      m_accounts{content.economy}, m_stepVoice{content.footsteps}, m_breath{content.breath},
      m_status{.money = m_cvars.startingMoney()}, m_inventory{content.weapons} {}

auto LocalGame::start(const GameContent& content, const Level& level, Team team, AutoSwitch autoSwitch,
                      GameCvars cvars) -> nx::Result<std::unique_ptr<LocalGame>, LevelError> {
    auto game =
        std::make_unique<LocalGame>(CreateToken{}, content, level, team, autoSwitch, std::move(cvars));
    if (auto spawned = spawnGame(level.map(), level.entityClasses(), game->m_world); !spawned) {
        return std::unexpected{std::move(spawned.error())};
    }
    if (auto registered = registerHostages(game->m_world, level.models().playedModels(), content.hitGroups);
        !registered) {
        return nx::fail(LevelError::GameUnspawnable, std::move(registered.error()));
    }
    spawnHostages(game->m_world, level.hulls(), level.models().hostageSpawns());
    game->m_body = game->m_world.spawn(nx::game::Transform{}, bodyOf(game->m_player));
    game->equip();
    game->spawn();
    return game;
}

void LocalGame::give(WeaponIndex weapon) {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kSpawnStream};
    std::ignore = m_inventory.give(weapon, m_autoSwitch, time, random);
    m_player.maxSpeed = maxSpeed();
}

auto LocalGame::eye() const noexcept -> nx::Vec3 {
    return eyeOf(m_player);
}

void LocalGame::placeEye(nx::Vec3 eye) {
    standAt(eye - nx::Vec3{.z = m_player.viewHeight.value()});
}

auto LocalGame::roundTimeShown() const -> std::uint32_t {
    return m_roundTimer.shownAt(shownTime(), nx::game::tickTimeOf(m_world).rate);
}

auto LocalGame::play(PlayerInput& input, nx::EulerAngles view, nx::Seconds elapsed) -> nx::Vec3 {
    m_pending = std::min(m_pending + elapsed, kMaxCatchUp);
    return run([&input, view, this] { return input.nextCommand(view, kCommandInterval, m_player.maxSpeed); });
}

auto LocalGame::wait(nx::EulerAngles view, nx::Seconds elapsed) -> nx::Vec3 {
    m_pending += elapsed;
    return run([view] { return UserCommand{.duration = kCommandInterval, .viewAngles = view}; });
}

auto LocalGame::viewModel() const -> std::optional<ViewModelPose> {
    const std::optional<ViewAnimation>& animation = m_inventory.animation();
    if (!animation) {
        return std::nullopt;
    }
    return ViewModelPose{
        .weapon = animation->weapon,
        .sequence = animation->sequence,
        .start = animation->start,
        .intoIt =
            std::max(shownTime().since(animation->start, nx::game::tickTimeOf(m_world).rate), nx::Seconds{})};
}

auto LocalGame::hearing() const -> Hearing {
    return Hearing{.from = shownTime(), .rate = nx::game::tickTimeOf(m_world).rate};
}

auto LocalGame::hostages() -> std::vector<ShownHostage> {
    return hostagesShownAt(m_world, shownTime());
}

auto LocalGame::shownTime() const -> nx::GameTime {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    return time.now.after(m_pending - kCommandInterval, time.rate);
}

auto LocalGame::run(const std::function<UserCommand()>& nextCommand) -> nx::Vec3 {
    while (m_pending >= kCommandInterval) {
        UserCommand command = nextCommand();
        m_lastCommand = CommandNumber{m_lastCommand.value + 1};
        command.number = m_lastCommand;
        tick(command);
        m_pending -= kCommandInterval;
    }
    return nx::lerp(m_previousEye, eyeOf(m_player), m_pending / kCommandInterval);
}

void LocalGame::tick(const UserCommand& command) {
    const nx::collision::BrushHulls& hulls = m_level->hulls();
    m_previousEye = eyeOf(m_player);
    // The weapon the command selects is drawn before it moves the player, at the speed it allows, as
    // the original's CmdStart selects it.
    m_inventory.select(command, nx::game::tickTimeOf(m_world));
    m_player.maxSpeed = maxSpeed();
    const bool isAlive = m_status.isAlive();
    if (isAlive) {
        preThink(command);
    }
    m_lastButtons = command.buttons;
    const MovementState before = m_player;
    const std::vector<BrushEntity> entities = brushEntitiesOf(m_world);
    const MovementResult moved =
        simulateMovement(m_player, command, MovementSettings{}, hulls, entities, solidBoxesOf(m_world));
    m_player = moved.state;
    // A dead player's body is not solid (SOLID_NOT): it touches nothing.
    if (isAlive) {
        hearMovement(moved.sounds, entities, command);
        nx::game::touchBrushEntities(m_world, hulls, moved.touchedModels, m_body);
    }
    m_world.get<nx::game::Transform>(m_body)->origin = m_player.origin;
    *m_world.get<nx::game::Body>(m_body) = bodyOf(m_player);
    if (isAlive) {
        nx::game::touchTriggers(m_world, hulls, m_body);
        postThink(command, before);
    }
    thinkRules();
    nx::game::runThinks(m_world, hulls);
    nx::game::runPushers(m_world, hulls);
    m_player.origin = m_world.get<nx::game::Transform>(m_body)->origin;
    nx::game::advanceTick(m_world);
}

void LocalGame::preThink(const UserCommand& command) {
    if (command.buttons.test(Button::Use) && !m_lastButtons.test(Button::Use)) {
        std::ignore = nx::game::playerUse(
            m_world, m_level->hulls(), m_body,
            {.eye = m_previousEye, .forward = nx::angleVectors(command.viewAngles).forward});
    }
    nx::Random breathRandom{command.number.value, kBreathStream};
    if (const std::optional<SoundMade> breath = m_breath.breathe(
            m_player.waterLevel, m_player.moveType, nx::game::tickTimeOf(m_world), breathRandom)) {
        m_playerSounds.push_back(*breath);
    }
}

void LocalGame::postThink(const UserCommand& command, const MovementState& before) {
    const nx::collision::BrushHulls& hulls = m_level->hulls();
    // Each shot goes as far as a bullet does, and where it meets the world or a brush entity it leaves
    // its mark.
    Shooter shooter{
        .eye = eyeOf(m_player),
        .view = command.viewAngles,
        .punch = m_player.punch,
        .horizontalSpeed = nx::UnitsPerSecond{static_cast<float>(std::sqrt(double{
            (m_player.velocity.x * m_player.velocity.x) + (m_player.velocity.y * m_player.velocity.y)}))},
        .isOnGround = m_player.isOnGround,
        .isDucked = m_player.isDucked,
        .canAttack = !m_rounds.isFrozen()};
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    // What the attacks meet: what is solid, the world, the brush entities and the hostages; a body is
    // an entity the original's Classify makes other than none (a hostage, a vehicle).
    const WeaponTracer tracer = [this, &hulls](nx::collision::Hull hull, nx::Vec3 start, nx::Vec3 end) {
        const nx::game::SolidHit solid =
            nx::game::traceSolidsHit(m_world, hulls, nx::game::hullTrace(hull, start, end));
        const nx::game::Classified* const classified =
            solid.entity ? m_world.get<nx::game::Classified>(*solid.entity) : nullptr;
        return WeaponHit{.trace = solid.trace,
                         .entity = solid.entity,
                         .isBrushModel = isBrushEntityOrWorld(m_world, solid.entity),
                         .isBody = classified != nullptr &&
                                   classified->classification != nx::game::Classification::None};
    };
    const WeaponAttacks attacks = m_inventory.frame(command, shooter, tracer, time);
    for (const Blow& blow : attacks.blows) {
        attackEntity(blow.entity, nx::game::Attack{.attacker = m_body,
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
                    Impact{.position = hit.trace.end, .on = decalTargetOf(m_world, hit.entity)});
            }
            if (hit.entity) {
                attackEntity(*hit.entity, nx::game::Attack{.attacker = m_body,
                                                           .damage = hit.damage,
                                                           .direction = hit.direction,
                                                           .trace = hit.trace});
            }
        }
    }
    m_player.punch = shooter.punch;
    if (const std::optional<nx::Damage> damage =
            landingDamageOf(m_content->playerHarm.fall, before, m_player)) {
        land(*damage);
    }
}

void LocalGame::land(nx::Damage damage) {
    const PlayerHarmRules& rules = m_content->playerHarm;
    // Weighed before the damage is cut to its whole points.
    if (damage.value() > m_status.health.value()) {
        makeSound(rules.splat);
    }
    makeSound(rules.pain);
    const nx::game::Harm harm = harmPlayer(m_status, damage);
    m_player.punch.pitch = nx::Degrees{};
    if (harm.isKilled) {
        die();
    }
}

void LocalGame::die() {
    nx::log::info(kLog, "the player dies");
    makeSound(m_content->playerHarm.death);
    m_player.isDead = true;
    *m_world.get<nx::game::Body>(m_body) = bodyOf(m_player);
    // Its weapons go with it (PackDeadPlayerItems; the box it drops comes with dropping, TODO.md).
    m_inventory.removeAll();
    // As the original's CheckWinConditions weighs a death: nobody is alive, the other team having no
    // player (TeamExterminationCheck).
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    if (m_rounds.endRound(RoundEndReason::RoundDraw, time.now, time.rate)) {
        roundEnded(RoundEndReason::RoundDraw);
    }
}

void LocalGame::makeSound(const GameSound& sound) {
    m_playerSounds.push_back(
        SoundMade{.sound = &sound, .at = nx::game::tickTimeOf(m_world).now, .volume = sound.volume});
}

void LocalGame::thinkRules() {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    const RoundEvents events = m_rounds.think(time.now, time.rate, m_cvars.roundLengths());
    if (events.test(RoundEvent::FreezeEnded)) {
        nx::log::info(kLog, "the round's freeze is over");
        m_roundTimer = m_rounds.timerAt(time.now, time.rate);
        m_player.maxSpeed = maxSpeed();
    }
    if (const std::optional<RoundEndReason> reason = m_rounds.end();
        events.test(RoundEvent::Ended) && reason) {
        roundEnded(*reason);
    }
    std::uint32_t hostagesHeld = 0;
    if (events.test(RoundEvent::RoundStarted) || events.test(RoundEvent::GameRestarted)) {
        // RestartRound puts the hostages back as it counts what they bring, before it pays.
        hostagesHeld = repositionHostages(m_world, m_level->hulls(), m_content->economy.hostageBonus);
    }
    if (events.test(RoundEvent::RoundStarted)) {
        nx::log::info(kLog, "a round starts");
        payForRound(hostagesHeld);
    }
    if (events.test(RoundEvent::GameRestarted)) {
        nx::log::info(kLog, "the game restarts");
        m_accounts.restartGame();
        m_status = PlayerStatus{.money = m_cvars.startingMoney()};
        m_inventory.removeAll();
        equip();
    }
    if (events.test(RoundEvent::RoundStarted) || events.test(RoundEvent::GameRestarted)) {
        spawn();
        // After the players spawn, as the original's RestartRound cleans the map up (CleanUpMap).
        nx::game::restartBrushEntities(m_world, m_level->hulls());
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

void LocalGame::roundEnded(RoundEndReason reason) {
    nx::log::info(kLog, "the round is over: {}", nx::enumName(reason));
    m_accounts.roundEnded(reason);
    const std::optional<Team> winner = winnerOf(reason);
    if (winner && *winner != m_team && m_status.isAlive() &&
        !m_content->economy.roundEnd(reason).paysLivingLosers) {
        m_isPaidNextRound = false;
    }
}

void LocalGame::attackEntity(nx::ecs::Entity entity, const nx::game::Attack& attack) {
    const nx::game::Harm harm = nx::game::attackEntity(m_world, entity, attack);
    if (m_world.has<Hostage>(entity)) {
        // The hostage's TakeDamage takes it from the player's account.
        m_status.money =
            addMoney(m_status.money, nx::Money{} - hostageHarmCost(harm, m_content->economy.hostageHarm));
    }
}

void LocalGame::payForRound(std::uint32_t hostagesHeld) {
    const nx::EnumArray<Team, nx::Money> paid = m_accounts.roundStarts(m_level->objectives(), hostagesHeld);
    if (m_isPaidNextRound) {
        m_status.money = addMoney(m_status.money, paid[m_team]);
    }
}

void LocalGame::spawn() {
    if (!m_status.isAlive()) {
        // Killed, it spawns without armor (Spawn), given what a player of its team spawns with
        // (GiveDefaultItems), as its weapons went with it.
        m_status.armor = nx::ArmorPoints{};
        equip();
    }
    const SpawnPoint point = nextSpawnPoint();
    standAt(point.origin + nx::Vec3{.z = kSpawnLift.value()});
    m_status.health = kSpawnHealth;
    m_isPaidNextRound = true;
    m_inventory.spawned();
    m_turnedView = nx::EulerAngles{.yaw = point.yaw};
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    m_breath.fill(time);
    m_roundTimer = m_rounds.timerAt(time.now, time.rate);
}

void LocalGame::equip() {
    const Loadout* const loadout = m_content->loadouts.find(m_team);
    if (loadout == nullptr) {
        return;
    }
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kSpawnStream};
    for (const std::string& weapon : loadout->weapons) {
        // readLoadouts checked that the catalog has each.
        std::ignore = m_inventory.give(*m_content->weapons.indexOf(weapon), m_autoSwitch, time, random);
    }
    for (const LoadoutAmmo& ammo : loadout->ammo) {
        std::ignore = m_inventory.giveAmmo(ammo.id, ammo.rounds);
    }
}

auto LocalGame::nextSpawnPoint() -> SpawnPoint {
    const std::span<const SpawnPoint> points = m_level->spawnPoints(m_team);
    if (points.empty()) {
        const std::span<const SpawnPoint> terrorists = m_level->spawnPoints(Team::Terrorists);
        return terrorists.empty() ? SpawnPoint{} : terrorists.front();
    }
    m_lastSpawnPoint = m_lastSpawnPoint ? (*m_lastSpawnPoint + 1) % points.size() : 0;
    return points[*m_lastSpawnPoint];
}

auto LocalGame::maxSpeed() const -> nx::UnitsPerSecond {
    return m_rounds.isFrozen() ? kFreezeSpeed : m_inventory.maxSpeed();
}

void LocalGame::standAt(nx::Vec3 origin) {
    m_player = MovementState{.origin = origin};
    m_player.maxSpeed = maxSpeed();
    m_world.get<nx::game::Transform>(m_body)->origin = m_player.origin;
    *m_world.get<nx::game::Body>(m_body) = bodyOf(m_player);
    m_previousEye = eyeOf(m_player);
}

auto LocalGame::takeSounds() -> std::vector<SoundMade> {
    std::vector<SoundMade> sounds = m_inventory.takeSounds();
    std::ranges::move(m_playerSounds, std::back_inserter(sounds));
    m_playerSounds.clear();
    return sounds;
}

void LocalGame::hearMovement(std::span<const MovementSound> sounds, std::span<const BrushEntity> entities,
                             const UserCommand& command) {
    const nx::GameTime now = nx::game::tickTimeOf(m_world).now;
    nx::Random random{command.number.value, kFootstepStream};
    for (const MovementSound& sound : sounds) {
        if (std::holds_alternative<Splash>(sound)) {
            makeSound(m_content->footsteps.splash);
            continue;
        }
        if (!m_cvars.doStepsSound()) {
            continue;
        }
        const Footstep& step = std::get<Footstep>(sound);
        const TextureMaterial ground =
            step.ground ? groundMaterial(*step.ground, entities) : TextureMaterial::Concrete;
        if (const std::optional<SoundMade> made = m_stepVoice.soundOf(step, ground, now, random)) {
            m_playerSounds.push_back(*made);
        }
    }
}

auto LocalGame::groundMaterial(const StepGround& ground, std::span<const BrushEntity> entities) const
    -> TextureMaterial {
    const nx::Vec3 below = ground.origin - nx::Vec3{.z = kStepTextureDepth.value()};
    if (std::holds_alternative<BoxSolid>(ground.ground)) {
        return TextureMaterial::Concrete; // a box has no model, so no texture (PM_TraceTexture)
    }
    const std::optional<nx::collision::ModelIndex> model = modelOf(ground.ground);
    if (!model) {
        return m_level->materialAlong(m_content->materials, nx::assets::kWorldModel, ground.origin, below);
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
    return m_level->materialAlong(m_content->materials, entity->model, inModelSpace(ground.origin),
                                  inModelSpace(below));
}

auto LocalGame::materialOfHit(const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end) const
    -> std::optional<TextureMaterial> {
    if (hit.isBody) {
        return TextureMaterial::Flesh;
    }
    if (!hit.entity) {
        return m_level->materialAlong(m_content->materials, nx::assets::kWorldModel, start, end);
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
    return m_level->materialAlong(m_content->materials, model->index, inModelSpace(start), inModelSpace(end));
}

auto LocalGame::toggleNoclip() -> bool {
    const bool isFlying = m_player.moveType == MoveType::Walk;
    m_player.moveType = isFlying ? MoveType::Noclip : MoveType::Walk;
    return isFlying;
}

auto LocalGame::placements() -> std::vector<nx::render::ModelPlacement> {
    std::vector<nx::render::ModelPlacement> placed;
    nx::ecs::Query<const nx::game::BrushModel&, const nx::game::Transform&,
                   nx::ecs::Optional<const nx::game::Waves>,
                   nx::ecs::Optional<const nx::game::AlternateTextures>>{m_world}
        .forEach([&placed](const nx::game::BrushModel& model, const nx::game::Transform& transform,
                           const nx::game::Waves* waves, const nx::game::AlternateTextures* textures) {
            placed.push_back({.model = model.index,
                              .origin = transform.origin,
                              .angles = transform.angles,
                              .waveHeight = waves != nullptr ? waves->height : nx::HammerUnits{},
                              .showsAlternateTextures = textures != nullptr && textures->isShown});
        });
    return placed;
}

} // namespace cs
