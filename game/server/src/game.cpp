// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.assets;
import nx.ecs;
import nx.collision;
import nx.game;
import nx.navigation;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers a joining player's character is drawn from, and its models' animations'.
constexpr std::string_view kAppearanceStream = "appearance";
/// The stream of random numbers the team of a player joining by auto-select is drawn from.
constexpr std::string_view kDefaultTeamStream = "default team";
/// The teams a player may join, which SelectDefaultTeam draws one of.
constexpr std::uint32_t kPlayingTeams = 2;

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
    game->m_state.moves.emplace(game->m_state.world);
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
    if (canSpawn(m_state, player)) {
        spawn(m_state, player);
    } else {
        observe(m_state, player);
    }
    m_state.moves->areBoxesStale = true;
    checkWinConditions(m_state);
    return index;
}

void Game::leave(PlayerIndex index) {
    const Player& player = playerAt(m_state, index);
    nx::log::info(kGameLog, "{} of the {} leaves", player.name, nx::enumName(player.team));
    // ClientDisconnected: its weapons destroyed with it, its body gone, no longer counted.
    m_state.world.destroy(player.body);
    m_state.moves->areBoxesStale = true;
    m_state.players[index.value].reset();
    checkWinConditions(m_state);
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
    return teamCountsOf(m_state);
}

auto Game::playerCount() const -> std::size_t {
    return static_cast<std::size_t>(std::ranges::distance(playersIn(m_state.players)));
}

