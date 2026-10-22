// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a player has besides where it is and how it moves (docs/10-counter-strike-gameplay.md): its
/// health, its armor and its money, in the domain's types from the game to the HUD
/// (docs/12-ui-hud-and-console.md, section 3.4), as the game gives them to a player that spawns
/// (ReGameDLL_CS's CBasePlayer::Spawn and mp_startmoney, GPL, consulted: docs/20 section 3).
///
/// Today: what a player has as it first spawns; damage, buying and rewards come with the game's rules
/// (TODO.md).
export module cs.shared:player;

import nx.core;

export namespace cs {

/// The health a player spawns with.
inline constexpr nx::HitPoints kSpawnHealth{100.0F};
/// The money a player starts with: mp_startmoney's default.
inline constexpr nx::Money kStartMoney{800};

/// The teams a player plays in.
enum class Team : std::uint8_t {
    Terrorists,
    CounterTerrorists,
    Count,
};

/// What a player has: what its HUD shows of it.
struct PlayerStatus {
    nx::HitPoints health{kSpawnHealth};
    nx::ArmorPoints armor{};
    nx::Money money{kStartMoney};
};

} // namespace cs
