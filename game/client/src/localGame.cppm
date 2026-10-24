// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game of a level as the client plays it alone, until its server comes (cs.server): the map's
/// brush entities as the game makes them, and the player among them, moved by the game's own
/// movement one user command a tick (docs/07-simulation-and-physics.md, section 2.2), with the
/// weapons a player of its team spawns with (cs::Inventory).
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
    [[nodiscard]] auto delayOf(nx::GameTime made) const noexcept -> nx::Seconds {
        return std::max(made.since(from, rate), nx::Seconds{});
    }
};

/// The game of a level and its player, played by the client alone.
class LocalGame {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The game of `level` of `content`, which must both outlive it, as it starts, and its player
    /// standing with its eye at `eye`, given what a player of `team` spawns with, drawing the
    /// heaviest. Errors: GameUnspawnable.
    [[nodiscard]] static auto start(const GameContent& content, const Level& level, Team team, nx::Vec3 eye)
        -> nx::Result<std::unique_ptr<LocalGame>, LevelError>;

    /// Use start(). Public only so that start() can use std::make_unique.
    LocalGame(CreateToken token, const GameContent& content, const Level& level, nx::Vec3 eye);

    LocalGame(const LocalGame&) = delete;
    auto operator=(const LocalGame&) -> LocalGame& = delete;
    LocalGame(LocalGame&&) = delete;
    auto operator=(LocalGame&&) -> LocalGame& = delete;
    ~LocalGame() = default;

    /// Gives `elapsed` to the game, tick by tick, catching up on a quarter of a second at most: the
    /// next command of `input`, looking along `view`, numbered after the last, draws the weapon it
    /// selects, moves the player among the brush entities at the speed of the weapon it holds, the use
    /// button using what is in front as it goes down, and the doors it meets and the triggers it is in
    /// are touched; the weapon in hand fires, is reloaded or idles (cs::Inventory::frame); then what is
    /// due acts, and the pushers move, pushing the player's body. Returns the player's eye, between
    /// where the last two ticks left it by the time left.
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
    /// its loadout gives one, drawn when heavier; the speed it runs at follows the weapon it holds.
    void give(WeaponIndex weapon);
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

    const Level* m_level;
    nx::ecs::World m_world;
    nx::ecs::Entity m_body{};
    std::vector<Impact> m_impacts; ///< Since takeImpacts.
    MovementState m_player{};
    PlayerStatus m_status{};
    Inventory m_inventory;
    CommandNumber m_lastCommand{};     ///< The number of the last command the player made.
    nx::Vec3 m_previousEye{};          ///< Before the last command.
    nx::Seconds m_pending{};           ///< Of the time given to the game, what no tick has taken yet.
    nx::Flags<Button> m_lastButtons{}; ///< Of the last command: the use button acts as it goes down.
};

} // namespace cs
