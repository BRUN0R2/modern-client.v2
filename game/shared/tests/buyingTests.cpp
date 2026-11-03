// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The rules of buying, as the original's CanPlayerBuy, CanBuyThis, BuyItem and BuyGunAmmo weigh them: who
// may buy, what a weapon, armor and a box of ammo cost a buyer, how long a round's players buy for, and the
// repository's data/cstrike/buying.toml.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.game;
import nx.platform;
import cs.shared;

namespace {

using cs::ArmorKind;
using cs::Buyer;
using cs::BuyRefusal;

constexpr nx::Money kMuchMoney{16000};
constexpr nx::Seconds kBuyTime{90.0F};

/// A terrorist of much money where it may buy, right as its round starts.
[[nodiscard]] auto buyer() -> Buyer {
    return Buyer{.team = cs::Team::Terrorists,
                 .money = kMuchMoney,
                 .isAlive = true,
                 .isAtBuyingPlace = true,
                 .mayTeamBuy = true,
                 .buyTime = kBuyTime};
}

/// The AK-47's purchase: what the rules weigh of it.
[[nodiscard]] auto rifle() -> cs::Weapon {
    return cs::Weapon{.id = "ak47",
                      .purchase =
                          cs::WeaponPurchase{.buyers = cs::Buyers::Terrorists, .price = nx::Money{2500}}};
}

constexpr cs::ArmorPrices kArmorPrices{
    .kevlar = nx::Money{650}, .kevlarAndHelmet = nx::Money{1000}, .helmet = nx::Money{350}};

/// A player that wears `armor` points of armor of `kind`.
[[nodiscard]] auto wearing(float armor, ArmorKind kind) -> cs::PlayerStatus {
    return cs::PlayerStatus{.armor = nx::ArmorPoints{armor}, .armorKind = kind};
}

template <class Value>
[[nodiscard]] auto refusalOf(const nx::Result<Value, BuyRefusal>& result) -> std::optional<BuyRefusal> {
    return result ? std::nullopt : std::optional{result.error().code};
}

TEST(BuyingRulesTest, TheRepositorysAreTheOriginals) {
    const std::filesystem::path file = std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "buying.toml";
    auto text = nx::platform::readTextFile(file);
    ASSERT_TRUE(text.has_value());
    auto rules = cs::readBuyingRules(*text, "buying.toml");
    ASSERT_TRUE(rules.has_value()) << nx::describe(rules.error());
    EXPECT_EQ(rules->spawnReach, nx::HammerUnits{200.0F});
    EXPECT_EQ(rules->armor.kevlar, nx::Money{650});
    EXPECT_EQ(rules->armor.kevlarAndHelmet, nx::Money{1000});
    EXPECT_EQ(rules->armor.helmet, nx::Money{350});
    EXPECT_EQ(rules->soundFiles(),
              (std::vector<std::string_view>{"items/gunpickup2.wav", "items/ammopickup2.wav",
                                             "items/9mmclip1.wav"}));
    EXPECT_EQ(rules->sounds.weapon.channel, cs::SoundChannel::Item);
}

TEST(BuyingRulesTest, ASoundWithoutAFileIsRefused) {
    const auto rules = cs::readBuyingRules(R"(spawnReach = 200.0
armor = { kevlar = 650, kevlarAndHelmet = 1000, helmet = 350 }
[sounds]
weapon = { files = [], channel = "item", volume = 1.0 }
armor = { files = ["a.wav"], channel = "item", volume = 1.0 }
ammo = { files = ["a.wav"], channel = "item", volume = 1.0 }
)",
                                           "buying.toml");
    ASSERT_FALSE(rules.has_value());
    EXPECT_EQ(rules.error().code, cs::BuyingError::DataInvalid);
}

TEST(BuyTimeTest, ItsMinutesAreCutToWholeSecondsAndHeldToTheLeast) {
    EXPECT_EQ(cs::readBuyTime(1.5F).length, nx::Seconds{90.0F});
    EXPECT_EQ(cs::readBuyTime(1.5F).setToMinutes, std::nullopt);
    EXPECT_EQ(cs::readBuyTime(0.51F).length, nx::Seconds{30.0F}) << "30.6 seconds, cut";
    EXPECT_EQ(cs::readBuyTime(0.25F).length, cs::kLeastBuyTime);
    EXPECT_EQ(cs::readBuyTime(0.25F).setToMinutes, std::nullopt) << "at the least, it is not held";
    EXPECT_EQ(cs::readBuyTime(0.0F).length, cs::kLeastBuyTime);
    EXPECT_EQ(cs::readBuyTime(0.0F).setToMinutes, 0.25F);
}

