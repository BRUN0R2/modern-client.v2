// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console's weapon commands (docs/10-counter-strike-gameplay.md, section 7.5): `weapon_<id>` for
/// each weapon, as the original's game takes them, and the commands that make weapons as the game
/// runs, set and read their values by the key paths of their data, replace one by another and give
/// one, named as a plugin's functions are (docs/15): what a plugin will do, typed at the console.
export module cs.client:weaponCommands;

import std;
import nx.core;
import nx.console;
import cs.shared;

export namespace cs {

/// Why a weapon command could not act on the game.
enum class WeaponActionError : std::uint8_t {
    NoMap, ///< No map is played.
};

/// What the weapon commands do to the game played.
struct WeaponActions {
    /// The player draws `weapon`, when it has it. Errors: NoMap.
    std::function<nx::Result<void, WeaponActionError>(WeaponIndex weapon)> select;
    /// The player is given `weapon`, which is complete. Errors: NoMap.
    std::function<nx::Result<void, WeaponActionError>(WeaponIndex weapon)> give;
};

/// The weapon `name` names in `weapons`: by its id, or by its classname, in any case, as the console
/// takes its commands' names; none.
[[nodiscard]] auto findWeaponNamed(const WeaponCatalog& weapons, std::string_view name) noexcept
    -> std::optional<WeaponIndex>;

/// What follows the first `words` words of `argumentText`, a command's arguments as written, its
/// spaces at its ends trimmed: the value of a command whose last argument is the rest of the line.
[[nodiscard]] auto textAfterWords(std::string_view argumentText, std::size_t words) noexcept
    -> std::string_view;

/// Registers in `console` `weapon_<id>` (its classname) for each weapon of `weapons`, and
/// `create_weapon`, `set_weapon_var`, `get_weapon_var`, `list_weapons`, `replace_weapon` and `give`,
/// which act through `actions`; `weapons` and `console` must outlive the commands. A weapon made
/// registers its own `weapon_<id>`. Errors: those of nx::console::Console::registerCommand.
[[nodiscard]] auto registerWeaponCommands(nx::console::Console& console, WeaponCatalog& weapons,
                                          WeaponActions actions)
    -> nx::Result<void, nx::console::ConsoleError>;

} // namespace cs
