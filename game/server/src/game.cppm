// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game of a map, as its server plays it (docs/07-simulation-and-physics.md, section 2.2): the map's
/// brush entities and hostages as the game makes them, and its players among them, joining a team and
/// solid to each other, each moved by the game's own movement one user command a tick, with the weapons
/// a player of its team spawns with (cs::Inventory), in the rounds of the map (cs::Rounds), ended by a
/// team eliminated and begun by Game Commencing as both teams have players, spawning again at their
/// team's spawn points in turn as each starts, paid by them (cs::TeamAccounts); their feet sound by what
/// they walk on (cs::StepVoice); their hard landings, drowning and harmful water hurt them, and kill them
/// (cs.shared:playerHarm); their shots and knives meet each other's hitboxes (cs.server:playerBodies),
/// hurt them by where they meet them and through their armor, and pay for a kill; as ReGameDLL_CS's
/// CBasePlayer and CHalfLifeMultiplay play them (GPL, consulted: docs/20 section 3). After each tick it
/// tells each player's client what it needs (PlayerSnapshot).
///
/// Today: the players hurt by the world and by each other's shots and knives; the grenades, the bomb, the
/// blood and the scores come with them (TODO.md).
export module cs.server:game;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.game;
import nx.navigation;
import cs.shared;
import :gameCvars;
import :gameModels;
import :entityBodies;
import :playerBodies;

export namespace cs {

/// Why the game of a map could not start.
enum class GameError : std::uint8_t {
    Unspawnable, ///< The map's entities or its hostages could not be made (cause).
};

/// A player's command for a tick.
struct PlayerCommand {
    PlayerIndex player{};
    UserCommand command{};
};

/// A living player's body that an agent's perception may consider: its entity, the player and its team,
/// its origin and eye, its feet, how it moves, and whether it is ducked. A candidate, not a fact
/// perceived: Game::sees and Game::seesSpot tell whether an eye sees it.
struct PerceptionTarget {
    nx::ecs::Entity entity{};
    PlayerIndex player{};
    Team team{};
    nx::Vec3 origin{};
    nx::Vec3 eye{};
    nx::Vec3 feet{}; ///< The active body's lower center, independent of its stance.
    nx::Vec3 velocity{};
    bool isDucked = false;
};

/// What a player knows of itself, as its own client is told it: its team, whether it is alive, its eye
/// and its feet, how it moves, where it looks (pev->v_angle, which the game turns as it spawns), how
/// fast it may run (pev->maxspeed), and the slot of the weapon it holds, none while it holds none.
struct PlayerOwnState {
    Team team{};
    bool isAlive = false;
    nx::Vec3 eye{};
    nx::Vec3 feet{};
    nx::Vec3 velocity{};
    nx::EulerAngles view{};
    nx::UnitsPerSecond maxSpeed{};
    std::optional<WeaponSlot> heldSlot{};
};

} // namespace cs

namespace cs {

/// A player of the game, as its server keeps it (CBasePlayer): its team and _cl_autowepswitch, where it
/// is and how it moves, what it has, its weapons, its feet's and its breath's voices, the harms it keeps
/// over time, its body among the world's entities, the buttons of its last command, whether the next
/// round pays it, how often it spawned in the round, what its HUD was told last of the round's time,
/// and what the tick did to it and made it make, which its client is told of; the character it plays
/// as, where it looks (pev->v_angle), how its model plays, and the seed of the command it runs, which what
/// it does draws from (random_seed); the hit group an attack last met it in (m_LastHitGroup), and the
/// share of its speed a hit slowed it down to (m_flVelocityModifier). It joins dead, and lives once it
/// spawns.
struct Player {
    Team team;
    AutoSwitch autoSwitch;
    CharacterIndex character;
    MovementState movement{};
    PlayerStatus status;
    Inventory inventory;
    StepVoice stepVoice;
    Breath breath;
    TimedHarms timedHarms{};
    nx::ecs::Entity body{};
    nx::Flags<Button> lastButtons{}; ///< m_afButtonLast: the use button acts as it goes down.
    bool isPaidNextRound = true; ///< Not when the last end leaves it unpaid (m_bReceivesNoMoneyNextRound).
    std::uint32_t spawnsThisRound = 0; ///< m_iNumSpawns.
    RoundTimer roundTimer{};
    std::optional<nx::EulerAngles> turnedView{}; ///< In the tick.
    bool wasPlaced = false;                      ///< In the tick.
    std::vector<SoundMade> sounds{};             ///< In the tick, but its weapons', which they keep.
    nx::EulerAngles view{};
    PlayerAnimation animation{};
    CommandNumber randomSeed{};
    std::uint32_t lastHitGroup = 0;
    float speedShare = 1.0F;
};

} // namespace cs

