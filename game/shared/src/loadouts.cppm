// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a player of each team has as it spawns (docs/10-counter-strike-gameplay.md, section 7.1):
/// data/cstrike/loadouts.toml, with ReGameDLL_CS's values (CBasePlayer::GiveDefaultItems, GPL,
/// consulted: docs/20 section 3).
///
/// Today: the weapons and the ammo; the VIP's, armor and what the map's equipment gives come with
/// the game's rules (TODO.md).
export module cs.shared:loadouts;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :player;
import :weapons;

export namespace cs {

/// Rounds of a kind of ammo a player spawns with, besides its weapons' clips.
struct LoadoutAmmo {
    std::string id{}; ///< A kind of ammo of the catalog.
    std::uint32_t rounds{};
};

/// What a player of a team has as it spawns.
struct Loadout {
    Team team{};
    std::vector<std::string> weapons{}; ///< The ids of its weapons, in the order it is given them.
    std::vector<LoadoutAmmo> ammo{};
};

/// data/cstrike/loadouts.toml: a loadout for each team at most.
struct Loadouts {
    std::vector<Loadout> loadouts{};

    /// The loadout of `team`; none when it has none.
    [[nodiscard]] auto find(Team team) const noexcept -> const Loadout*;
};

/// Reads `text`, the file `sourceName`: a loadout for each team at most, of the weapons and the ammo
/// of `catalog`. Errors: DataInvalid.
[[nodiscard]] auto readLoadouts(std::string_view text, std::string_view sourceName,
                                const WeaponCatalog& catalog) -> nx::Result<Loadouts, WeaponError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::LoadoutAmmo> {
    static constexpr std::string_view kName = "cs::LoadoutAmmo";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::LoadoutAmmo::id>("id"),
        nx::meta::field<&cs::LoadoutAmmo::rounds>("rounds"),
    };
};

template <>
struct nx::meta::Description<cs::Loadout> {
    static constexpr std::string_view kName = "cs::Loadout";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::Loadout::team>("team"),
        nx::meta::field<&cs::Loadout::weapons>("weapons"),
        nx::meta::field<&cs::Loadout::ammo>("ammo"),
    };
};

template <>
struct nx::meta::Description<cs::Loadouts> {
    static constexpr std::string_view kName = "cs::Loadouts";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::Loadouts::loadouts>("loadouts"),
    };
};