TEST(MayBuyTest, ALivingPlayerWhereItsTeamBuysInTheTimeOfItsRoundMayBuy) {
    EXPECT_TRUE(cs::checkMayBuy(buyer()).has_value());

    Buyer dead = buyer();
    dead.isAlive = false;
    dead.isAtBuyingPlace = false;
    EXPECT_EQ(refusalOf(cs::checkMayBuy(dead)), BuyRefusal::Dead) << "before all else";

    Buyer outside = buyer();
    outside.isAtBuyingPlace = false;
    outside.sinceRoundStart = kBuyTime * 2.0F;
    EXPECT_EQ(refusalOf(cs::checkMayBuy(outside)), BuyRefusal::OutsideBuyZones) << "before the time";

    Buyer late = buyer();
    late.sinceRoundStart = kBuyTime;
    EXPECT_TRUE(cs::checkMayBuy(late).has_value()) << "only past it";
    late.sinceRoundStart = nx::Seconds{90.5F};
    late.mayTeamBuy = false;
    EXPECT_EQ(refusalOf(cs::checkMayBuy(late)), BuyRefusal::TimeOver) << "before the map's rule";

    Buyer forbidden = buyer();
    forbidden.mayTeamBuy = false;
    EXPECT_EQ(refusalOf(cs::checkMayBuy(forbidden)), BuyRefusal::TeamMayNotBuy);
}

TEST(WeaponPriceTest, AWeaponCostsItsPriceToABuyerItIsSoldToThatLacksIt) {
    EXPECT_EQ(cs::weaponPriceFor(rifle(), buyer(), false).value_or(nx::Money{}), nx::Money{2500});

    Buyer exact = buyer();
    exact.money = nx::Money{2500};
    EXPECT_TRUE(cs::weaponPriceFor(rifle(), exact, false).has_value());
    exact.money = nx::Money{2499};
    EXPECT_EQ(refusalOf(cs::weaponPriceFor(rifle(), exact, false)), BuyRefusal::NotEnoughMoney);

    EXPECT_EQ(refusalOf(cs::weaponPriceFor(rifle(), buyer(), true)), BuyRefusal::AlreadyOwned);
    EXPECT_EQ(refusalOf(cs::weaponPriceFor(cs::Weapon{.id = "knife"}, buyer(), false)), BuyRefusal::NotSold);
}

TEST(WeaponPriceTest, WhetherItsTeamIsSoldItIsWeighedBeforeWhetherItMayBuy) {
    Buyer other = buyer();
    other.team = cs::Team::CounterTerrorists;
    other.isAtBuyingPlace = false;
    EXPECT_EQ(refusalOf(cs::weaponPriceFor(rifle(), other, false)), BuyRefusal::NotForItsTeam);

    Buyer outside = buyer();
    outside.isAtBuyingPlace = false;
    EXPECT_EQ(refusalOf(cs::weaponPriceFor(rifle(), outside, true)), BuyRefusal::OutsideBuyZones);

    cs::Weapon forEveryone = rifle();
    forEveryone.purchase->buyers = cs::Buyers::Everyone;
    other.isAtBuyingPlace = true;
    EXPECT_TRUE(cs::weaponPriceFor(forEveryone, other, false).has_value());
}

TEST(ArmorPriceTest, EachArmorCostsWhatItLacks) {
    struct Case {
        float armor;
        ArmorKind worn;
        ArmorKind asked;
        std::optional<std::int32_t> price;
        std::string_view what;
    };
    for (const Case& each : {
             Case{0.0F, ArmorKind::None, ArmorKind::Kevlar, 650, "a vest"},
             Case{40.0F, ArmorKind::Kevlar, ArmorKind::Kevlar, 650, "a vest over one hit"},
             Case{40.0F, ArmorKind::KevlarAndHelmet, ArmorKind::Kevlar, 650, "a vest under a helmet"},
             Case{100.0F, ArmorKind::Kevlar, ArmorKind::Kevlar, std::nullopt, "a whole vest"},
             Case{100.0F, ArmorKind::KevlarAndHelmet, ArmorKind::Kevlar, std::nullopt, "and a helmet"},
             Case{0.0F, ArmorKind::None, ArmorKind::KevlarAndHelmet, 1000, "both"},
             Case{40.0F, ArmorKind::Kevlar, ArmorKind::KevlarAndHelmet, 1000, "both over a vest hit"},
             Case{100.0F, ArmorKind::Kevlar, ArmorKind::KevlarAndHelmet, 350, "a helmet alone"},
             Case{40.0F, ArmorKind::KevlarAndHelmet, ArmorKind::KevlarAndHelmet, 650, "the vest alone"},
             Case{100.0F, ArmorKind::KevlarAndHelmet, ArmorKind::KevlarAndHelmet, std::nullopt, "both whole"},
         }) {
        const auto price =
            cs::armorPriceFor(kArmorPrices, buyer(), wearing(each.armor, each.worn), each.asked);
        if (each.price) {
            EXPECT_EQ(price.value_or(nx::Money{}), nx::Money{*each.price}) << each.what;
        } else {
            EXPECT_EQ(refusalOf(price), BuyRefusal::ArmorWhole) << each.what;
        }
    }
}