export namespace cs {

/// The game of a map, played tick by tick by its players' commands.
class Game {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The game of `map` of `content`, which must both outlive it, as it starts: its brush entities and
    /// hostages made, its rounds played by the rules `cvars` set, the first frozen; no player yet.
    /// Errors: Unspawnable.
    [[nodiscard]] static auto start(const GameContent& content, const GameMap& map, GameCvars cvars)
        -> nx::Result<std::unique_ptr<Game>, GameError>;

    /// Use start(). Public only so that start() can use std::make_unique.
    Game(CreateToken token, const GameContent& content, const GameMap& map, GameCvars cvars,
         GameModels models);

    Game(const Game&) = delete;
    auto operator=(const Game&) -> Game& = delete;
    Game(Game&&) = delete;
    auto operator=(Game&&) -> Game& = delete;
    ~Game() = default;

    /// A player joins the game in `team`, with mp_startmoney, its weapons drawn as `autoSwitch` says (its
    /// _cl_autowepswitch, until setAutoSwitch), as one of its team's characters drawn at random, as the
    /// original's appearance menu draws one to auto-select; as its JoiningThink lets it play: spawned at
    /// once when it may (canSpawn), given what a player of `team` spawns with; else watching from its
    /// team's next spawn point, dead, until the next round. Then the round's end is weighed
    /// (checkWinConditions): the first player of the second team commences the game.
    auto join(Team team, AutoSwitch autoSwitch) -> PlayerIndex;
    /// `player` leaves the game, as a client disconnects (CHalfLifeMultiplay::ClientDisconnected): its
    /// weapons and its body are gone, and its slot is free for the next player to join; then the round's
    /// end is weighed (checkWinConditions). `player` is in the game.
    void leave(PlayerIndex player);
    /// The team a player joining by auto-select joins (SelectDefaultTeam): the one of fewer players, else
    /// one of the two drawn at random, from a stream of the tick. The original then weighs the teams'
    /// wins, which come with the scores (TODO.md).
    [[nodiscard]] auto defaultTeam() const -> Team;
    /// How many players of each team are in the game, alive and dead.
    [[nodiscard]] auto teamCounts() const -> TeamCounts;
    /// How many players are in the game.
    [[nodiscard]] auto playerCount() const -> std::size_t;
    /// What `player`, in the game, knows of itself (PlayerOwnState).
    [[nodiscard]] auto ownStateOf(PlayerIndex player) const -> PlayerOwnState;

    /// One tick of the game (docs/07, section 2.2): each player of `commands`, in their order, a command
    /// at most for each, draws the weapon it selects and moves among the brush entities at the speed of
    /// the weapon it holds, and the doors it meets and the triggers it is in are touched; the use button
    /// going down uses what is in front, the weapon in hand fires, is reloaded or idles
    /// (cs::Inventory::frame), and a hard landing hurts it; then command-driven entity bodies move
    /// once and make their attacks, and the rules of the round play
    /// (thinkRules), what is due acts, and the pushers move, pushing the players' bodies. While the
    /// round is frozen a player moves at kFreezeSpeed at most, and its attack is held back; dead, it
    /// does nothing until it spawns again.
    void tick(std::span<const PlayerCommand> commands, std::span<const EntityCommand> entities = {});

