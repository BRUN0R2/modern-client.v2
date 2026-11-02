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

/// How often the rules look at the restart the cvars ask for (Think's periodic part).
constexpr nx::Seconds kPeriodicThinkInterval{1.0F};
/// The stream of random numbers the sounds of a command's steps draw from.
constexpr std::string_view kFootstepStream = "footsteps";
constexpr std::string_view kBreathStream = "breath";
/// The stream of random numbers a joining player's character is drawn from, and its models' animations'.
constexpr std::string_view kAppearanceStream = "appearance";
/// The stream of random numbers an attack on a player draws from, of the attacker's command and the victim.
constexpr std::string_view kAttackStream = "player attack";
/// The stream of random numbers the blood a player's attack sheds draws from, of its command and the
/// victim.
constexpr std::string_view kBloodStream = "player blood";
/// The stream of random numbers the blood a player's attack makes an entity's body shed draws from, of
/// its command and the entity.
constexpr std::string_view kEntityBloodStream = "entity blood";
/// How much less slow a player slowed down by a hit is each command it starts on the ground (PreThink).
constexpr double kSpeedShareRecovery = 0.01;
/// The stream of random numbers the team of a player joining by auto-select is drawn from.
constexpr std::string_view kDefaultTeamStream = "default team";
/// The teams a player may join, which SelectDefaultTeam draws one of.
constexpr std::uint32_t kPlayingTeams = 2;

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

/// The way of `vector`, of length 1, as the original's Vector::Normalize makes it: its parts times the
/// inverse of its length; straight up for none.
[[nodiscard]] auto wayOf(nx::Vec3 vector) noexcept -> nx::Vec3 {
    const float length = nx::length(vector);
    if (length == 0.0F) {
        return nx::Vec3{.z = 1.0F};
    }
    return vector * (1.0F / length);
}

/// The seed of the random numbers `attacker`'s attack on what it met, `victim`, draws from: of its
/// command and the victim.
[[nodiscard]] auto attackSeedOf(const Player& attacker, std::uint64_t victim) noexcept -> std::uint64_t {
    constexpr unsigned kVictimBits = 32;
    return (static_cast<std::uint64_t>(attacker.randomSeed.value) << kVictimBits) ^ victim;
}

} // namespace

Game::Game(CreateToken /*token*/, const GameContent& content, const GameMap& map, GameCvars cvars,
           GameModels models)
    : m_state{content, map, std::move(cvars), std::move(models)} {}

Game::~Game() = default;

auto Game::start(const GameContent& content, const GameMap& map, GameCvars cvars)
    -> nx::Result<std::unique_ptr<Game>, GameError> {
    GameModels models = GameModels::read(content.vfs, map.map(), content.characters);
    if (!models.missing().empty()) {
        nx::log::warning(kGameLog, "models the map's hostages play that cannot be read: {}",
                         models.missing() | std::views::join_with(std::string_view{", "}) |
                             std::ranges::to<std::string>());
    }
    auto game = std::make_unique<Game>(CreateToken{}, content, map, std::move(cvars), std::move(models));
    if (auto spawned = spawnGame(map.map(), content.entityClasses, game->m_state.world); !spawned) {
        return nx::fail(GameError::Unspawnable, std::move(spawned.error()));
    }
    if (auto registered =
            registerHostages(game->m_state.world, game->m_state.models.played(), content.hitGroups);
        !registered) {
        return nx::fail(GameError::Unspawnable, std::move(registered.error()));
    }
    if (auto registered = registerPlayerBodies(game->m_state.world); !registered) {
        return nx::fail(GameError::Unspawnable, std::move(registered.error()));
    }
    spawnHostages(game->m_state.world, map.hulls(), game->m_state.models.hostageSpawns());
    if (auto registered = registerEntityBodies(game->m_state.world, content.entities); !registered) {
        return nx::fail(GameError::Unspawnable, std::move(registered.error()));
    }
    return game;
}

auto Game::join(Team team, AutoSwitch autoSwitch, std::string_view name) -> PlayerIndex {
    const std::vector<std::string_view> taken = playersIn(m_state.players) |
                                                std::views::transform(&Player::name) |
                                                std::ranges::to<std::vector<std::string_view>>();
    std::string given = playerNameOf(name, taken);
    // The first free slot, as the engine gives a client the first free one.
    const auto free =
        std::ranges::find_if(m_state.players, [](const std::optional<Player>& slot) { return !slot; });
    const PlayerIndex index{nx::narrow<std::uint32_t>(std::distance(m_state.players.begin(), free))};
    if (free == m_state.players.end()) {
        m_state.players.emplace_back();
    }
    // HandleMenu_ChooseAppearance's auto-select: one of the team's characters, each as likely.
    const std::vector<CharacterIndex> characters = m_state.content.characters.ofTeam(team);
    nx::Random appearance{index.value, kAppearanceStream};
    Player& player = m_state.players[index.value].emplace(
        Player{.team = team,
               .name = std::move(given),
               .autoSwitch = autoSwitch,
               .character = characters.at(appearance.below(characters.size())),
               .status = PlayerStatus{.health = nx::HitPoints{}, .money = m_state.cvars.startingMoney()},
               .inventory = Inventory{m_state.content.weapons},
               .stepVoice = StepVoice{m_state.content.footsteps},
               .breath = Breath{m_state.content.breath, m_state.content.playerHarm}});
    player.body =
        m_state.world.spawn(nx::game::Transform{}, bodyOf(player.movement), PlayerBody{.player = index},
                            nx::game::Classified{.classification = nx::game::Classification::Player});
    nx::log::info(kGameLog, "{} joins the {}", player.name, nx::enumName(team));
    if (cs::canSpawn(m_state, player)) {
        cs::spawn(m_state, player);
    } else {
        cs::observe(m_state, player);
    }
    checkWinConditions();
    return index;
}

