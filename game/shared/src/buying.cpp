// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

namespace {

constexpr float kSecondsAMinute = 60.0F;

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

auto armorPriceFor(const ArmorPrices& prices, const Buyer& buyer, const PlayerStatus& status, ArmorKind asked)
    -> nx::Result<nx::Money, BuyRefusal> {
    nx::expects(asked != ArmorKind::None, "armorPriceFor: armor of some kind");
    if (auto mayBuy = checkMayBuy(buyer); !mayBuy) {
        return std::unexpected{std::move(mayBuy.error())};
    }
    const bool isWhole = status.armor >= kMostArmor;
    const bool hasHelmet = status.armorKind == ArmorKind::KevlarAndHelmet;
    if (isWhole && (asked == ArmorKind::Kevlar || hasHelmet)) {
        return nx::fail(BuyRefusal::ArmorWhole);
    }
    const nx::Money price = isWhole                                   ? prices.helmet
                            : asked == ArmorKind::Kevlar || hasHelmet ? prices.kevlar
                                                                      : prices.kevlarAndHelmet;
    if (buyer.money < price) {
        return nx::fail(BuyRefusal::NotEnoughMoney);
    }
    return price;
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

} // namespace cs
