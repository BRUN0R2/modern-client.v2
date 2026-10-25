// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game of a level as the client plays it alone, until its server comes (cs.server): the map's
/// brush entities as the game makes them, and the player among them, moved by the game's own
/// movement one user command a tick (docs/07-simulation-and-physics.md, section 2.2), with the
/// weapons a player of its team spawns with (cs::Inventory), in the rounds of the map (cs::Rounds),
/// spawning again at its team's spawn points as each starts, paid by them (cs::TeamAccounts).
export module cs.client:localGame;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.render;
import cs.shared;
import :input;
import :level;
import :viewModel;
import :gameCvars;

export namespace cs {

/// Where a shot met what is solid: the point, and what it is on, the world or a brush entity where the
/// game had it then, which a bullet leaves its mark on.
struct Impact {
    nx::Vec3 position{};
    nx::render::DecalTarget on{};
};

/// When a frame plays what the game made: what came after `from`, the moment of the game the frame
/// before showed, is heard as long after the frame as it came after that moment. A frame that ran
/// many ticks plays their sounds spread as they were made, not all at once: a weapon's cadence is
/// heard the same at any frame rate, a frame later at most.
struct Hearing {
    nx::GameTime from{};
    nx::TickRate rate{nx::kDefaultTickRate};

    /// How long after the frame what was made at `made` is heard: at once when not after `from`.
    [[nodiscard]] inline auto delayOf(nx::GameTime made) const noexcept -> nx::Seconds {
        return std::max(made.since(from, rate), nx::Seconds{});
    }
};

/// The game of a level and its player, played by the client alone.
class LocalGame {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The game of `level` of `content`, which must both outlive it, as it starts: its rounds played
    /// by the rules `cvars` set, the first frozen; its player, of `team`, spawned at its team's first
    /// spawn point, given what a player of `team` spawns with, drawn as `autoSwitch` says (the player's
    /// _cl_autowepswitch until setAutoSwitch). Errors: GameUnspawnable.
    [[nodiscard]] static auto start(const GameContent& content, const Level& level, Team team,
                                    AutoSwitch autoSwitch, GameCvars cvars)
        -> nx::Result<std::unique_ptr<LocalGame>, LevelError>;

    /// Use start(). Public only so that start() can use std::make_unique.
    LocalGame(CreateToken token, const GameContent& content, const Level& level, Team team,
              AutoSwitch autoSwitch, GameCvars cvars);

    LocalGame(const LocalGame&) = delete;
    auto operator=(const LocalGame&) -> LocalGame& = delete;
    LocalGame(LocalGame&&) = delete;
    auto operator=(LocalGame&&) -> LocalGame& = delete;
    ~LocalGame() = default;

    /// Gives `elapsed` to the game, tick by tick, catching up on a quarter of a second at most: the
    /// next command of `input`, looking along `view`, numbered after the last, draws the weapon it
    /// selects, moves the player among the brush entities at the speed of the weapon it holds, the use
    /// button using what is in front as it goes down, and the doors it meets and the triggers it is in
    /// are touched; the weapon in hand fires, is reloaded or idles (cs::Inventory::frame); then the
    /// rules of the round play (thinkRules), what is due acts, and the pushers move, pushing the
    /// player's body. While the round is frozen the player moves at kFreezeSpeed at most, and its
    /// attack is held back. Returns the player's eye, between where the last two ticks left it by the
    /// time left.
    auto play(PlayerInput& input, nx::EulerAngles view, nx::Seconds elapsed) -> nx::Vec3;

    /// As play(), all of `elapsed`, each command looking along `view` and asking nothing else: the
    /// player stands, as a frame of nxview shows it `elapsed` after the map started.
    auto wait(nx::EulerAngles view, nx::Seconds elapsed) -> nx::Vec3;

    /// What the view model shows when the eye is drawn: none while the player holds no weapon.
    [[nodiscard]] auto viewModel() const -> std::optional<ViewModelPose>;
    /// How the next frame hears what the game makes until it (Hearing): from the moment of the game
    /// a frame shows now.
    [[nodiscard]] auto hearing() const -> Hearing;
    /// The kick the player's weapon gave its view (punchangle), which the view is drawn turned by.
    [[nodiscard]] inline auto punch() const noexcept -> nx::EulerAngles { return m_player.punch; }
    /// The player's eye, where the last tick left it.
    [[nodiscard]] auto eye() const noexcept -> nx::Vec3;
    /// The way the game turned the player's view since the last call, as the original's fixangle
    /// turns it: the way the spawn point it last spawned at faces; none.
    [[nodiscard]] inline auto takeTurnedView() -> std::optional<nx::EulerAngles> {
        return std::exchange(m_turnedView, std::nullopt);
    }
    /// Stands the player still with its eye at `eye`, wherever that is: the view a tool puts it at
    /// (nxview's --position).
    void placeEye(nx::Vec3 eye);

    /// The rounds of the map.
    [[nodiscard]] inline auto rounds() const noexcept -> const Rounds& { return m_rounds; }
    /// The whole seconds the player's HUD shows of the round's time (RoundTimer::shownAt), at the
    /// moment of the game a frame shows.
    [[nodiscard]] auto roundTimeShown() const -> std::uint32_t;

    /// Turns noclip on or off, as the original's `noclip` cheat does; returns whether it is on now.
    auto toggleNoclip() -> bool;