void Game::leave(PlayerIndex index) {
    const Player& player = cs::playerAt(m_state, index);
    nx::log::info(kGameLog, "{} of the {} leaves", player.name, nx::enumName(player.team));
    // ClientDisconnected: its weapons destroyed with it, its body gone, no longer counted.
    m_state.world.destroy(player.body);
    m_state.players[index.value].reset();
    checkWinConditions();
}

auto Game::defaultTeam() const -> Team {
    const TeamCounts counts = teamCounts();
    const std::uint32_t terrorists = counts[Team::Terrorists].players();
    const std::uint32_t counterTerrorists = counts[Team::CounterTerrorists].players();
    if (terrorists != counterTerrorists) {
        return terrorists < counterTerrorists ? Team::Terrorists : Team::CounterTerrorists;
    }
    if (m_state.wins[Team::Terrorists] != m_state.wins[Team::CounterTerrorists]) {
        return m_state.wins[Team::Terrorists] < m_state.wins[Team::CounterTerrorists]
                   ? Team::Terrorists
                   : Team::CounterTerrorists;
    }
    // Teams equal: one at random, RANDOM_LONG(0, 1) == 0 picking the counter-terrorists.
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_state.world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kDefaultTeamStream};
    return random.below(kPlayingTeams) == 0 ? Team::CounterTerrorists : Team::Terrorists;
}

auto Game::teamCounts() const -> TeamCounts {
    TeamCounts counts{};
    for (const Player& player : playersIn(m_state.players)) {
        TeamCount& count = counts[player.team];
        if (player.status.isAlive()) {
            ++count.alive;
        } else {
            ++count.dead;
        }
    }
    return counts;
}

auto Game::playerCount() const -> std::size_t {
    return static_cast<std::size_t>(std::ranges::distance(playersIn(m_state.players)));
}

auto Game::nameOf(PlayerIndex player) const -> std::string_view {
    return cs::playerAt(m_state, player).name;
}

auto Game::playerNamed(std::string_view name) const -> std::optional<PlayerIndex> {
    for (const auto& [slot, player] : std::views::enumerate(m_state.players)) {
        if (player && player->name == name) {
            return PlayerIndex{nx::narrow<std::uint32_t>(slot)};
        }
    }
    return std::nullopt;
}

auto Game::bodyStatusOf(nx::ecs::Entity body) const -> BodyStatus {
    for (const std::optional<Player>& player : m_state.players) {
        if (player && player->body == body) {
            return BodyStatus{.isAlive = player->status.isAlive(), .team = player->team};
        }
    }
    const EntityBody* const entity =
        m_state.world.isAlive(body) ? m_state.world.get<EntityBody>(body) : nullptr;
    return BodyStatus{.isAlive = entity != nullptr && entity->motion != EntityMotion::Dead};
}

auto Game::ownStateOf(PlayerIndex index) const -> PlayerOwnState {
    const Player& player = cs::playerAt(m_state, index);
    const OwnedWeapon* const held = player.inventory.held();
    return PlayerOwnState{.team = player.team,
                          .isAlive = player.status.isAlive(),
                          .spawnedAt = player.spawnedAt,
                          .eye = eyeOf(player.movement),
                          .feet = feetOf(player.movement),
                          .velocity = player.movement.velocity,
                          .view = player.view,
                          .maxSpeed = player.movement.maxSpeed,
                          .heldSlot = held != nullptr
                                          ? std::optional{m_state.content.weapons.weapon(held->weapon).slot}
                                          : std::nullopt,
                          .hurtFrom = player.hurtFrom};
}

void Game::checkWinConditions() {
    const TeamCounts counts = teamCounts();
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_state.world);
    if (const std::optional<RoundEndReason> reason =
            m_state.rounds.checkWinConditions(counts, time.now, time.rate)) {
        roundEnded(*reason);
    }
}

auto Game::boxesMetBy(const Player& mover) -> std::vector<SolidBox> {
    std::vector<SolidBox> near;
    boxesNear(solidBoxesOf(m_state.world, mover.body), mover.movement.origin, mover.body, near);
    return near;
}

void Game::give(PlayerIndex index, WeaponIndex weapon) {
    giveWeapon(m_state, cs::playerAt(m_state, index), weapon);
}

void Game::setAutoSwitch(PlayerIndex index, AutoSwitch autoSwitch) {
    cs::playerAt(m_state, index).autoSwitch = autoSwitch;
}

void Game::giveArmor(PlayerIndex index, ArmorKind kind) {
    wearArmor(cs::playerAt(m_state, index).status, kind);
}