    /// A body of a type issued by this game's content catalog. Its origin/yaw must be finite and
    /// its pitch/roll zero. Errors: InvalidPlacement. No player slot is allocated.
    [[nodiscard]] auto createEntity(EntityTypeIndex type, nx::game::Transform placement)
        -> nx::Result<nx::ecs::Entity, EntityError>;
    /// Removes only a body issued by createEntity. Errors: UnknownEntity for a stale/non-body handle.
    [[nodiscard]] auto removeEntity(nx::ecs::Entity entity) -> nx::Result<void, EntityError>;
    /// Body states, including corpses, ordered by their complete generational handles.
    [[nodiscard]] auto entities() -> std::vector<EntityState>;
    /// Living player bodies a mod may consider; these are candidates, not already perceived facts.
    /// Feet come from each active collision hull, independently of its current eye height.
    [[nodiscard]] auto perceptionTargets() const -> std::vector<PerceptionTarget>;
    /// Whether the solid trace from eye reaches this candidate's eye, leaving out the observer.
    [[nodiscard]] auto sees(nx::ecs::Entity observer, nx::Vec3 eye, const PerceptionTarget& target) -> bool;
    /// Whether a line from `eye` reaches `spot` through the world and the solid brush entities alone, the
    /// bodies leaving it be, as a bot's IsVisible traces one (UTIL_TraceLine with ignore_monsters).
    [[nodiscard]] auto seesSpot(nx::Vec3 eye, nx::Vec3 spot) -> bool;
    /// World and solid brush surfaces where they are now, without moving bodies.
    [[nodiscard]] auto navigationGeometry() -> nx::navigation::Geometry;
    /// The authoritative clock, independent of a command's requested duration.
    [[nodiscard]] auto time() const -> nx::game::TickTime;

    /// Native extension components are registered before attachment, once a game: by `name`, or by the
    /// name the first extension registering the type gave it, which every one may register, as the bots
    /// and the test zombies both register nx::agents::Agent. A component pointer is borrowed only until
    /// the next structural change of the world's entities.
    template <nx::ecs::Component Component>
    [[nodiscard]] auto registerEntityComponent(std::string_view name)
        -> nx::Result<void, nx::ecs::ComponentError> {
        if (m_world.components().find<Component>()) {
            return {};
        }
        auto registered = m_world.registerComponent<Component>(name);
        if (!registered) {
            return std::unexpected{std::move(registered.error())};
        }
        return {};
    }
    template <nx::ecs::Component Component>
    [[nodiscard]] auto attachEntityComponent(nx::ecs::Entity entity, Component component)
        -> nx::Result<void, EntityError> {
        if (!m_world.isAlive(entity) || !m_world.has<EntityBody>(entity)) {
            return nx::fail(EntityError::UnknownEntity, "an entity-body handle is required");
        }
        m_world.add(entity, std::move(component));
        return {};
    }
    template <nx::ecs::Component Component>
    [[nodiscard]] auto entityComponent(nx::ecs::Entity entity) -> Component* {
        return m_world.has<EntityBody>(entity) ? m_world.get<Component>(entity) : nullptr;
    }
    /// Attaches `component`, registered, to the body of `player`, in the game, which the body keeps until
    /// the player leaves.
    template <nx::ecs::Component Component>
    void attachPlayerComponent(PlayerIndex player, Component component) {
        m_world.add(playerAt(player).body, std::move(component));
    }
    /// The component of `player`'s body, in the game, none when it has none.
    template <nx::ecs::Component Component>
    [[nodiscard]] auto playerComponent(PlayerIndex player) -> Component* {
        return m_world.get<Component>(playerAt(player).body);
    }

