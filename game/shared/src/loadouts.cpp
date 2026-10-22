// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

auto Loadouts::find(Team team) const noexcept -> const Loadout* {
    const auto found = std::ranges::find(loadouts, team, &Loadout::team);
    return found == loadouts.end() ? nullptr : &*found;
}

auto readLoadouts(std::string_view text, std::string_view sourceName, const WeaponCatalog& catalog)
    -> nx::Result<Loadouts, WeaponError> {
    auto loadouts = nx::data::parseToml<Loadouts>(text, sourceName);
    if (!loadouts) {
        return nx::fail(WeaponError::DataInvalid, std::string{sourceName}, std::move(loadouts.error()));
    }
    std::set<Team> teams;
    for (const Loadout& loadout : loadouts->loadouts) {
        const std::string_view team = nx::enumName(loadout.team);
        if (!teams.insert(loadout.team).second) {
            return nx::fail(WeaponError::DataInvalid,
                            std::format("{}: the loadout of {} twice", sourceName, team));
        }
        for (const std::string& weapon : loadout.weapons) {
            if (!catalog.indexOf(weapon)) {
                return nx::fail(
                    WeaponError::DataInvalid,
                    std::format("{}: the {} spawn with {:?}, not a weapon", sourceName, team, weapon));
            }
        }
        for (const LoadoutAmmo& ammo : loadout.ammo) {
            if (catalog.findAmmo(ammo.id) == nullptr) {
                return nx::fail(
                    WeaponError::DataInvalid,
                    std::format("{}: the {} spawn with {:?}, not a kind of ammo", sourceName, team, ammo.id));
            }
        }
    }
    return std::move(*loadouts);
}

} // namespace cs