TEST(ArmorPriceTest, ABuyerPaysForItAndMayBuyAtAll) {
    Buyer poor = buyer();
    poor.money = nx::Money{649};
    EXPECT_EQ(refusalOf(cs::armorPriceFor(kArmorPrices, poor, {}, ArmorKind::Kevlar)),
              BuyRefusal::NotEnoughMoney);
    Buyer dead = buyer();
    dead.isAlive = false;
    EXPECT_EQ(refusalOf(cs::armorPriceFor(kArmorPrices, dead, {}, ArmorKind::Kevlar)), BuyRefusal::Dead);
}

TEST(ArmorCostTest, WhatIsWornWholeHasNoCost) {
    const cs::ArmorPrices prices{
        .kevlar = nx::Money{650}, .kevlarAndHelmet = nx::Money{1000}, .helmet = nx::Money{350}};
    const auto costOf = [&prices](nx::ArmorPoints armor, ArmorKind worn, ArmorKind asked) {
        return cs::armorCostOf(prices, cs::PlayerStatus{.armor = armor, .armorKind = worn}, asked);
    };
    EXPECT_EQ(costOf({}, ArmorKind::None, ArmorKind::Kevlar), nx::Money{650});
    EXPECT_EQ(costOf({}, ArmorKind::None, ArmorKind::KevlarAndHelmet), nx::Money{1000});
    EXPECT_EQ(costOf(cs::kMostArmor, ArmorKind::Kevlar, ArmorKind::Kevlar), std::nullopt);
    EXPECT_EQ(costOf(cs::kMostArmor, ArmorKind::Kevlar, ArmorKind::KevlarAndHelmet), nx::Money{350});
    EXPECT_EQ(costOf(cs::kMostArmor, ArmorKind::KevlarAndHelmet, ArmorKind::KevlarAndHelmet), std::nullopt);
    EXPECT_EQ(costOf(nx::ArmorPoints{50.0F}, ArmorKind::KevlarAndHelmet, ArmorKind::KevlarAndHelmet),
              nx::Money{650})
        << "the vest under a helmet kept";
}

/// A catalog of a pistol and a rifle, with what a player given them has.
class RebuyTest : public ::testing::Test {
protected:
    RebuyTest() : weapons{catalog()}, inventory{weapons} {}

