// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

namespace {

constexpr float kSecondsAMinute = 60.0F;

/// The kind of ammo `weapon` fires; none for one that fires none.
[[nodiscard]] auto ammoOf(WeaponIndex weapon, const WeaponCatalog& weapons) noexcept -> const AmmoType* {
    const std::optional<WeaponMagazine>& magazine = weapons.weapon(weapon).magazine;
    return magazine ? weapons.findAmmo(magazine->ammo) : nullptr;
}

/// The rounds of the ammo `weapon` fires that a player of `inventory` carries.
[[nodiscard]] auto carriedFor(WeaponIndex weapon, const Inventory& inventory,
                              const WeaponCatalog& weapons) noexcept -> std::uint32_t {
    const AmmoType* const ammo = ammoOf(weapon, weapons);
    return ammo != nullptr ? inventory.reserve(ammo->id) : 0;
}

/// The newest of a player's weapons of `slot`; none.
[[nodiscard]] auto newestIn(WeaponSlot slot, const Inventory& inventory) -> std::optional<WeaponIndex> {
    const std::vector<WeaponIndex> weapons = inventory.weaponsIn(slot);
    return weapons.empty() ? std::nullopt : std::optional{weapons.front()};
}

/// Whether `buyers` take in a player of `team`.
[[nodiscard]] constexpr auto isSoldTo(Buyers buyers, Team team) noexcept -> bool {
    switch (buyers) {
    case Buyers::Terrorists:
        return team == Team::Terrorists;
    case Buyers::CounterTerrorists:
        return team == Team::CounterTerrorists;
    case Buyers::Everyone:
    case Buyers::Count:
        break;
    }
    return true;
}

} // namespace

auto BuyingRules::soundFiles() const -> std::vector<std::string_view> {
    std::vector<std::string_view> files;
    for (const GameSound* const sound : {&sounds.weapon, &sounds.armor, &sounds.ammo}) {
        for (const std::string& file : sound->files) {
            if (!std::ranges::contains(files, std::string_view{file})) {
                files.emplace_back(file);
            }
        }
    }
    return files;
}

auto readBuyingRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BuyingRules, BuyingError> {
    auto rules = nx::data::parseToml<BuyingRules>(text, sourceName);
    if (!rules) {
        return nx::fail(BuyingError::DataInvalid, std::string{sourceName}, std::move(rules.error()));
    }
    for (const auto& [what, sound] : {std::pair{"the sound of a weapon bought", &rules->sounds.weapon},
                                      std::pair{"the sound of armor bought", &rules->sounds.armor},
                                      std::pair{"the sound of ammo bought", &rules->sounds.ammo}}) {
        if (const std::optional<std::string_view> problem = soundProblem(*sound)) {
            return nx::fail(BuyingError::DataInvalid, std::format("{}: {} {}", sourceName, what, *problem));
        }
    }
    return std::move(*rules);
}

auto readBuyTime(float minutes) noexcept -> BuyTime {
    const std::int32_t seconds = nx::truncateAsX86(minutes * kSecondsAMinute);
    if (static_cast<float>(seconds) < kLeastBuyTime.count()) {
        return BuyTime{.length = kLeastBuyTime, .setToMinutes = kLeastBuyTime.count() / kSecondsAMinute};
    }
    return BuyTime{.length = nx::Seconds{static_cast<float>(seconds)}};
}

auto checkMayBuy(const Buyer& buyer) -> nx::Result<void, BuyRefusal> {
    if (!buyer.isAlive) {
        return nx::fail(BuyRefusal::Dead);
    }
    if (!buyer.isAtBuyingPlace) {
        return nx::fail(BuyRefusal::OutsideBuyZones);
    }
    if (buyer.sinceRoundStart > buyer.buyTime) {
        return nx::fail(BuyRefusal::TimeOver);
    }
    if (!buyer.mayTeamBuy) {
        return nx::fail(BuyRefusal::TeamMayNotBuy);
    }
    return {};
}

auto weaponPriceFor(const Weapon& weapon, const Buyer& buyer, bool isOwned)
    -> nx::Result<nx::Money, BuyRefusal> {
    if (!weapon.purchase) {
        return nx::fail(BuyRefusal::NotSold);
    }
    if (!isSoldTo(weapon.purchase->buyers, buyer.team)) {
        return nx::fail(BuyRefusal::NotForItsTeam);
    }
    if (auto mayBuy = checkMayBuy(buyer); !mayBuy) {
        return std::unexpected{std::move(mayBuy.error())};
    }
    if (isOwned) {
        return nx::fail(BuyRefusal::AlreadyOwned);
    }
    if (buyer.money < weapon.purchase->price) {
        return nx::fail(BuyRefusal::NotEnoughMoney);
    }
    return weapon.purchase->price;
}

auto armorCostOf(const ArmorPrices& prices, const PlayerStatus& status, ArmorKind asked) noexcept
    -> std::optional<nx::Money> {
    const bool isWhole = status.armor >= kMostArmor;
    const bool hasHelmet = status.armorKind == ArmorKind::KevlarAndHelmet;
    if (isWhole && (asked == ArmorKind::Kevlar || hasHelmet)) {
        return std::nullopt;
    }
    return isWhole                                   ? prices.helmet
           : asked == ArmorKind::Kevlar || hasHelmet ? prices.kevlar
                                                     : prices.kevlarAndHelmet;
}