auto Game::buyWeapon(PlayerIndex index, WeaponIndex weapon) -> nx::Result<void, BuyRefusal> {
    return cs::buyWeapon(m_state, cs::playerAt(m_state, index), weapon);
}

auto Game::buyArmor(PlayerIndex index, ArmorKind kind) -> nx::Result<void, BuyRefusal> {
    return cs::buyArmor(m_state, cs::playerAt(m_state, index), kind);
}

auto Game::buyAmmo(PlayerIndex index, WeaponSlot slot, AmmoPurchase purchase)
    -> nx::Result<void, BuyRefusal> {
    return cs::buyAmmo(m_state, cs::playerAt(m_state, index), slot, purchase);
}

auto Game::snapshotFor(PlayerIndex player) -> PlayerSnapshot {
    return cs::snapshotFor(m_state, player);
}

auto Game::toggleNoclip(PlayerIndex index) -> bool {
    MovementState& movement = cs::playerAt(m_state, index).movement;
    const bool isFlying = movement.moveType == MoveType::Walk;
    movement.moveType = isFlying ? MoveType::Noclip : MoveType::Walk;
    return isFlying;
}

void Game::placeEye(PlayerIndex index, nx::Vec3 eye) {
    Player& player = cs::playerAt(m_state, index);
    cs::standAt(m_state, player, eye - nx::Vec3{.z = player.movement.viewHeight.value()});
}

void Game::tick(std::span<const PlayerCommand> commands, std::span<const EntityCommand> entities) {
    m_state.events.impacts.clear();
    m_state.events.bloodMarks.clear();
    m_state.events.bloodSprays.clear();
    m_state.events.bloodStreams.clear();
    m_state.events.streakSplashes.clear();
    m_state.events.killNotices.clear();
    for (Player& player : playersIn(m_state.players)) {
        player.turnedView.reset();
        player.wasPlaced = false;
        player.sounds = std::exchange(player.soundsOfPurchases, {});
        player.hurtFrom.reset();
        player.damageDealt.clear();
    }
    for (const PlayerCommand& command : commands) {
        runCommand(cs::playerAt(m_state, command.player), command.command);
    }
    EntityBodiesTick bodies = runEntityBodies(m_state.world, m_state.map.hulls(), entities);
    m_state.events.entitySteps = std::move(bodies.steps);
    for (const EntityAttack& strike : bodies.attacks) {
        if (const PlayerBody* const body = m_state.world.get<PlayerBody>(strike.target)) {
            // A strike met the player where its trace did, as a TraceAttack sets m_LastHitGroup.
            Player& struck = cs::playerAt(m_state, body->player);
            struck.lastHitGroup = hitGroupOf(strike.attack.trace);
            feelHurt(struck, strike.attack, std::nullopt);
            hurt(struck, {.damage = strike.attack.damage, .kind = HarmKind::Melee});
        } else {
            std::ignore = nx::game::attackEntity(m_state.world, strike.target, strike.attack);
        }
    }
    thinkRules();
    const nx::collision::BrushHulls& hulls = m_state.map.hulls();
    nx::game::runThinks(m_state.world, hulls);
    nx::game::runPushers(m_state.world, hulls);
    for (Player& player : playersIn(m_state.players)) {
        player.movement.origin = m_state.world.get<nx::game::Transform>(player.body)->origin;
        // Its weapons' sounds of the tick first, as they come before what the player makes of them.
        std::vector<SoundMade> sounds = player.inventory.takeSounds();
        std::ranges::move(player.sounds, std::back_inserter(sounds));
        player.sounds = std::move(sounds);
    }
    nx::game::advanceTick(m_state.world);
}

void Game::runCommand(Player& player, const UserCommand& command) {
    const nx::collision::BrushHulls& hulls = m_state.map.hulls();
    // SV_RunCmd: where the player looks, and the seed of what it does.
    player.view = command.viewAngles;
    player.randomSeed = command.number;
    // The weapon the command selects is drawn before it moves the player, at the speed it allows, as
    // the original's CmdStart selects it.
    player.inventory.select(command, nx::game::tickTimeOf(m_state.world));
    player.movement.maxSpeed = cs::maxSpeed(m_state, player);
    preThink(player, command);
    // After PreThink, which may kill it, as the original's returns then, and its PostThink skips the dead.
    const bool isAlive = player.status.isAlive();
    const MovementState before = player.movement;
    const std::vector<BrushEntity> entities = brushEntitiesOf(m_state.world);
    const MovementResult moved =
        simulateMovement(player.movement, command, MovementSettings{}, hulls, entities, boxesMetBy(player));
    player.movement = moved.state;
    // A dead player's body is not solid (SOLID_NOT): it touches nothing.
    if (isAlive) {
        hearMovement(player, moved.sounds, entities, command);
        nx::game::touchBrushEntities(m_state.world, hulls, moved.touchedModels, player.body);
    }
    cs::placeBody(m_state, player);
    if (isAlive) {
        nx::game::touchTriggers(m_state.world, hulls, player.body);
        touchHurtVolumes(player);
        // SV_RunCmd's PostThink is of a player still alive after what it touched.
        if (player.status.isAlive()) {
            postThink(player, command, before);
        }
    }
    // As the original's PostThink ends, dead or alive (m_afButtonLast, StudioProcessGait).
    player.lastButtons = command.buttons;
    cs::animateAfterCommand(m_state, player, frameTimeOf(command));
    cs::poseBody(m_state, player);
}