    [[nodiscard]] static auto gun(std::string id, cs::WeaponSlot slot, std::string ammo, std::int32_t price)
        -> cs::Weapon {
        return cs::Weapon{
            .id = id,
            .classname = "weapon_" + id,
            .name = id,
            .slot = slot,
            .position = 1,
            .purchase = cs::WeaponPurchase{.buyers = cs::Buyers::Everyone, .price = nx::Money{price}},
            .magazine = cs::WeaponMagazine{.ammo = std::move(ammo), .clipSize = 30},
            .models = cs::WeaponModels{.view = "models/v_gun.mdl",
                                       .player = "models/p_gun.mdl",
                                       .playerAnimation = "gun"},
            .view = cs::WeaponView{
                .drawSequences = {1},
                .idles = {cs::WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                .idlesWhenEmpty = true}};
    }
    [[nodiscard]] static auto catalog() -> cs::WeaponCatalog {
        cs::AmmoTypes ammo{
            .ammo = {
                cs::AmmoType{.id = "9mm", .maxCarried = 120, .boxPrice = nx::Money{20}, .boxRounds = 30},
                cs::AmmoType{.id = "762Nato", .maxCarried = 90, .boxPrice = nx::Money{80}, .boxRounds = 30}}};
        auto made =
            cs::WeaponCatalog::make(std::move(ammo), {gun("glock", cs::WeaponSlot::Secondary, "9mm", 400),
                                                      gun("ak", cs::WeaponSlot::Primary, "762Nato", 2500)});
        EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
        return std::move(made).value();
    }
    void give(cs::WeaponIndex weapon) {
        nx::Random random{0, "test"};
        std::ignore = inventory.give(weapon, cs::AutoSwitch::Off, nx::game::TickTime{}, random);
    }

    static constexpr cs::WeaponIndex kGlock{0};
    static constexpr cs::WeaponIndex kAk{1};
    cs::WeaponCatalog weapons;
    cs::Inventory inventory;
    cs::PlayerStatus status{};
    cs::ArmorPrices prices{
        .kevlar = nx::Money{650}, .kevlarAndHelmet = nx::Money{1000}, .helmet = nx::Money{350}};
};

TEST_F(RebuyTest, APurchaseLeavesWhatThePlayerHasToBuyAgain) {
    give(kGlock);
    give(kAk);
    std::ignore = inventory.giveAmmo("762Nato", 60);
    std::ignore = inventory.giveAmmo("9mm", 40);
    status.armorKind = ArmorKind::KevlarAndHelmet;
    EXPECT_EQ(cs::lastPurchaseOf(inventory, status, weapons),
              (cs::LastPurchase{.primary = kAk,
                                .primaryRounds = 60,
                                .pistol = kGlock,
                                .pistolRounds = 40,
                                .armor = ArmorKind::KevlarAndHelmet}));
}

TEST_F(RebuyTest, ItBuysInTheOrderOfClRebuysDefaultWhatThePlayerLacksAndItsCostAddsUp) {
    const cs::LastPurchase last{.primary = kAk,
                                .primaryRounds = 90,
                                .pistol = kGlock,
                                .pistolRounds = 0,
                                .armor = ArmorKind::KevlarAndHelmet};
    give(kGlock); // as it spawns
    const std::vector<cs::RebuyStep> steps = cs::rebuyStepsOf(last, inventory, status, weapons);
    EXPECT_EQ(steps,
              (std::vector<cs::RebuyStep>{kAk, cs::AmmoRebuy{.slot = cs::WeaponSlot::Primary, .gun = kAk},
                                          ArmorKind::KevlarAndHelmet}));
    EXPECT_EQ(cs::rebuyCostOf(steps, inventory, status, weapons, prices), nx::Money{2500 + (3 * 80) + 1000});
}

TEST_F(RebuyTest, ItBuysNothingOfWhatThePlayerStillHas) {
    give(kGlock);
    give(kAk);
    std::ignore = inventory.giveAmmo("762Nato", 90);
    status.armorKind = ArmorKind::Kevlar;
    const cs::LastPurchase last{
        .primary = kAk, .primaryRounds = 60, .pistol = kGlock, .armor = ArmorKind::Kevlar};
    EXPECT_TRUE(cs::rebuyStepsOf(last, inventory, status, weapons).empty());
}

TEST_F(RebuyTest, APrimaryWeaponItHasStaysButThePistolIsBoughtInItsPlace) {
    give(kAk);
    const cs::LastPurchase noPrimary{.pistol = kGlock};
    EXPECT_EQ(cs::rebuyStepsOf(noPrimary, inventory, status, weapons), (std::vector<cs::RebuyStep>{kGlock}));
    EXPECT_TRUE(cs::rebuyStepsOf(cs::LastPurchase{}, inventory, status, weapons).empty());
}

TEST_F(RebuyTest, ArmorIsBoughtWhenThePlayerWearsLessOfIt) {
    status.armorKind = ArmorKind::KevlarAndHelmet;
    EXPECT_TRUE(
        cs::rebuyStepsOf(cs::LastPurchase{.armor = ArmorKind::Kevlar}, inventory, status, weapons).empty());
    status.armorKind = ArmorKind::Kevlar;
    EXPECT_EQ(
        cs::rebuyStepsOf(cs::LastPurchase{.armor = ArmorKind::KevlarAndHelmet}, inventory, status, weapons),
        (std::vector<cs::RebuyStep>{ArmorKind::KevlarAndHelmet}));
}

TEST(BuyingStateTest, APlayerIsToldWhetherItMayBuyAndForHowLong) {
    Buyer asked = buyer();
    asked.sinceRoundStart = nx::Seconds{30.0F};
    const cs::BuyingState running = cs::buyingStateOf(asked, false);
    EXPECT_FALSE(running.refusal.has_value());
    EXPECT_EQ(running.timeLeft, nx::Seconds{60.0F});
    EXPECT_EQ(running.length, kBuyTime);

    asked.isAtBuyingPlace = false;
    EXPECT_EQ(cs::buyingStateOf(asked, false).refusal, BuyRefusal::OutsideBuyZones);
}

TEST(BuyingStateTest, ItsTimeDoesNotRunDownInAFreezeWhichStartsItAgain) {
    Buyer asked = buyer();
    asked.sinceRoundStart = nx::Seconds{3.0F};
    EXPECT_EQ(cs::buyingStateOf(asked, true).timeLeft, kBuyTime);
}

TEST(BuyingStateTest, ItsTimeOverIsNoneLeftAndARefusal) {
    Buyer asked = buyer();
    asked.sinceRoundStart = kBuyTime + nx::Seconds{5.0F};
    const cs::BuyingState over = cs::buyingStateOf(asked, false);
    EXPECT_EQ(over.refusal, BuyRefusal::TimeOver);
    EXPECT_EQ(over.timeLeft, nx::Seconds{});
}

TEST(BuyMenuLookTest, TheRepositorysReads) {
    const auto text =
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "buyMenu.toml");
    ASSERT_TRUE(text.has_value());
    const auto look = cs::readBuyMenuLook(*text, "buyMenu.toml");
    ASSERT_TRUE(look.has_value()) << (look ? "" : nx::describe(look.error()));
    EXPECT_FALSE(look->fonts.empty());
    ASSERT_EQ(look->armor.size(), 2U);
    EXPECT_EQ(look->armor[0].kind, ArmorKind::Kevlar);
    EXPECT_EQ(look->armor[0].key, 1U) << "as the original's menu numbers it";
    EXPECT_EQ(look->armor[1].kind, ArmorKind::KevlarAndHelmet);
    EXPECT_EQ(look->armor[1].key, 2U);
}

