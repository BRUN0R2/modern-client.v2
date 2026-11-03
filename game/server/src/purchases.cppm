// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a player buys, weighed by cs.shared:buying and acted on here (docs/10-counter-strike-gameplay.md,
/// section 8).
export module cs.server:purchases;

import std;
import nx.core;
import cs.shared;
import :gameState;

namespace cs {

/// `player` as the rules of buying weigh it now (Buyer).
[[nodiscard]] auto buyerOf(const GameState& game, const Player& player) -> Buyer;

/// `player` pays `price`, and is heard buying with `sound` in the next tick.
void pay(GameState& game, Player& player, nx::Money price, const GameSound& sound);

/// `player` buys a box of ammo for each of its weapons of `slot` (BuyAmmo, BuyGunAmmo). Errors: NoGun,
/// those of ammoPriceFor.
[[nodiscard]] auto buyBoxes(GameState& game, Player& player, WeaponSlot slot) -> nx::Result<void, BuyRefusal>;

/// `player` buys `weapon`: as cs::Game::buyWeapon tells.
[[nodiscard]] auto buyWeapon(GameState& game, Player& player, WeaponIndex weapon)
    -> nx::Result<void, BuyRefusal>;

/// `player` buys armor of `kind`: as cs::Game::buyArmor tells.
[[nodiscard]] auto buyArmor(GameState& game, Player& player, ArmorKind kind) -> nx::Result<void, BuyRefusal>;

/// `player` buys ammo for its weapons of `slot`: as cs::Game::buyAmmo tells.
[[nodiscard]] auto buyAmmo(GameState& game, Player& player, WeaponSlot slot, AmmoPurchase purchase)
    -> nx::Result<void, BuyRefusal>;

/// `player` buys again what it had after its last purchase: as cs::Game::rebuy tells.
[[nodiscard]] auto rebuy(GameState& game, Player& player) -> nx::Result<void, BuyRefusal>;

} // namespace cs
