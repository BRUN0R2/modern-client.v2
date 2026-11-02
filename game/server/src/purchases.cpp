// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.game;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers a purchase's weapon is drawn by.
constexpr std::string_view kPurchaseStream = "purchase";

} // namespace

auto buyerOf(const GameState& game, const Player& player) -> Buyer {
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    return Buyer{.team = player.team,
                 .money = player.status.money,
                 .isAlive = player.status.isAlive(),
                 .isAtBuyingPlace =
                     game.map.isBuyingPlace(player.team, bodyOf(player.movement), player.movement.origin,
                                            game.content.buying.spawnReach),
                 .mayTeamBuy = game.map.letsBuy(player.team),
                 .sinceRoundStart = game.rounds.sinceStartAt(time.now, time.rate),
                 .buyTime = game.cvars.buyingTime()};
}

void pay(GameState& game, Player& player, nx::Money price, const GameSound& sound) {
    player.status.money = addMoney(player.status.money, nx::Money{} - price);
    player.soundsOfPurchases.push_back(
        SoundMade{.sound = &sound, .at = nx::game::tickTimeOf(game.world).now, .volume = sound.volume});
}

auto buyBoxes(GameState& game, Player& player, WeaponSlot slot) -> nx::Result<void, BuyRefusal> {
    if (auto mayBuy = checkMayBuy(buyerOf(game, player)); !mayBuy) {
        return mayBuy;
    }
    const std::vector<WeaponIndex> weapons = player.inventory.weaponsIn(slot);
    if (weapons.empty()) {
        return nx::fail(BuyRefusal::NoGun);
    }
    for (const WeaponIndex weapon : weapons) {
        const std::optional<WeaponMagazine>& magazine = game.content.weapons.weapon(weapon).magazine;
        const AmmoType* const ammo = magazine ? game.content.weapons.findAmmo(magazine->ammo) : nullptr;
        if (ammo == nullptr) {
            return nx::fail(BuyRefusal::NoGun);
        }
        auto price = ammoPriceFor(*ammo, buyerOf(game, player), player.inventory.reserve(ammo->id));
        if (!price) {
            return std::unexpected{std::move(price.error())};
        }
        std::ignore = player.inventory.giveAmmo(ammo->id, ammo->boxRounds);
        pay(game, player, *price, game.content.buying.sounds.ammo);
    }
    return {};
}

auto buyWeapon(GameState& game, Player& player, WeaponIndex asked) -> nx::Result<void, BuyRefusal> {
    const WeaponIndex weapon = game.content.weapons.madeFor(asked);
    const Weapon& sold = game.content.weapons.weapon(weapon);
    auto price = weaponPriceFor(sold, buyerOf(game, player), player.inventory.has(weapon));
    if (!price) {
        return std::unexpected{std::move(price.error())};
    }
    const nx::game::TickTime& time = nx::game::tickTimeOf(game.world);
    nx::Random random{std::bit_cast<std::uint64_t>(time.now.tick()), kPurchaseStream};
    for (const WeaponIndex owned : player.inventory.weaponsIn(sold.slot)) {
        player.inventory.drop(owned, time, random);
    }
    giveWeapon(game, player, weapon);
    pay(game, player, *price, game.content.buying.sounds.weapon);
    return {};
}

auto buyArmor(GameState& game, Player& player, ArmorKind kind) -> nx::Result<void, BuyRefusal> {
    auto price = armorPriceFor(game.content.buying.armor, buyerOf(game, player), player.status, kind);
    if (!price) {
        return std::unexpected{std::move(price.error())};
    }
    wearArmor(player.status, kind);
    pay(game, player, *price, game.content.buying.sounds.armor);
    return {};
}

auto buyAmmo(GameState& game, Player& player, WeaponSlot slot, AmmoPurchase purchase)
    -> nx::Result<void, BuyRefusal> {
    if (auto bought = buyBoxes(game, player, slot); !bought) {
        return bought;
    }
    if (purchase == AmmoPurchase::AsManyBoxes) {
        // Each box adds a round at least, or is refused as the ammo is full.
        while (buyBoxes(game, player, slot).has_value()) {
        }
    }
    return {};
}

} // namespace cs