    /// Where the game puts its brush entities now, how high their waves rise, and which textures they
    /// show: what the scene draws them by.
    [[nodiscard]] auto placements() -> std::vector<nx::render::ModelPlacement>;

    /// What the player has: what it spawned with, until the game's rules change it.
    [[nodiscard]] inline auto status() const noexcept -> const PlayerStatus& { return m_status; }
    /// The player's weapons.
    [[nodiscard]] inline auto inventory() const noexcept -> const Inventory& { return m_inventory; }
    /// Gives the player `weapon` (its replacement, when it is replaced), which must be complete, as
    /// its loadout gives one, drawn as its _cl_autowepswitch says; the speed it runs at follows the
    /// weapon it holds.
    void give(WeaponIndex weapon);
    /// Sets the player's _cl_autowepswitch, as its client tells the server of it: whether a weapon
    /// it is given heavier than the one it holds is drawn.
    inline void setAutoSwitch(AutoSwitch autoSwitch) noexcept { m_autoSwitch = autoSwitch; }
    /// Where the player's shots met the world and the brush entities since the last call, in their
    /// order: where a bullet leaves its mark.
    [[nodiscard]] inline auto takeImpacts() -> std::vector<Impact> { return std::exchange(m_impacts, {}); }
    /// The sounds the player's weapons made since the last call, each with the moment of its tick
    /// (Inventory::takeSounds).
    [[nodiscard]] inline auto takeSounds() -> std::vector<SoundMade> { return m_inventory.takeSounds(); }

private:
    /// The moment of the game a frame shows: a tick behind the game, by the time pending, as the eye
    /// is drawn.
    [[nodiscard]] auto shownTime() const -> nx::GameTime;
    /// Plays the time pending a tick at a time, the commands made by `nextCommand`; returns the eye
    /// as play() does.
    auto run(const std::function<UserCommand()>& nextCommand) -> nx::Vec3;
    /// One tick of the game, `command` the player's.
    void tick(const UserCommand& command);
    /// The rules of the round at the tick (CHalfLifeMultiplay::Think): once a second the restart the
    /// cvars ask for (GameCvars::takeRestart); then Rounds::think, a round that starts as long as the
    /// cvars say. As the freeze ends the player's HUD is told the round's time and the player runs
    /// again; as a round starts the player spawns again (spawn); as the game restarts it spawns as it
    /// joined: with mp_startmoney, without armor, and with its team's loadout alone.
    void thinkRules();
    /// What the end of a round by `reason` does to the money (TerminateRound): what its winners get
    /// goes to their team's account, and the player, alive on the losing team, is not paid as the next
    /// round starts when the end does not pay its living losers.
    void roundEnded(RoundEndReason reason);
    /// Pays the player what its team gets as a round starts (RestartRound), unless the last end left
    /// it unpaid.
    void payForRound();
    /// Spawns the player at the next spawn point of its team (nextSpawnPoint), as Spawn does: one
    /// unit above it, standing still and turned the way it faces (takeTurnedView), its health whole;
    /// it keeps its armor, its weapons and its money, and is paid as the next round starts. Its HUD is
    /// told the round's time.
    void spawn();
    /// Gives the player what a player of its team spawns with, drawn as its _cl_autowepswitch says.
    void equip();
    /// The spawn point the player spawns at next, as EntSelectSpawnPoint picks it: the first of its
    /// team's after the one it last spawned at, the first after the last; the terrorists' first when
    /// its team has none; the world's origin without either.
    [[nodiscard]] auto nextSpawnPoint() -> SpawnPoint;
    /// How fast the player may run: kFreezeSpeed while the round is frozen, else as the weapon it
    /// holds allows (ResetMaxSpeed).
    [[nodiscard]] auto maxSpeed() const -> nx::UnitsPerSecond;
    /// Stands the player still at `origin`, its body there and its eye seen there from now on.
    void standAt(nx::Vec3 origin);

    const GameContent* m_content;
    const Level* m_level;
    Team m_team;
    AutoSwitch m_autoSwitch;
    GameCvars m_cvars;
    Rounds m_rounds;
    TeamAccounts m_accounts;
    bool m_isPaidNextRound = true; ///< Not when the last end leaves it unpaid (m_bReceivesNoMoneyNextRound).
    RoundTimer m_roundTimer{};     ///< What the player's HUD was told last.
    std::optional<std::size_t> m_lastSpawnPoint{}; ///< Of its team's, the one it last spawned at.
    std::optional<nx::EulerAngles> m_turnedView{}; ///< Since takeTurnedView.
    nx::GameTime m_nextPeriodicThink{};            ///< When the rules look at the restart cvars next.
    nx::ecs::World m_world;
    nx::ecs::Entity m_body{};
    std::vector<Impact> m_impacts; ///< Since takeImpacts.
    MovementState m_player{};
    PlayerStatus m_status;
    Inventory m_inventory;
    CommandNumber m_lastCommand{};     ///< The number of the last command the player made.
    nx::Vec3 m_previousEye{};          ///< Before the last command.
    nx::Seconds m_pending{};           ///< Of the time given to the game, what no tick has taken yet.
    nx::Flags<Button> m_lastButtons{}; ///< Of the last command: the use button acts as it goes down.
};

} // namespace cs
