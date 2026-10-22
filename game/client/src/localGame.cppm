// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The game of a level as the client plays it alone, until its server comes (cs.server): the map's
/// brush entities as the game makes them, and the player among them, moved by the game's own
/// movement one user command a tick (docs/07-simulation-and-physics.md, section 2.2).
export module cs.client:localGame;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.render;
import cs.shared;
import :input;
import :level;

export namespace cs {

/// The game of a level and its player, played by the client alone.
class LocalGame {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// The game of `level`, which must outlive it, as it starts, and its player standing with its
    /// eye at `eye`, a knife in hand. Errors: GameUnspawnable.
    [[nodiscard]] static auto start(const Level& level, nx::Vec3 eye)
        -> nx::Result<std::unique_ptr<LocalGame>, LevelError>;

    /// Use start(). Public only so that start() can use std::make_unique.
    LocalGame(CreateToken token, const Level& level, nx::Vec3 eye);

    LocalGame(const LocalGame&) = delete;
    auto operator=(const LocalGame&) -> LocalGame& = delete;
    LocalGame(LocalGame&&) = delete;
    auto operator=(LocalGame&&) -> LocalGame& = delete;
    ~LocalGame() = default;

    /// Gives `elapsed` to the game, tick by tick, catching up on a quarter of a second at most: the
    /// next command of `input`, looking along `view`, moves the player among the brush entities, the
    /// use button using what is in front as it goes down, and the doors it meets and the triggers it
    /// is in are touched; then what is due acts, and the pushers move, pushing the player's body.
    /// Returns the player's eye, between where the last two ticks left it by the time left.
    auto play(PlayerInput& input, nx::EulerAngles view, nx::Seconds elapsed) -> nx::Vec3;

    /// Turns noclip on or off, as the original's `noclip` cheat does; returns whether it is on now.
    auto toggleNoclip() -> bool;

    /// Where the game puts its brush entities now, how high their waves rise, and which textures they
    /// show: what the scene draws them by.
    [[nodiscard]] auto placements() -> std::vector<nx::render::ModelPlacement>;

private:
    const Level* m_level;
    nx::ecs::World m_world;
    nx::ecs::Entity m_body{};
    MovementState m_player{};
    nx::Vec3 m_previousEye{};          ///< Before the last command.
    nx::Seconds m_pending{};           ///< Of the time given to the game, what no tick has taken yet.
    nx::Flags<Button> m_lastButtons{}; ///< Of the last command: the use button acts as it goes down.
};

} // namespace cs