auto armorPriceFor(const ArmorPrices& prices, const Buyer& buyer, const PlayerStatus& status, ArmorKind asked)
    -> nx::Result<nx::Money, BuyRefusal> {
    nx::expects(asked != ArmorKind::None, "armorPriceFor: armor of some kind");
    if (auto mayBuy = checkMayBuy(buyer); !mayBuy) {
        return std::unexpected{std::move(mayBuy.error())};
    }
    const std::optional<nx::Money> price = armorCostOf(prices, status, asked);
    if (!price) {
        return nx::fail(BuyRefusal::ArmorWhole);
    }
    if (buyer.money < *price) {
        return nx::fail(BuyRefusal::NotEnoughMoney);
    }
    return *price;
}

auto ammoPriceFor(const AmmoType& ammo, const Buyer& buyer, std::uint32_t carried)
    -> nx::Result<nx::Money, BuyRefusal> {
    if (auto mayBuy = checkMayBuy(buyer); !mayBuy) {
        return std::unexpected{std::move(mayBuy.error())};
    }
    if (carried >= ammo.maxCarried) {
        return nx::fail(BuyRefusal::AmmoFull);
    }
    if (buyer.money < ammo.boxPrice) {
        return nx::fail(BuyRefusal::NotEnoughMoney);
    }
    return ammo.boxPrice;
}

auto lastPurchaseOf(const Inventory& inventory, const PlayerStatus& status, const WeaponCatalog& weapons)
    -> LastPurchase {
    const std::optional<WeaponIndex> primary = newestIn(WeaponSlot::Primary, inventory);
    const std::optional<WeaponIndex> pistol = newestIn(WeaponSlot::Secondary, inventory);
    return LastPurchase{.primary = primary,
                        .primaryRounds = primary ? carriedFor(*primary, inventory, weapons) : 0,
                        .pistol = pistol,
                        .pistolRounds = pistol ? carriedFor(*pistol, inventory, weapons) : 0,
                        .armor = status.armorKind};
}

auto rebuyStepsOf(const LastPurchase& last, const Inventory& inventory, const PlayerStatus& status,
                  const WeaponCatalog& weapons) -> std::vector<RebuyStep> {
    std::vector<RebuyStep> steps;
    const auto appendAmmo = [&](WeaponSlot slot, const std::optional<WeaponIndex>& gun,
                                std::uint32_t rounds) {
        if (gun && rounds > carriedFor(*gun, inventory, weapons)) {
            steps.emplace_back(AmmoRebuy{.slot = slot, .gun = *gun});
        }
    };
    const std::optional<WeaponIndex> heldPrimary = newestIn(WeaponSlot::Primary, inventory);
    if (!heldPrimary && last.primary) {
        steps.emplace_back(*last.primary);
    }
    appendAmmo(WeaponSlot::Primary, heldPrimary ? heldPrimary : last.primary, last.primaryRounds);

    const std::optional<WeaponIndex> heldPistol = newestIn(WeaponSlot::Secondary, inventory);
    if (last.pistol && !inventory.has(*last.pistol)) {
        steps.emplace_back(*last.pistol);
    }
    appendAmmo(WeaponSlot::Secondary, last.pistol ? last.pistol : heldPistol, last.pistolRounds);

    if (std::to_underlying(last.armor) > std::to_underlying(status.armorKind)) {
        steps.emplace_back(last.armor);
    }
    return steps;
}

auto rebuyCostOf(std::span<const RebuyStep> steps, const Inventory& inventory, const PlayerStatus& status,
                 const WeaponCatalog& weapons, const ArmorPrices& prices) -> nx::Money {
    nx::Money cost{};
    for (const RebuyStep& step : steps) {
        if (const auto* const weapon = std::get_if<WeaponIndex>(&step)) {
            const std::optional<WeaponPurchase>& purchase = weapons.weapon(weapons.madeFor(*weapon)).purchase;
            cost = cost + (purchase ? purchase->price : nx::Money{});
        } else if (const auto* const ammo = std::get_if<AmmoRebuy>(&step)) {
            const AmmoType* const kind = ammoOf(ammo->gun, weapons);
            const std::uint32_t carried = carriedFor(ammo->gun, inventory, weapons);
            if (kind != nullptr && carried < kind->maxCarried) {
                const std::uint32_t boxes =
                    ((kind->maxCarried - carried) + kind->boxRounds - 1) / kind->boxRounds;
                cost = cost + nx::Money{nx::narrow<std::int32_t>(boxes) * kind->boxPrice.value()};
            }
        } else if (const auto* const armor = std::get_if<ArmorKind>(&step)) {
            cost = cost + armorCostOf(prices, status, *armor).value_or(nx::Money{});
        }
    }
    return cost;
}

auto BuyingState::timeLeftAfter(nx::Seconds elapsed) const noexcept -> nx::Seconds {
    return isRunningDown ? std::clamp(timeLeft - elapsed, nx::Seconds{}, length) : timeLeft;
}

auto buyingStateOf(const Buyer& buyer, bool isFrozen) -> BuyingState {
    const auto mayBuy = checkMayBuy(buyer);
    return BuyingState{.refusal = mayBuy ? std::nullopt : std::optional{mayBuy.error().code},
                       .timeLeft = isFrozen ? buyer.buyTime
                                            : std::max(buyer.buyTime - buyer.sinceRoundStart, nx::Seconds{}),
                       .length = buyer.buyTime,
                       .isRunningDown = !isFrozen};
}

} // namespace cs