void Game::preThink(Player& player, const UserCommand& command) {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_state.world);
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
    if (player.movement.isOnGround) {
        // Slowed down by a hit, a hundredth less slow each command, in double as the original adds it.
        if (player.speedShare < 1.0F) {
            player.speedShare =
                static_cast<float>(static_cast<double>(player.speedShare) + kSpeedShareRecovery);
            player.movement.velocity = player.movement.velocity * player.speedShare;
        }
        player.speedShare = std::min(player.speedShare, 1.0F);
    }
    if (player.status.isAlive()) {
        // Jump: the button going down on the ground, out of deep water and of a jump out of water.
        const MovementState& movement = player.movement;
        if (command.buttons.test(Button::Jump) && !player.lastButtons.test(Button::Jump) &&
            movement.isOnGround && movement.waterLevel < WaterLevel::Waist &&
            movement.waterJumpTime <= nx::Milliseconds{}) {
            cs::animate(m_state, player, PlayerAction::Jump);
        }
        // Duck.
        if (command.buttons.test(Button::Duck)) {
            cs::animate(m_state, player, PlayerAction::Walk);
        }
    } else if (!player.animation.torso.isFinished) {
        // PlayerDeathThink: its death plays on.
        advanceFrame(player.animation.torso, time.now, time.rate);
    }
    // Dead or not, as the original's PreThink looks at the kept harms before it weighs a death; the dead
    // are given back none.
    healPlayer(player.status,
               player.timedHarms.check(m_state.content.playerHarm.drowning.givenBack, time.now, time.rate));
}

void Game::postThink(Player& player, const UserCommand& command, const MovementState& before) {
    const nx::collision::BrushHulls& hulls = m_state.map.hulls();
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
        .canAttack = !m_state.rounds.isFrozen()};
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_state.world);
    // What the attacks meet: what is solid but the player itself, the world, the brush entities, the
    // hostages and the other players; a body is an entity the original's Classify makes other than none
    // (a player, a hostage, a vehicle).
    const WeaponTracer tracer = [this, &hulls, &player](nx::collision::Hull hull, nx::Vec3 start,
                                                        nx::Vec3 end) {
        nx::game::ServerTrace way = nx::game::hullTrace(hull, start, end);
        way.ignored = player.body;
        const nx::game::SolidHit solid = nx::game::traceSolidsHit(m_state.world, hulls, way);
        const nx::game::Classified* const classified =
            solid.entity ? m_state.world.get<nx::game::Classified>(*solid.entity) : nullptr;
        const PlayerBody* const body = solid.entity ? m_state.world.get<PlayerBody>(*solid.entity) : nullptr;
        return WeaponHit{.trace = solid.trace,
                         .entity = solid.entity,
                         .isBrushModel = isBrushEntityOrWorld(m_state.world, solid.entity),
                         .isBody = classified != nullptr &&
                                   classified->classification != nx::game::Classification::None,
                         .playerAngles = body != nullptr ? std::optional{body->pose.angles} : std::nullopt};
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
        cs::animate(m_state, player, PlayerAction::Attack1);
    }
    if (attacks.motions.test(HolderMotion::Reloaded)) {
        cs::animate(m_state, player, PlayerAction::Reload);
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
                m_state.events.impacts.push_back(
                    Impact{.position = hit.trace.end, .on = stampTargetOf(m_state.world, hit.entity)});
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
            landingDamageOf(m_state.content.playerHarm.fall, before, player.movement)) {
        land(player, *damage);
    }
}

void Game::use(Player& player, const UserCommand& command) {
    const std::optional<nx::ecs::Entity> used = nx::game::playerUse(
        m_state.world, m_state.map.hulls(), player.body,
        {.eye = eyeOf(player.movement), .forward = nx::angleVectors(command.viewAngles).forward});
    cs::makeSound(m_state, player, used ? m_state.content.use.found : m_state.content.use.nothing);
}

void Game::touchHurtVolumes(Player& player) {
    for (const nx::game::VolumeHarm& harm :
         nx::game::touchHurtVolumes(m_state.world, m_state.map.hulls(), player.body)) {
        if (harm.damage < nx::Damage{}) {
            healPlayer(player.status, nx::HitPoints{-harm.damage.value()}); // TakeHealth
        } else {
            hurt(player, PlayerHarm{.damage = harm.damage, .kind = harmKindOf(harm.kind)});
        }
    }
}

void Game::land(Player& player, nx::Damage damage) {
    // Weighed before the damage is cut to its whole points.
    if (damage.value() > player.status.health.value()) {
        cs::makeSound(m_state, player, m_state.content.playerHarm.splat);
    }
    player.lastHitGroup = HitGroup::Generic; // PostThink: a fall meets it in no group.
    hurt(player, PlayerHarm{.damage = damage, .kind = HarmKind::Fall});
    player.movement.punch.pitch = nx::Degrees{};
}