TEST(BuyMenuLookTest, WhatTheMenuCouldNotShowIsRefused) {
    const auto refused = [](std::string_view text) {
        const auto look = cs::readBuyMenuLook(text, "test");
        return !look.has_value() && look.error().code == cs::BuyMenuLookError::DataInvalid;
    };
    constexpr std::string_view kVest =
        "[[armor]]\nkind = \"kevlar\"\nkey = 1\nname = \"Vest\"\npicture = \"a.tga\"\nabout = \"\"\n";
    const std::string fonts = "fonts = [\"Verdana\"]\n";
    EXPECT_FALSE(refused(fonts + std::string{kVest}));
    EXPECT_TRUE(refused(std::string{"fonts = []\n"} + std::string{kVest})) << "no font";
    EXPECT_TRUE(refused(fonts + std::string{kVest} + std::string{kVest})) << "one kind twice";
    EXPECT_TRUE(refused(
        fonts + "[[armor]]\nkind = \"none\"\nkey = 1\nname = \"Vest\"\npicture = \"a.tga\"\nabout = \"\"\n"))
        << "armor of no kind";
    EXPECT_TRUE(refused(
        fonts +
        "[[armor]]\nkind = \"kevlar\"\nkey = 10\nname = \"Vest\"\npicture = \"a.tga\"\nabout = \"\"\n"))
        << "a key beyond the keys";
    EXPECT_TRUE(refused(
        fonts + "[[armor]]\nkind = \"kevlar\"\nkey = 1\nname = \"\"\npicture = \"a.tga\"\nabout = \"\"\n"))
        << "no name";
}

TEST(AmmoPriceTest, ABoxCostsItsPriceUntilTheBuyerCarriesAllItCan) {
    const cs::AmmoType ammo{.id = "9mm", .maxCarried = 120, .boxPrice = nx::Money{20}, .boxRounds = 30};
    EXPECT_EQ(cs::ammoPriceFor(ammo, buyer(), 119).value_or(nx::Money{}), nx::Money{20});
    EXPECT_EQ(refusalOf(cs::ammoPriceFor(ammo, buyer(), 120)), BuyRefusal::AmmoFull);
    Buyer poor = buyer();
    poor.money = nx::Money{19};
    EXPECT_EQ(refusalOf(cs::ammoPriceFor(ammo, poor, 0)), BuyRefusal::NotEnoughMoney);
    poor.isAtBuyingPlace = false;
    EXPECT_EQ(refusalOf(cs::ammoPriceFor(ammo, poor, 0)), BuyRefusal::OutsideBuyZones);
}

} // namespace