auto Game::nameOf(PlayerIndex player) const -> std::string_view {
    return playerAt(m_state, player).name;
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
    const Player& player = playerAt(m_state, index);
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

void Game::give(PlayerIndex index, WeaponIndex weapon) {
    giveWeapon(m_state, playerAt(m_state, index), weapon);
}

void Game::setAutoSwitch(PlayerIndex index, AutoSwitch autoSwitch) {
    playerAt(m_state, index).autoSwitch = autoSwitch;
}

void Game::giveArmor(PlayerIndex index, ArmorKind kind) {
    wearArmor(playerAt(m_state, index).status, kind);
}

auto Game::buyWeapon(PlayerIndex index, WeaponIndex weapon) -> nx::Result<void, BuyRefusal> {
    return cs::buyWeapon(m_state, playerAt(m_state, index), weapon);
}

auto Game::rebuy(PlayerIndex index) -> nx::Result<void, BuyRefusal> {
    return cs::rebuy(m_state, playerAt(m_state, index));
}

auto Game::buyArmor(PlayerIndex index, ArmorKind kind) -> nx::Result<void, BuyRefusal> {
    return cs::buyArmor(m_state, playerAt(m_state, index), kind);
}

auto Game::buyAmmo(PlayerIndex index, WeaponSlot slot, AmmoPurchase purchase)
    -> nx::Result<void, BuyRefusal> {
    return cs::buyAmmo(m_state, playerAt(m_state, index), slot, purchase);
}

auto Game::snapshotFor(PlayerIndex player) -> PlayerSnapshot {
    return cs::snapshotFor(m_state, player);
}

auto Game::toggleNoclip(PlayerIndex index) -> bool {
    MovementState& movement = playerAt(m_state, index).movement;
    const bool isFlying = movement.moveType == MoveType::Walk;
    movement.moveType = isFlying ? MoveType::Noclip : MoveType::Walk;
    return isFlying;
}

void Game::placeEye(PlayerIndex index, nx::Vec3 eye) {
    Player& player = playerAt(m_state, index);
    standAt(m_state, player, eye - nx::Vec3{.z = player.movement.viewHeight.value()});
    m_state.moves->areBoxesStale = true;
}

void Game::tick(std::span<const PlayerCommand> commands, std::span<const EntityCommand> entities,
                nx::jobs::JobSystem* jobs) {
    m_state.events.clear();
    m_state.moves->jobs = jobs;
    m_state.moves->areBrushesStale = true;
    m_state.moves->areBoxesStale = true;
    for (Player& player : playersIn(m_state.players)) {
        player.turnedView.reset();
        player.wasPlaced = false;
        player.sounds = std::exchange(player.soundsOfPurchases, {});
        player.hurtFrom.reset();
        player.damageDealt.clear();
    }
    for (const PlayerCommand& command : commands) {
        runCommand(m_state, playerAt(m_state, command.player), command.command);
    }
    std::vector<nx::Vec3> watchers;
    for (const Player& player : playersIn(m_state.players)) {
        if (player.status.isAlive()) {
            watchers.push_back(player.movement.origin);
        }
    }
    EntityBodiesTick bodies = runEntityBodies(m_state.world, m_state.map.hulls(), entities, jobs, watchers);
    // They moved, and what they touched may have opened.
    m_state.moves->areBrushesStale = true;
    m_state.moves->areBoxesStale = true;
    m_state.events.entitySteps = std::move(bodies.steps);
    for (const EntityAttack& strike : bodies.attacks) {
        if (const PlayerBody* const body = m_state.world.get<PlayerBody>(strike.target)) {
            // A strike met the player where its trace did, as a TraceAttack sets m_LastHitGroup.
            Player& struck = playerAt(m_state, body->player);
            struck.lastHitGroup = hitGroupOf(strike.attack.trace);
            feelHurt(struck, strike.attack, std::nullopt);
            hurt(m_state, struck, {.damage = strike.attack.damage, .kind = HarmKind::Melee});
        } else {
            std::ignore = nx::game::attackEntity(m_state.world, strike.target, strike.attack);
        }
    }
    thinkRules(m_state);
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
    // Its rounds may have placed its bodies anew.
    m_state.moves->areBrushesStale = true;
    m_state.moves->areBoxesStale = true;
    m_state.moves->jobs = nullptr;
    refreshEntityStates(m_state, jobs);
}

auto Game::createEntity(EntityTypeIndex type, nx::game::Transform placement)
    -> nx::Result<nx::ecs::Entity, EntityError> {
    nx::game::Solids& solids = solidsNow(m_state);
    auto made = spawnEntityBody(m_state.world, m_state.map.hulls(), solids, type, placement);
    if (made) {
        m_state.entityStates.isStale = true;
        solids.boxes.add({.entity = *made,
                          .origin = m_state.world.get<nx::game::Transform>(*made)->origin,
                          .box = m_state.world.get<nx::game::BoxSolidity>(*made)->box,
                          .clip = *m_state.world.get<nx::game::ClipMethod>(*made)});
    }
    return made;
}

auto Game::removeEntity(nx::ecs::Entity entity) -> nx::Result<void, EntityError> {
    if (!m_state.world.isAlive(entity) || !m_state.world.has<EntityBody>(entity)) {
        return nx::fail(EntityError::UnknownEntity, "a live entity-body handle is required");
    }
    m_state.world.destroy(entity);
    m_state.moves->solids.boxes.forget(entity);
    m_state.entityStates.isStale = true;
    return {};
}

auto Game::entities() -> EntityStates {
    return entityStatesNow(m_state);
}

auto Game::movingPlayers() const -> std::vector<MovingBody> {
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
    return bodies;
}

auto Game::movingBodies() -> std::vector<MovingBody> {
    std::vector<MovingBody> bodies = movingPlayers();
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

auto Game::sightSolids() -> std::vector<nx::game::SolidBrushEntity> {
    return nx::game::solidBrushesOf(m_state.world, m_state.map.hulls());
}

auto Game::seesSpot(std::span<const nx::game::SolidBrushEntity> solids, nx::Vec3 eye, nx::Vec3 spot) const
    -> bool {
    return nx::game::lineReaches(m_state.map.hulls(), solids, eye, spot);
}

auto Game::leavesSeenFrom(nx::Vec3 eye) const -> nx::assets::LeafSet {
    const nx::assets::BspView& map = m_state.map.map();
    return map.potentiallyVisible(map.leafAt(eye));
}

auto Game::isInLeaves(const nx::assets::LeafSet& leaves, nx::Vec3 spot) const -> bool {
    return leaves.contains(m_state.map.map().leafAt(spot));
}

auto Game::navigationGeometry() -> nx::navigation::Geometry {
    return nx::game::navigationGeometryOf(m_state.map.map(), m_state.world);
}

auto Game::time() const -> nx::game::TickTime {
    return nx::game::tickTimeOf(m_state.world);
}

} // namespace cs
