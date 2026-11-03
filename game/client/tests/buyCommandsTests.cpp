// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The console's buy commands (docs/10, section 8): a weapon's own, as its file names them, in any case, and
// `buy_weapon` for any; the armor's and the ammo's by the original's names; a refusal printed, no failure of
// the command.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import cs.shared;
import cs.client;

namespace {

using cs::WeaponIndex;
using nx::console::ConsoleError;

constexpr WeaponIndex kKnife{0};
constexpr WeaponIndex kAk{1};

/// A console's output, kept.
class RecordingOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view line) override { lines.emplace_back(line); }
    void reportFailure(const nx::Failure<ConsoleError>& failure) override {
        failures.push_back(failure.code);
    }

    std::vector<std::string> lines;
    std::vector<ConsoleError> failures;
};

/// No files.
class NoFiles final : public nx::console::ConfigFiles {
public:
    [[nodiscard]] auto read(std::string_view path)
        -> nx::Result<std::string, nx::console::ConfigFileError> override {
        return nx::fail(nx::console::ConfigFileError::NotFound, std::string{path});
    }
};

[[nodiscard]] auto weaponOf(std::string id, cs::WeaponSlot slot, std::optional<cs::WeaponPurchase> purchase,
                            std::optional<cs::WeaponMagazine> magazine) -> cs::Weapon {
    return cs::Weapon{.id = id,
                      .classname = "weapon_" + id,
                      .name = id,
                      .slot = slot,
                      .position = 1,
                      .purchase = std::move(purchase),
                      .magazine = std::move(magazine),
                      .models = cs::WeaponModels{.view = "models/v_gun.mdl",
                                                 .player = "models/p_gun.mdl",
                                                 .playerAnimation = "gun"},
                      .view = cs::WeaponView{
                          .drawSequences = {1},
                          .idles = {cs::WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                          .idlesWhenEmpty = true}};
}

/// A knife, not sold, and an AK-47 of two commands.
[[nodiscard]] auto catalog() -> cs::WeaponCatalog {
    const cs::AmmoTypes ammo{.ammo = {cs::AmmoType{.id = "762Nato", .maxCarried = 90, .boxRounds = 30}}};
    auto made = cs::WeaponCatalog::make(
        ammo, {weaponOf("knife", cs::WeaponSlot::Knife, std::nullopt, std::nullopt),
               weaponOf("ak47", cs::WeaponSlot::Primary,
                        cs::WeaponPurchase{.buyers = cs::Buyers::Terrorists,
                                           .price = nx::Money{2500},
                                           .commands = std::vector<std::string>{"ak47", "cv47"}},
                        cs::WeaponMagazine{.ammo = "762Nato", .clipSize = 30})});
    EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
    return std::move(made).value();
}

/// A console with the buy commands over a knife and an AK-47, what they asked for and what the game says.
class BuyConsole {
public:
    BuyConsole() : console{output, files}, weapons{catalog()} {
        const cs::BuyActions actions{
            .weapon = [this](WeaponIndex weapon) -> nx::Result<void, cs::BuyRefusal> {
                bought.push_back(weapon);
                return answer();
            },
            .armor = [this](cs::ArmorKind kind) -> nx::Result<void, cs::BuyRefusal> {
                armor.push_back(kind);
                return answer();
            },
            .ammo = [this](cs::WeaponSlot slot,
                           cs::AmmoPurchase purchase) -> nx::Result<void, cs::BuyRefusal> {
                ammo.emplace_back(slot, purchase);
                return answer();
            },
            .refused = [this](cs::BuyRefusal told) { refusals.push_back(told); }};
        nx::invariant(cs::registerBuyCommands(console, weapons, actions).has_value(), "the buy commands");
        const cs::BuyMenuCommands commands{
            .toggle = [this]() -> nx::Result<void, cs::BuyRefusal> {
                ++toggles;
                return answer();
            },
            .openEquipment = [this]() -> nx::Result<void, cs::BuyRefusal> {
                ++equipmentOpenings;
                return answer();
            },
            .pressSlot = [this](std::uint32_t slot) { slots.push_back(slot); },
            .cancel = [this] { ++cancels; },
            .refused = [this](cs::BuyRefusal told) { refusals.push_back(told); }};
        nx::invariant(cs::registerBuyMenuCommands(console, commands).has_value(), "the buy menu's commands");
    }

    /// Runs `script` now.
    void run(std::string_view script) {
        console.addText(script);
        std::ignore = console.execute();
    }

    RecordingOutput output;
    NoFiles files;
    nx::console::Console console;
    cs::WeaponCatalog weapons;
    std::optional<cs::BuyRefusal> refusal;
    std::vector<WeaponIndex> bought;
    std::vector<cs::ArmorKind> armor;
    std::vector<std::pair<cs::WeaponSlot, cs::AmmoPurchase>> ammo;
    std::vector<cs::BuyRefusal> refusals;
    std::size_t toggles = 0;
    std::size_t equipmentOpenings = 0;
    std::size_t cancels = 0;
    std::vector<std::uint32_t> slots;

private:
    [[nodiscard]] auto answer() const -> nx::Result<void, cs::BuyRefusal> {
        if (refusal) {
            return nx::fail(*refusal);
        }
        return {};
    }
};

TEST(BuyMenuCommandsTest, BuyAndBuyequipAskForTheMenuAndWhyTheyCannotIsPrintedAndTold) {
    BuyConsole game;
    game.run("buy; buyequip");
    EXPECT_EQ(game.toggles, 1U);
    EXPECT_EQ(game.equipmentOpenings, 1U);
    EXPECT_TRUE(game.refusals.empty());

    game.refusal = cs::BuyRefusal::OutsideBuyZones;
    game.run("buy");
    EXPECT_EQ(game.refusals, (std::vector{cs::BuyRefusal::OutsideBuyZones}));
    EXPECT_EQ(game.output.lines,
              (std::vector<std::string>{std::string{cs::buyRefusalText(cs::BuyRefusal::OutsideBuyZones)}}));
    EXPECT_TRUE(game.output.failures.empty()) << "a refusal is no failure of the command";
}

TEST(BuyMenuCommandsTest, TheSlotsAreTheKeysOfTheirNumbersAndCancelselectGoesBack) {
    BuyConsole game;
    game.run("slot1; slot4; slot10; cancelselect");
    EXPECT_EQ(game.slots, (std::vector<std::uint32_t>{1, 4, 10}));
    EXPECT_EQ(game.cancels, 1U);
    EXPECT_TRUE(game.output.failures.empty());
}

TEST(BuyCommandsTest, ARefusalIsToldOnTheScreenToo) {
    BuyConsole game;
    game.refusal = cs::BuyRefusal::NotEnoughMoney;
    game.run("ak47; vest; buyammo1");
    EXPECT_EQ(game.refusals, (std::vector{cs::BuyRefusal::NotEnoughMoney, cs::BuyRefusal::NotEnoughMoney,
                                          cs::BuyRefusal::NotEnoughMoney}));
}

TEST(BuyCommandsTest, AWeaponIsBoughtByTheCommandsItsFileGivesItInAnyCase) {
    BuyConsole game;
    game.run("ak47; CV47");
    EXPECT_EQ(game.bought, (std::vector{kAk, kAk}));
    EXPECT_TRUE(game.output.failures.empty());
    EXPECT_TRUE(game.output.lines.empty());
}

TEST(BuyCommandsTest, BuyWeaponBuysOneByItsIdOrClassnameAndATypoIsAFailure) {
    BuyConsole game;
    game.run("buy_weapon ak47; buy_weapon WEAPON_AK47; buy_weapon knife");
    EXPECT_EQ(game.bought, (std::vector{kAk, kAk, kKnife}));
    EXPECT_TRUE(game.output.failures.empty());

    game.bought.clear();
    game.run("buy_weapon m4a1; buy_weapon");
    EXPECT_TRUE(game.bought.empty());
    EXPECT_EQ(game.output.failures,
              (std::vector{ConsoleError::InvalidArguments, ConsoleError::InvalidArguments}));
}

TEST(BuyCommandsTest, AWeaponMadeAsTheGameRunsIsBoughtOnceComplete) {
    BuyConsole game;
    auto incomplete = game.weapons.create({.id = "Empty", .slot = cs::WeaponSlot::Primary});
    ASSERT_TRUE(incomplete.has_value()) << nx::describe(incomplete.error());
    game.run("buy_weapon empty");
    EXPECT_TRUE(game.bought.empty());
    EXPECT_EQ(game.output.failures, std::vector{ConsoleError::CommandFailed});

    auto complete = game.weapons.create({.id = "DrakarIII", .slot = cs::WeaponSlot::Primary, .from = kAk});
    ASSERT_TRUE(complete.has_value()) << nx::describe(complete.error());
    game.run("buy_weapon drakariii");
    EXPECT_EQ(game.bought, std::vector{*complete});
}

TEST(BuyCommandsTest, ArmorAndAmmoAreBoughtByTheOriginalsNames) {
    BuyConsole game;
    game.run("vest; vesthelm");
    EXPECT_EQ(game.armor, (std::vector{cs::ArmorKind::Kevlar, cs::ArmorKind::KevlarAndHelmet}));
    game.run("buyammo1; buyammo2; primammo; secammo");
    const std::vector<std::pair<cs::WeaponSlot, cs::AmmoPurchase>> expected{
        {cs::WeaponSlot::Primary, cs::AmmoPurchase::OneBox},
        {cs::WeaponSlot::Secondary, cs::AmmoPurchase::OneBox},
        {cs::WeaponSlot::Primary, cs::AmmoPurchase::AsManyBoxes},
        {cs::WeaponSlot::Secondary, cs::AmmoPurchase::AsManyBoxes}};
    EXPECT_EQ(game.ammo, expected);
}

TEST(BuyCommandsTest, ARefusalIsPrintedAndNoFailureOfTheCommand) {
    BuyConsole game;
    game.refusal = cs::BuyRefusal::NotEnoughMoney;
    game.run("ak47");
    EXPECT_EQ(game.output.lines,
              std::vector{std::string{cs::buyRefusalText(cs::BuyRefusal::NotEnoughMoney)}});
    EXPECT_TRUE(game.output.failures.empty());
}

TEST(BuyCommandsTest, EachRefusalSaysSomethingOfItsOwn) {
    std::set<std::string_view> texts;
    for (const cs::BuyRefusal refusal : nx::EnumArray<cs::BuyRefusal, bool>::keys()) {
        EXPECT_FALSE(cs::buyRefusalText(refusal).empty()) << nx::enumName(refusal);
        EXPECT_TRUE(texts.insert(cs::buyRefusalText(refusal)).second) << nx::enumName(refusal);
    }
}

} // namespace
