// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console's buy commands (docs/10-counter-strike-gameplay.md, section 8), by the original's names
/// (HandleBuyAliasCommands, ClientCommand): those a weapon's file gives it (`ak47`, `cv47`), `vest`,
/// `vesthelm`, `buyammo1`, `buyammo2`, `primammo`, `secammo` and `rebuy`; and `buy_weapon`, named as a
/// plugin's functions are (docs/15), which buys any weapon, one made as the game runs too. The game weighs
/// each (cs::Game), and a refusal is printed and told on the screen. The buy menu's commands (`buy`,
/// `buyequip`, `slot1` to `slot10`, `cancelselect`) are here too, its state and what it shows in
/// cs.client:buyMenu.
export module cs.client:buyCommands;

import std;
import nx.core;
import nx.console;
import cs.shared;

export namespace cs {

/// What the buy commands do to the game played. Errors: NoMap, and the game's.
struct BuyActions {
    /// The player buys `weapon`, which is complete.
    std::function<nx::Result<void, BuyRefusal>(WeaponIndex weapon)> weapon;
    /// The player buys armor of `kind`.
    std::function<nx::Result<void, BuyRefusal>(ArmorKind kind)> armor;
    /// The player buys ammo for its weapons of `slot`.
    std::function<nx::Result<void, BuyRefusal>(WeaponSlot slot, AmmoPurchase purchase)> ammo;
    /// The player buys again what it had after its last purchase.
    std::function<nx::Result<void, BuyRefusal>()> rebuy;
    /// Tells the player on the screen of what was refused.
    std::function<void(BuyRefusal refusal)> refused;
};

/// What the buy menu's commands do to it. Errors: why the player may not buy, the menu staying closed.
struct BuyMenuCommands {
    /// `buy`: opens the menu, or closes it.
    std::function<nx::Result<void, BuyRefusal>()> toggle;
    /// `buyequip`: opens the menu on its equipment tab.
    std::function<nx::Result<void, BuyRefusal>()> openEquipment;
    /// `slot1` to `slot10`: the key of that number on the menu, when it is open.
    std::function<void(std::uint32_t slot)> pressSlot;
    /// `cancelselect`: back a level on the menu, when it is open.
    std::function<void()> cancel;
    /// Tells the player on the screen of what was refused.
    std::function<void(BuyRefusal refusal)> refused;
};

/// What the player is told of `refusal`.
[[nodiscard]] auto buyRefusalText(BuyRefusal refusal) noexcept -> std::string_view;

/// Registers in `console` the buy commands of `weapons`, which act through `actions`: a weapon's own, as
/// its file names them, of those in `weapons` now; `weapons` must outlive them. Errors: those of
/// nx::console::Console::registerCommand.
[[nodiscard]] auto registerBuyCommands(nx::console::Console& console, const WeaponCatalog& weapons,
                                       BuyActions actions) -> nx::Result<void, nx::console::ConsoleError>;

/// Registers in `console` the buy menu's commands, which act through `commands`. Errors: those of
/// nx::console::Console::registerCommand.
[[nodiscard]] auto registerBuyMenuCommands(nx::console::Console& console, BuyMenuCommands commands)
    -> nx::Result<void, nx::console::ConsoleError>;

} // namespace cs