void Game::hurt(Player& player, const PlayerHarm& harm) {
    if (!player.status.isAlive()) {
        return;
    }
    const PlayerHarmRules& rules = m_state.content.playerHarm;
    const PlayerHarmed harmed =
        harmPlayer(player.status, player.timedHarms, harm, player.lastHitGroup, rules.armor);
    cs::makeSound(m_state, player, painOf(rules, player.status, player.lastHitGroup, harmed.isArmored));
    if (harmed.isKilled) {
        die(player, nullptr);
    }
}

void Game::attackPlayer(Player& attacker, Player& victim, const nx::game::Attack& attack) {
    if (!victim.status.isAlive()) {
        return;
    }
    const PlayerHarmRules& rules = m_state.content.playerHarm;
    // TraceAttack: a teammate's attack sheds no blood, nor hurts, while friendly fire is off.
    const bool isTeammate = attacker.team == victim.team;
    const bool isSpared = isTeammate && !m_state.cvars.isFriendlyFireOn();
    victim.lastHitGroup = hitGroupOf(attack.trace);
    nx::Damage damage = scaled(attack.damage, m_state.content.hitGroups.player.of(victim.lastHitGroup));
    // A helmet sparks, whoever's attack met it.
    if (sparksOffHelmet(victim.lastHitGroup, victim.status.armorKind)) {
        const HelmetSparks& sparks = rules.helmetSparks;
        m_state.events.streakSplashes.push_back(StreakSplash{.position = attack.trace.end,
                                                             .direction = attack.trace.plane.normal,
                                                             .color = sparks.color,
                                                             .count = sparks.count,
                                                             .speed = sparks.speed,
                                                             .spread = sparks.spread});
    }
    if (isSpared) {
        return;
    }
    feelHurt(victim, attack, attacker.team);
    const std::uint64_t seed =
        attackSeedOf(attacker, m_state.world.get<PlayerBody>(victim.body)->player.value);
    nx::Random random{seed, kAttackStream};
    punchByHit(victim.movement.punch, victim.lastHitGroup, damage, victim.status.armorKind, random);
    if (shedsBlood(victim.lastHitGroup, victim.status.armorKind)) {
        nx::Random blood{seed, kBloodStream};
        // BloodSplat: a head streams its blood the way the hit went, before it bleeds.
        if (victim.lastHitGroup == HitGroup::Head) {
            const BloodStreamRules& stream = m_state.content.blood.stream;
            m_state.events.bloodStreams.push_back(BloodStream{.position = attack.trace.end,
                                                              .direction = attack.direction,
                                                              .color = stream.color,
                                                              .speed = streamSpeedOf(stream, damage, blood)});
        }
        bleed(attack,
              {.body = victim.body,
               .dealt = damage,
               .hitGroup = victim.lastHitGroup,
               .color = m_state.content.blood.spray.color},
              blood);
    }
    // TakeDamage.
    if (isTeammate) {
        damage = scaled(damage, rules.attacks.teammateShare);
    }
    ArmorShares armor = rules.armor;
    const OwnedWeapon* const held = attacker.inventory.held();
    const WeaponHarm* const harm =
        held != nullptr ? &m_state.content.weapons.weapon(held->weapon).harm : nullptr;
    if (harm != nullptr) {
        armor.ratio = static_cast<float>(static_cast<double>(armor.ratio) * harm->armorRatio);
    }
    const bool isLargeFlinch =
        harm != nullptr && harm->largeFlinch && !victim.movement.isDucked && !isLeg(victim.lastHitGroup);
    if (!isLargeFlinch) {
        victim.speedShare = rules.attacks.flinchSpeedShare;
        cs::animate(m_state, victim, PlayerAction::Flinch);
    } else {
        if (nx::length(victim.movement.velocity) < rules.attacks.knockbackBelowSpeed.value()) {
            const nx::Vec3 away = wayOf(victim.movement.origin - attacker.movement.origin);
            victim.movement.velocity = victim.movement.velocity + (away * rules.attacks.knockback.value());
            victim.speedShare = rules.attacks.largeFlinchSpeedShare;
        }
        cs::animate(m_state, victim, PlayerAction::LargeFlinch);
    }
    const PlayerHarmed harmed =
        harmPlayer(victim.status, victim.timedHarms, PlayerHarm{.damage = damage, .kind = HarmKind::Bullet},
                   victim.lastHitGroup, armor);
    if (harmed.damage > nx::Damage{}) {
        attacker.damageDealt.push_back({.headTop = headTopOf(victim.body),
                                        .damage = harmed.damage,
                                        .isHead = victim.lastHitGroup == HitGroup::Head});
    }
    cs::makeSound(m_state, victim, painOf(rules, victim.status, victim.lastHitGroup, harmed.isArmored));
    if (harmed.isKilled) {
        die(victim, &attacker);
    }
}

