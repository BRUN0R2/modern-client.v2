// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;
import cs.shared;

namespace cs {

namespace {

using nx::console::CommandLine;
using nx::console::Console;
using nx::console::ConsoleError;

/// What each refusal tells the player, in BuyRefusal's order: the project's own words.
constexpr nx::EnumArray<BuyRefusal, std::string_view> kRefusalTexts{
    nx::EnumArray<BuyRefusal, std::string_view>::Storage{
        "No map is being played.", "That is not for sale.", "Your team cannot buy that.", "You are dead.",
        "You are not in a buy zone.", "The time to buy is over.",
        "Your team cannot buy anything on this map.", "You already own that weapon.",
        "You already wear that armor, whole.", "You have no weapon of that kind that takes ammo.",
        "You cannot carry more of that ammo.", "You do not have enough money."}};

/// A command that buys armor.
struct ArmorCommand {
    std::string_view name;
    std::string_view description;
    ArmorKind kind;
};
constexpr std::array kArmorCommands{
    ArmorCommand{.name = "vest", .description = "Buys a vest of kevlar.", .kind = ArmorKind::Kevlar},
    ArmorCommand{.name = "vesthelm",
                 .description = "Buys a vest of kevlar and a helmet.",
                 .kind = ArmorKind::KevlarAndHelmet}};

/// A command that buys ammo.
struct AmmoCommand {
    std::string_view name;
    std::string_view description;
    WeaponSlot slot;
    AmmoPurchase purchase;
};
constexpr std::array kAmmoCommands{
    AmmoCommand{.name = "buyammo1",
                .description = "Buys a box of ammo for the primary weapon.",
                .slot = WeaponSlot::Primary,
                .purchase = AmmoPurchase::OneBox},
    AmmoCommand{.name = "buyammo2",
                .description = "Buys a box of ammo for the pistol.",
                .slot = WeaponSlot::Secondary,
                .purchase = AmmoPurchase::OneBox},
    AmmoCommand{.name = "primammo",
                .description = "Buys as much ammo for the primary weapon as can be carried and paid for.",
                .slot = WeaponSlot::Primary,
                .purchase = AmmoPurchase::AsManyBoxes},
    AmmoCommand{.name = "secammo",
                .description = "Buys as much ammo for the pistol as can be carried and paid for.",
                .slot = WeaponSlot::Secondary,
                .purchase = AmmoPurchase::AsManyBoxes}};

/// Prints what `bought` was refused for, when it was: a refusal is the game's answer, no failure of the
/// command.
void report(Console& console, const nx::Result<void, BuyRefusal>& bought) {
    if (!bought) {
        console.print(buyRefusalText(bought.error().code));
    }
}

} // namespace

auto buyRefusalText(BuyRefusal refusal) noexcept -> std::string_view {
    return kRefusalTexts[refusal];
}

auto registerBuyCommands(Console& console, const WeaponCatalog& weapons, BuyActions actions)
    -> nx::Result<void, ConsoleError> {
    for (std::uint32_t index = 0; index < weapons.weapons().size(); ++index) {
        const Weapon& weapon = weapons.weapon(WeaponIndex{index});
        if (!weapon.purchase) {
            continue;
        }
        for (const std::string& command : weapon.purchase->commands.value_or(std::vector<std::string>{})) {
            if (auto registered = console.registerCommand(
                    {.name = std::string_view{command}, .description = "Buys the weapon."},
                    [buy = actions.weapon, index](Console& on,
                                                  const CommandLine&) -> nx::Result<void, ConsoleError> {
                        report(on, buy(WeaponIndex{index}));
                        return {};
                    });
                !registered) {
                return registered;
            }
        }
    }
    for (const ArmorCommand& command : kArmorCommands) {
        if (auto registered = console.registerCommand(
                {.name = command.name, .description = command.description},
                [buy = actions.armor,
                 kind = command.kind](Console& on, const CommandLine&) -> nx::Result<void, ConsoleError> {
                    report(on, buy(kind));
                    return {};
                });
            !registered) {
            return registered;
        }
    }
    for (const AmmoCommand& command : kAmmoCommands) {
        if (auto registered = console.registerCommand(
                {.name = command.name, .description = command.description},
                [buy = actions.ammo, slot = command.slot, purchase = command.purchase](
                    Console& on, const CommandLine&) -> nx::Result<void, ConsoleError> {
                    report(on, buy(slot, purchase));
                    return {};
                });
            !registered) {
            return registered;
        }
    }
    return console.registerCommand(
        {.name = "buy_weapon",
         .description = "Buys a weapon, one made as the game runs too: buy_weapon <weapon>."},
        [buy = std::move(actions.weapon),
         catalog = &weapons](Console& on, const CommandLine& line) -> nx::Result<void, ConsoleError> {
            const std::span<const std::string> words = line.arguments();
            if (words.size() != 1) {
                return nx::fail(ConsoleError::InvalidArguments, "usage: buy_weapon <weapon>");
            }
            const std::optional<WeaponIndex> weapon = findWeaponNamed(*catalog, words[0]);
            if (!weapon) {
                return nx::fail(ConsoleError::InvalidArguments, std::format("no weapon {:?}", words[0]));
            }
            if (auto complete = catalog->checkComplete(catalog->madeFor(*weapon)); !complete) {
                return nx::fail(ConsoleError::CommandFailed, "buy_weapon", std::move(complete.error()));
            }
            report(on, buy(*weapon));
            return {};
        });
}

} // namespace cs