    /// What `player`'s client is told after the last tick (PlayerSnapshot).
    [[nodiscard]] auto snapshotFor(PlayerIndex player) -> PlayerSnapshot;

    /// The rounds of the map.
    [[nodiscard]] inline auto rounds() const noexcept -> const Rounds& { return m_rounds; }

    /// Gives `player` `weapon` (its replacement, when it is replaced), which must be complete, as its
    /// loadout gives one, drawn as its _cl_autowepswitch says; the speed it runs at follows the weapon it
    /// holds.
    void give(PlayerIndex player, WeaponIndex weapon);
    /// Sets `player`'s _cl_autowepswitch, as its client tells the server of it: whether a weapon it is
    /// given heavier than the one it holds is drawn.
    void setAutoSwitch(PlayerIndex player, AutoSwitch autoSwitch);
    /// Gives `player` armor of `kind`, as an item of it does (wearArmor): item_kevlar's and
    /// item_assaultsuit's, which the original's `give` names.
    void giveArmor(PlayerIndex player, ArmorKind kind);
    /// Turns noclip on or off for `player`, as the original's `noclip` cheat does; returns whether it is
    /// on now.
    auto toggleNoclip(PlayerIndex player) -> bool;
    /// Stands `player` still with its eye at `eye`, wherever that is: the view a tool puts it at
    /// (nxview's --position).
    void placeEye(PlayerIndex player, nx::Vec3 eye);

private:
    /// The player `index` names, in the game.
    [[nodiscard]] auto playerAt(PlayerIndex index) -> Player&;
    [[nodiscard]] auto playerAt(PlayerIndex index) const -> const Player&;
    /// What `player` does with `command` in the tick: it selects, thinks before it moves (preThink),
    /// moves and touches what it meets, then thinks after (postThink), alive.
    void runCommand(Player& player, const UserCommand& command);
    /// What `player` does before `command` moves it (PreThink): alive, it breathes in the water the last
    /// command left it in (Breath::breathe), hurt as it drowns and by harmful water; on the ground, slowed
    /// down by a hit, it is a hundredth of its speed less slow each command, its velocity that share of it
    /// (m_flVelocityModifier); then the harms it keeps over time are looked at, and what drowning took
    /// given back to it alive (TimedHarms::check).
    void preThink(Player& player, const UserCommand& command);
    /// What a living `player` does once `command` moved it from `before` (PostThink): unless it is held
    /// back (Inventory::isHeldBack), the use key going down uses what is in front (use), then the weapon
    /// in hand fires, is reloaded or idles, its attacks dealt to what they meet (ItemPostFrame); then its
    /// landing hurts it (landingDamageOf).
    void postThink(Player& player, const UserCommand& command, const MovementState& before);
    /// `player` uses what is in front of it, looking along `command`'s view from where the move left its
    /// eye (CBasePlayer::PlayerUse), heard finding something to use or nothing (UseEmpty).
    void use(Player& player, const UserCommand& command);
    /// `player` lands, hurt by `damage` (PostThink): it splats when the damage is more than its health,
    /// is hurt by its fall (hurt), and loses the pitch of its kick.
    void land(Player& player, nx::Damage damage);
    /// `player` is dealt `harm` of the world (CBasePlayer::TakeDamage): alive, its armor takes its share
    /// and it loses its whole points, keeping a harm kept over time (harmPlayer), and is heard in its pain
    /// by where it was last met (painOf); a fall met it in no group; killed by it, it dies (die).
    void hurt(Player& player, const PlayerHarm& harm);
    /// `attacker`'s `attack` on `victim`, alive, as the victim's TraceAttack and TakeDamage take it: it is
    /// met in the attack's hit group, its damage that group's share of the players'; its view punched
    /// (punchByHit) but by a teammate while mp_friendlyfire is 0, whose attack then hurts it none
    /// (FPlayerCanTakeDamage); a teammate's attack deals its share of it (PlayerAttackRules); then it
    /// flinches, large as its attacker's weapon makes it but in the legs and ducked, knocked back while it
    /// moves slow enough, else small, slowed down; its armor takes its share, its attacker's weapon's
    /// times the armor's ratio (WeaponHarm::armorRatio), and it loses its whole points (harmPlayer), heard
    /// in its pain (painOf); killed by it, it dies, killed by `attacker` (die). The punch's draws come
    /// from a stream of the attacker's command and the victim.
    void attackPlayer(Player& attacker, Player& victim, const nx::game::Attack& attack);
    /// `player` dies (Killed), killed by `killer`, none for the world: it is heard dying, moves no more
    /// (MovementState::isDead), plays its death by where it was last met, is no longer solid, and its
    /// weapons are gone with it; its killer, another player, gets the money of an enemy's kill, or loses a
    /// teammate's (CHalfLifeMultiplay::PlayerKilled, KillMoney); then the round's end is weighed
    /// (checkWinConditions).
    void die(Player& player, Player* killer);
    /// Weighs the round's end by the players alive and dead of each team (CheckWinConditions,
    /// Rounds::checkWinConditions), what an end does to the money then played (roundEnded).
    void checkWinConditions();
    /// Whether `player`, joining, spawns now (FPlayerCanRespawn): not when it spawned in the round
    /// already, nor, the game having players of both teams, once the round started kJoinSpawnWindow ago.
    [[nodiscard]] auto canSpawn(const Player& player) const -> bool;
    /// `player` watches until the next round, dead, from its team's next spawn point, turned the way it
    /// faces (StartObserver at GetPlayerSpawnSpot).
    void observe(Player& player);
    /// What a move of `mover` meets as boxes: the world's (solidBoxesOf), the other players alive among
    /// them, each standing or ducked, as the engine lists them for movement (SV_AddLinksToPM's
    /// SOLID_SLIDEBOX).
    [[nodiscard]] auto boxesMetBy(const Player& mover) -> std::vector<SolidBox>;
    /// `player`'s body where it is: at its origin, its box among the bodies the pushers move, and, alive,
    /// solid to the server's traces as its box, standing or ducked (setBodySolidity); dead, not solid.
    void placeBody(Player& player);
    /// `player`'s body posed for the server's traces as its model plays (PlayerPose): its torso's sequence
    /// and frame, its legs' gait sequence and yaw, the aim's blends and its view's body angles.
    void poseBody(Player& player);
    /// `player` makes `sound` at the tick, at its own volume.
    void makeSound(Player& player, const GameSound& sound);
    /// `player`'s model plays `action` (setPlayerAnimation, CBasePlayer::SetAnimation), as the player
    /// stands and moves, by the weapon it holds, drawing from its command's seed; nothing for a character
    /// whose model could not be read.
    void animate(Player& player, PlayerAction action);
    /// What `player`'s model plays as it ends a command of `frameTime` (PostThink): alive, walking while it
    /// moves on the ground or in water, else standing (but, as the original compares the gait sequence
    /// with ACT_FLY's number, while its legs play the sixth), its torso's frame advanced; dead or alive,
    /// its gait (processGait, StudioProcessGait) turned by its view.
    void animateAfterCommand(Player& player, nx::Seconds frameTime);
    /// The sounds `sounds`, made by `player`'s movement at the tick of `command` among `entities`, as it
    /// hears them: each step by what it steps on (GameMap::materialAlong, in the space of the entity it
    /// stands on), while mp_footsteps lets them sound; each splash.
    void hearMovement(Player& player, std::span<const MovementSound> sounds,
                      std::span<const BrushEntity> entities, const UserCommand& command);
    /// What the ground a player stood on at `ground` is made of: the texture kStepTextureDepth under it,
    /// on the world or on the entity among `entities` it stood on (PM_CatagorizeTextureType).
    [[nodiscard]] auto groundMaterial(const StepGround& ground, std::span<const BrushEntity> entities) const
        -> TextureMaterial;
    /// The rules of the round at the tick (CHalfLifeMultiplay::Think): once a second the restart the
    /// cvars ask for (GameCvars::takeRestart); then Rounds::think, a round that starts as long as the
    /// cvars say. As the freeze ends each player's HUD is told the round's time and the players run
    /// again; as a round starts the players spawn again (spawn); as the game restarts they spawn as they
    /// joined: with mp_startmoney, without armor, and with their team's loadout alone.
    void thinkRules();
    /// What the end of a round by `reason` does to the money (TerminateRound), logged: what its winners
    /// get goes to their team's account, and a player alive on the losing team is not paid as the next
    /// round starts when the end does not pay its living losers.
    void roundEnded(RoundEndReason reason);
    /// Pays each player what its team gets as a round starts (RestartRound), `hostagesHeld` still held
    /// (repositionHostages), unless the last end left it unpaid.
    void payForRound(std::uint32_t hostagesHeld);
    /// `player`'s `attack` on `entity`, as its weapon's TraceAttack and ApplyMultiDamage deal it: to
    /// another player's body as attackPlayer deals it; to another entity as its kind takes it
    /// (nx::game::attackEntity), a hostage it hurts costing it its money (hostageHarmCost).
    void attackEntity(Player& player, nx::ecs::Entity entity, const nx::game::Attack& attack);
    /// What the texture a trace from `start` to `end` met, `hit`, is made of, as UTIL_TextureHit finds
    /// it: flesh for a body; of the world's or a brush entity's model, as materials.txt tags it; none for
    /// another entity.
    [[nodiscard]] auto materialOfHit(const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end) const
        -> std::optional<TextureMaterial>;
    /// Spawns `player` at the next spawn point of its team (nextSpawnPoint), as Spawn does: one unit
    /// above it, standing still and turned the way it faces, its health whole, its speed no longer
    /// slowed; alive, it keeps its armor, its weapons and its money; dead, it keeps its money alone,
    /// given what a player of its team spawns with. It is paid as the next round starts, and has spawned
    /// once more in the round. Its HUD is told the round's time.
    void spawn(Player& player);
    /// Gives `player` what a player of its team spawns with, drawn as its _cl_autowepswitch says.
    void equip(Player& player);
    /// The spawn point a player of `team` spawns at next, as EntSelectSpawnPoint picks it: the first of
    /// the team's after the one its last player spawned at, the first after the last; the terrorists'
    /// first when the team has none; the world's origin without either.
    [[nodiscard]] auto nextSpawnPoint(Team team) -> SpawnPoint;
    /// How fast `player` may run: kFreezeSpeed while the round is frozen, else as the weapon it holds
    /// allows (ResetMaxSpeed).
    [[nodiscard]] auto maxSpeed(const Player& player) const -> nx::UnitsPerSecond;
    /// Stands `player` still at `origin`, its body there, and its client told it was placed.
    void standAt(Player& player, nx::Vec3 origin);

    const GameContent* m_content;
    const GameMap* m_map;
    GameCvars m_cvars;
    GameModels m_models; ///< What the world's hostages play: they point into it.
    Rounds m_rounds;
    TeamAccounts m_accounts;
    nx::GameTime m_nextPeriodicThink{}; ///< When the rules look at the restart cvars next.
    /// Of each team's, the one its last player spawned at (the original's g_pLastTerroristSpawn and
    /// g_pLastCTSpawn).
    nx::EnumArray<Team, std::optional<std::size_t>> m_lastSpawnPoints{};
    nx::ecs::World m_world;
    std::vector<std::optional<Player>> m_players; ///< By slot: none for a slot a player left.
    std::vector<Impact> m_impacts;                ///< Of the tick.
    MapLightStyles lightStyles;                   ///< Owned by this game; copied into each client's snapshot.
};

} // namespace cs