void Game::bleed(const nx::game::Attack& attack, const Bleeding& bleeding, nx::Random& random) {
    // TraceBleed: a hit that dealt nothing sheds nothing.
    if (bleeding.dealt <= nx::Damage{}) {
        return;
    }
    const BloodRules& rules = m_state.content.blood;
    const nx::Vec3 from = attack.trace.end;
    if (const std::optional<std::uint32_t> size = spraySizeOf(rules.spray, bleeding.dealt)) {
        m_state.events.bloodSprays.push_back(BloodSpray{.position = from,
                                                        .direction = attack.direction,
                                                        .color = bleeding.color,
                                                        .size = *size,
                                                        .hitGroup = bleeding.hitGroup});
    }
    for (const nx::Vec3 way : bleedingWaysOf(rules, bleeding.dealt, attack.direction, random)) {
        const nx::game::SolidHit hit = nx::game::traceSolidsHit(m_state.world, m_state.map.hulls(),
                                                                {.start = from,
                                                                 .end = from + (way * rules.reach.value()),
                                                                 .type = nx::game::TraceType::NoMonsters,
                                                                 .ignored = bleeding.body});
        if (hit.trace.fraction >= 1.0F) {
            continue;
        }
        const std::optional<std::uint32_t> decal = bloodDecalOf(rules, random);
        // A decal lies on the world or on a brush entity alone (UTIL_DecalTrace).
        const bool isBrush = !hit.entity || m_state.world.has<nx::game::BrushModel>(*hit.entity);
        if (decal && isBrush) {
            m_state.events.bloodMarks.push_back(BloodMark{
                .position = hit.trace.end, .on = stampTargetOf(m_state.world, hit.entity), .decal = *decal});
        }
    }
}

void Game::die(Player& player, Player* killer) {
    nx::log::info(kGameLog, "{} dies", player.name);
    cs::makeSound(m_state, player, m_state.content.playerHarm.death);
    const bool isKilledByAnother = killer != nullptr && killer != &player;
    // DeathNotice, as PlayerKilled starts, while its killer holds the weapon that killed it.
    const OwnedWeapon* const held = isKilledByAnother ? killer->inventory.held() : nullptr;
    m_state.events.killNotices.push_back(KillNotice{
        .killer = isKilledByAnother ? std::optional{NoticedPlayer{.name = killer->name, .team = killer->team}}
                                    : std::nullopt,
        .victim = {.name = player.name, .team = player.team},
        .weapon = held != nullptr ? std::optional{held->weapon} : std::nullopt,
        .isHead = player.lastHitGroup == HitGroup::Head});
    ++player.score.deaths;
    if (killer == &player) {
        --player.score.frags;
    } else if (isKilledByAnother) {
        killer->score.frags += killer->team == player.team ? -1 : 1; // IPointsForKill
    }
    // PlayerKilled pays its killer, or makes it pay for a teammate.
    if (isKilledByAnother) {
        const KillMoney& kills = m_state.content.economy.kills;
        killer->status.money = addMoney(
            killer->status.money, killer->team == player.team ? nx::Money{} - kills.teammate : kills.enemy);
    }
    player.movement.isDead = true;
    cs::animate(m_state, player, PlayerAction::Die);
    cs::placeBody(m_state, player);
    // Its weapons go with it (PackDeadPlayerItems; the box it drops comes with dropping, TODO.md).
    player.inventory.removeAll();
    checkWinConditions();
}

void Game::feelHurt(Player& victim, const nx::game::Attack& attack, std::optional<Team> team) {
    const nx::Vec3 back{.x = -attack.direction.x, .y = -attack.direction.y};
    const float across = nx::length(back);
    if (across > 0.0F && victim.status.isAlive()) {
        victim.hurtFrom = HurtFrom{.toward = back / across, .team = team};
    }
}

void Game::thinkRules() {
    const nx::game::TickTime& time = nx::game::tickTimeOf(m_state.world);
    const RoundEvents events = m_state.rounds.think(time.now, time.rate, m_state.cvars.roundLengths());
    if (events.test(RoundEvent::FreezeEnded)) {
        nx::log::info(kGameLog, "the round's freeze is over");
        for (Player& player : playersIn(m_state.players)) {
            player.roundTimer = m_state.rounds.timerAt(time.now, time.rate);
            player.movement.maxSpeed = cs::maxSpeed(m_state, player);
        }
    }
    if (const std::optional<RoundEndReason> reason = m_state.rounds.end();
        events.test(RoundEvent::Ended) && reason) {
        roundEnded(*reason);
    }
    std::uint32_t hostagesHeld = 0;
    if (events.test(RoundEvent::RoundStarted) || events.test(RoundEvent::GameRestarted)) {
        // RestartRound puts the hostages back as it counts what they bring, before it pays.
        hostagesHeld =
            repositionHostages(m_state.world, m_state.map.hulls(), m_state.content.economy.hostageBonus);
    }
    if (events.test(RoundEvent::RoundStarted)) {
        nx::log::info(kGameLog, "a round starts");
        payForRound(hostagesHeld);
    }
    if (events.test(RoundEvent::GameRestarted)) {
        nx::log::info(kGameLog, "the game restarts");
        m_state.accounts.restartGame();
        m_state.wins = {}; // m_bCompleteReset
        for (Player& player : playersIn(m_state.players)) {
            player.score = {}; // CBasePlayer::Reset
            player.status = PlayerStatus{.money = m_state.cvars.startingMoney()};
            player.inventory.removeAll();
            cs::equip(m_state, player);
        }
    }
    if (events.test(RoundEvent::RoundStarted) || events.test(RoundEvent::GameRestarted)) {
        // RestartRound counts the spawns of the round anew before the players spawn again.
        for (Player& player : playersIn(m_state.players)) {
            player.spawnsThisRound = 0;
        }
        for (Player& player : playersIn(m_state.players)) {
            cs::spawn(m_state, player);
        }
        // After the players spawn, as the original's RestartRound cleans the map up (CleanUpMap).
        nx::game::restartBrushEntities(m_state.world, m_state.map.hulls());
    }
    // Last, as the original's Think reads them once a second after the round's checks.
    if (time.now > m_state.nextPeriodicThink) {
        if (const std::optional<nx::Seconds> delay = m_state.cvars.takeRestart()) {
            nx::log::info(kGameLog, "the game restarts in {}", *delay);
            m_state.rounds.restartGameIn(*delay, time.now, time.rate);
        }
        m_state.nextPeriodicThink = time.now.after(kPeriodicThinkInterval, time.rate);
    }
}

