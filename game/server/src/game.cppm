// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game of a map, as its server plays it (docs/07-simulation-and-physics.md, section 2.2): the map's
/// brush entities and hostages as the game makes them, and its players among them, joining a team and
/// solid to each other, each moved by the game's own movement one user command a tick, with the weapons
/// a player of its team spawns with (cs::Inventory), in the rounds of the map (cs::Rounds), ended by a
/// team eliminated and begun by Game Commencing as both teams have players, spawning again at their
/// team's spawn points in turn as each starts, paid by them (cs::TeamAccounts); their feet sound by what
/// they walk on (cs::StepVoice); their hard landings, drowning and harmful water hurt them, and kill them
/// (cs.shared:playerHarm), as ReGameDLL_CS's CBasePlayer and CHalfLifeMultiplay play them (GPL,
/// consulted: docs/20 section 3). After each tick it tells each player's client what it needs
/// (PlayerSnapshot).
///
/// Today: the players hurt by the world; their attacks on each other, their hit groups and armor come
/// with the players' models (TODO.md).
export module cs.server:game;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.game;
import cs.shared;
import :gameCvars;
import :gameModels;

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

} // namespace cs

namespace cs {

/// A player of the game, as its server keeps it (CBasePlayer): its team and _cl_autowepswitch, where it
/// is and how it moves, what it has, its weapons, its feet's and its breath's voices, the harms it keeps
/// over time, its body among the world's entities, the buttons of its last command, whether the next
/// round pays it, how often it spawned in the round, what its HUD was told last of the round's time,
/// and what the tick did to it and made it make, which its client is told of; the character it plays
/// as, where it looks (pev->v_angle), how its model plays, and the seed of the command it runs, which what
/// it does draws from (random_seed). It joins dead, and lives once it spawns.
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

    /// One tick of the game (docs/07, section 2.2): each player of `commands`, in their order, a command
    /// at most for each, draws the weapon it selects and moves among the brush entities at the speed of
    /// the weapon it holds, and the doors it meets and the triggers it is in are touched; the use button
    /// going down uses what is in front, the weapon in hand fires, is reloaded or idles
    /// (cs::Inventory::frame), and a hard landing hurts it; then the rules of the round play
    /// (thinkRules), what is due acts, and the pushers move, pushing the players' bodies. While the
    /// round is frozen a player moves at kFreezeSpeed at most, and its attack is held back; dead, it
    /// does nothing until it spawns again.
    void tick(std::span<const PlayerCommand> commands);

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
    /// Turns noclip on or off for `player`, as the original's `noclip` cheat does; returns whether it is
    /// on now.
    auto toggleNoclip(PlayerIndex player) -> bool;
    /// Stands `player` still with its eye at `eye`, wherever that is: the view a tool puts it at
    /// (nxview's --position).
    void placeEye(PlayerIndex player, nx::Vec3 eye);

private:
    /// The player `index` names.
    [[nodiscard]] auto playerAt(PlayerIndex index) -> Player&;
    /// What `player` does with `command` in the tick: it selects, thinks before it moves (preThink),
    /// moves and touches what it meets, then thinks after (postThink), alive.
    void runCommand(Player& player, const UserCommand& command);
    /// What `player` does before `command` moves it (PreThink): alive, it breathes in the water the last
    /// command left it in (Breath::breathe), hurt as it drowns and by harmful water; then the harms it
    /// keeps over time are looked at, and what drowning took given back to it alive
    /// (TimedHarms::check).
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
    /// `player` is dealt `harm` (CBasePlayer::TakeDamage): alive, it feels its pain and loses its whole
    /// points, keeping a harm kept over time (harmPlayer); killed by it, it dies (die).
    void hurt(Player& player, const PlayerHarm& harm);
    /// `player` dies (Killed): it is heard dying, moves no more (MovementState::isDead), is no longer
    /// solid, and its weapons are gone with it; then the round's end is weighed (checkWinConditions).
    void die(Player& player);
    /// Weighs the round's end by the players alive and dead of each team (CheckWinConditions,
    /// Rounds::checkWinConditions), what an end does to the money then played (roundEnded).
    void checkWinConditions();
    /// Whether `player`, joining, spawns now (FPlayerCanRespawn): not when it spawned in the round
    /// already, nor, the game having players of both teams, once the round started kJoinSpawnWindow ago.
    [[nodiscard]] auto canSpawn(const Player& player) const -> bool;
    /// `player` watches until the next round, dead, from its team's next spawn point, turned the way it
    /// faces (StartObserver at GetPlayerSpawnSpot).
    void observe(Player& player);
    /// What a move of `mover` meets as boxes: the world's (solidBoxesOf), and the other players alive, each
    /// standing or ducked, as the engine lists them for movement (SV_AddLinksToPM's SOLID_SLIDEBOX).
    [[nodiscard]] auto boxesMetBy(const Player& mover) -> std::vector<SolidBox>;
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
    /// `player`'s `attack` on `entity`, as its weapon's TraceAttack and ApplyMultiDamage deal it
    /// (nx::game::attackEntity); a hostage it hurts costs it its money (hostageHarmCost).
    void attackEntity(Player& player, nx::ecs::Entity entity, const nx::game::Attack& attack);
    /// What the texture a trace from `start` to `end` met, `hit`, is made of, as UTIL_TextureHit finds
    /// it: flesh for a body; of the world's or a brush entity's model, as materials.txt tags it; none for
    /// another entity.
    [[nodiscard]] auto materialOfHit(const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end) const
        -> std::optional<TextureMaterial>;
    /// Spawns `player` at the next spawn point of its team (nextSpawnPoint), as Spawn does: one unit
    /// above it, standing still and turned the way it faces, its health whole; alive, it keeps its armor,
    /// its weapons and its money; dead, it keeps its money alone, given what a player of its team spawns
    /// with. It is paid as the next round starts, and has spawned once more in the round. Its HUD is told
    /// the round's time.
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
    std::vector<Player> m_players;
    std::vector<Impact> m_impacts; ///< Of the tick.
};

} // namespace cs