void Game::roundEnded(RoundEndReason reason) {
    nx::log::info(kGameLog, "the round is over: {}", nx::enumName(reason));
    m_state.accounts.roundEnded(reason, m_state.map.objectives());
    const std::optional<Team> winner = winnerOf(reason);
    if (winner) {
        ++m_state.wins[*winner];
    }
    const bool paysLivingLosers = m_state.content.economy.roundEnd(reason).paysLivingLosers;
    for (Player& player : playersIn(m_state.players)) {
        if (winner && *winner != player.team && player.status.isAlive() && !paysLivingLosers) {
            player.isPaidNextRound = false;
        }
    }
}

void Game::attackEntity(Player& player, nx::ecs::Entity entity, const nx::game::Attack& attack) {
    if (const PlayerBody* const body = m_state.world.get<PlayerBody>(entity)) {
        attackPlayer(player, cs::playerAt(m_state, body->player), attack);
        return;
    }
    // Over its head as the attack met it, before a death poses it anew and takes its solidity.
    const bool showsDamage = m_state.world.has<Hostage>(entity) || m_state.world.has<EntityBody>(entity);
    const std::optional<nx::Vec3> headTop = showsDamage ? std::optional{headTopOf(entity)} : std::nullopt;
    const nx::game::Harm harm = nx::game::attackEntity(m_state.world, entity, attack);
    if (harm.dealt > nx::Damage{} && headTop) {
        player.damageDealt.push_back({.headTop = *headTop,
                                      .damage = harm.dealt,
                                      .isHead = hitGroupOf(attack.trace) == HitGroup::Head});
    }
    if (m_state.world.has<Hostage>(entity)) {
        // The hostage's TakeDamage takes it from its attacker's account.
        player.status.money = addMoney(
            player.status.money, nx::Money{} - hostageHarmCost(harm, m_state.content.economy.hostageHarm));
    }
    // CBaseMonster::TraceAttack: a body of a kind that bleeds sprays its blood and traces it behind it.
    const EntityBody* const body = m_state.world.get<EntityBody>(entity);
    if (body == nullptr) {
        return;
    }
    if (const std::optional<std::uint8_t> blood =
            m_state.content.entities.type(body->type).definition.bloodColor) {
        nx::Random random{attackSeedOf(player, std::to_underlying(entity.index())), kEntityBloodStream};
        bleed(attack,
              {.body = entity, .dealt = harm.dealt, .hitGroup = hitGroupOf(attack.trace), .color = *blood},
              random);
    }
}

auto Game::headTopOf(nx::ecs::Entity entity) -> nx::Vec3 {
    std::optional<nx::Vec3> head;
    if (m_state.world.has<PlayerBody>(entity)) {
        head = playerHeadTopOf(m_state.world, entity);
    } else if (m_state.world.has<Hostage>(entity)) {
        head = hostageHeadTopOf(m_state.world, entity);
    } else if (m_state.world.has<EntityBody>(entity)) {
        head = entityHeadTopOf(m_state.world, entity);
    }
    if (head) {
        return *head;
    }
    const nx::game::BoxSolidity* const solidity = m_state.world.get<nx::game::BoxSolidity>(entity);
    nx::expects(solidity != nullptr, "Game::headTopOf: a body of no hitbox is solid as its box");
    const nx::Vec3 origin = m_state.world.get<nx::game::Transform>(entity)->origin;
    const nx::Vec3 middle = solidity->box.center();
    return {.x = origin.x + middle.x, .y = origin.y + middle.y, .z = origin.z + solidity->box.max.z};
}

void Game::payForRound(std::uint32_t hostagesHeld) {
    const nx::EnumArray<Team, nx::Money> paid =
        m_state.accounts.roundStarts(m_state.map.objectives(), hostagesHeld);
    for (Player& player : playersIn(m_state.players)) {
        if (player.isPaidNextRound) {
            player.status.money = addMoney(player.status.money, paid[player.team]);
        }
    }
}

void Game::hearMovement(Player& player, std::span<const MovementSound> sounds,
                        std::span<const BrushEntity> entities, const UserCommand& command) {
    const nx::GameTime now = nx::game::tickTimeOf(m_state.world).now;
    nx::Random random{command.number.value, kFootstepStream};
    for (const MovementSound& sound : sounds) {
        if (std::holds_alternative<Splash>(sound)) {
            cs::makeSound(m_state, player, m_state.content.footsteps.splash);
            continue;
        }
        if (!m_state.cvars.doStepsSound()) {
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
        return m_state.map.materialAlong(m_state.content.materials, nx::assets::kWorldModel, ground.origin,
                                         below);
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
    return m_state.map.materialAlong(m_state.content.materials, entity->model, inModelSpace(ground.origin),
                                     inModelSpace(below));
}

auto Game::materialOfHit(const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end) const
    -> std::optional<TextureMaterial> {
    if (hit.isBody) {
        return TextureMaterial::Flesh;
    }
    if (!hit.entity) {
        return m_state.map.materialAlong(m_state.content.materials, nx::assets::kWorldModel, start, end);
    }
    const nx::game::BrushModel* const model = m_state.world.get<nx::game::BrushModel>(*hit.entity);
    const nx::game::Transform* const transform = m_state.world.get<nx::game::Transform>(*hit.entity);
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
    return m_state.map.materialAlong(m_state.content.materials, model->index, inModelSpace(start),
                                     inModelSpace(end));
}

auto Game::createEntity(EntityTypeIndex type, nx::game::Transform placement)
    -> nx::Result<nx::ecs::Entity, EntityError> {
    return spawnEntityBody(m_state.world, m_state.map.hulls(), type, placement);
}

auto Game::removeEntity(nx::ecs::Entity entity) -> nx::Result<void, EntityError> {
    if (!m_state.world.isAlive(entity) || !m_state.world.has<EntityBody>(entity)) {
        return nx::fail(EntityError::UnknownEntity, "a live entity-body handle is required");
    }
    m_state.world.destroy(entity);
    return {};
}

auto Game::entities() -> std::vector<EntityState> {
    return entityStatesOf(m_state.world);
}

auto Game::movingBodies() -> std::vector<MovingBody> {
    std::vector<MovingBody> bodies;
    for (const std::optional<Player>& player : m_state.players) {
        if (player && player->status.isAlive()) {
            const nx::Aabb box = player->movement.isDucked ? kDuckedBox : kStandingBox;
            bodies.push_back({.entity = player->body,
                              .feet = feetOf(player->movement),
                              .velocity = player->movement.velocity,
                              .radius = nx::HammerUnits{box.max.x}});
        }
    }
    nx::ecs::Query<nx::ecs::Entity, const EntityBody&, const nx::game::Transform&,
                   const nx::game::BoxSolidity&>{m_state.world}
        .forEach([&bodies](nx::ecs::Entity entity, const EntityBody& body,
                           const nx::game::Transform& transform, const nx::game::BoxSolidity& solidity) {
            if (body.motion != EntityMotion::Dead) {
                bodies.push_back({.entity = entity,
                                  .feet = transform.origin + nx::Vec3{.z = solidity.box.min.z},
                                  .velocity = body.movement.velocity,
                                  .radius = nx::HammerUnits{solidity.box.max.x}});
            }
        });
    return bodies;
}

auto Game::noises() const -> std::vector<GameNoise> {
    std::vector<GameNoise> made;
    for (const Player& player : playersIn(m_state.players)) {
        if (!player.status.isAlive()) {
            continue;
        }
        for (const SoundMade& sound : player.sounds) {
            if (sound.sound == nullptr || !sound.sound->noise) {
                continue;
            }
            const float own = sound.sound->volume;
            made.push_back({.entity = player.body,
                            .team = player.team,
                            .feet = feetOf(player.movement),
                            .eye = eyeOf(player.movement),
                            .kind = *sound.sound->noise,
                            .loudness = own > 0.0F ? std::min(sound.volume / own, 1.0F) : 0.0F});
        }
    }
    return made;
}

auto Game::perceptionTargets() const -> std::vector<PerceptionTarget> {
    std::vector<PerceptionTarget> targets;
    for (const auto& [slot, player] : std::views::enumerate(m_state.players)) {
        if (player && player->status.isAlive()) {
            targets.push_back({.entity = player->body,
                               .player = PlayerIndex{nx::narrow<std::uint32_t>(slot)},
                               .team = player->team,
                               .origin = player->movement.origin,
                               .eye = eyeOf(player->movement),
                               .feet = feetOf(player->movement),
                               .velocity = player->movement.velocity,
                               .isDucked = player->movement.isDucked,
                               .view = player->view});
        }
    }
    return targets;
}

auto Game::sightSolids() -> nx::game::Solids {
    return nx::game::solidsOf(m_state.world);
}

auto Game::seesSpot(const nx::game::Solids& solids, nx::Vec3 eye, nx::Vec3 spot) -> bool {
    const nx::game::SolidHit hit =
        nx::game::traceSolidsHit(m_state.world, m_state.map.hulls(), solids,
                                 {.start = eye, .end = spot, .type = nx::game::TraceType::NoMonsters});
    return hit.trace.fraction >= 1.0F;
}

auto Game::navigationGeometry() -> nx::navigation::Geometry {
    return nx::game::navigationGeometryOf(m_state.map.map(), m_state.world);
}

auto Game::time() const -> nx::game::TickTime {
    return nx::game::tickTimeOf(m_state.world);
}

} // namespace cs
