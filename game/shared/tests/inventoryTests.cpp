// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A player's weapons: given with their rounds, drawn when heavier, chosen by a command or `lastinv`,
// idling after their draw and each wait, with the random numbers of the command's number.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.game;
import cs.shared;

namespace {

using cs::CommandNumber;
using cs::Inventory;
using cs::UserCommand;
using cs::Weapon;
using cs::WeaponCatalog;
using cs::WeaponIdle;
using cs::WeaponIndex;
using cs::WeaponSlot;

constexpr WeaponIndex kKnife{0};
constexpr WeaponIndex kGlock{1};
constexpr WeaponIndex kAk{2};

/// Ticks at the default rate, from the map's start.
constexpr std::int64_t kTicksPerSecond = 100;

[[nodiscard]] auto at(std::int64_t tick) -> nx::game::TickTime {
    return nx::game::TickTime{.now = nx::GameTime::atTick(tick)};
}

[[nodiscard]] auto weapon(std::string id, WeaponSlot slot, std::uint32_t weight, float maxSpeed,
                          std::optional<cs::WeaponMagazine> magazine, cs::WeaponView view) -> Weapon {
    return Weapon{.id = id,
                  .classname = "weapon_" + id,
                  .slot = slot,
                  .weight = weight,
                  .maxSpeed = nx::UnitsPerSecond{maxSpeed},
                  .magazine = std::move(magazine),
                  .view = std::move(view)};
}

/// A knife, a Glock and an AK-47, as the original's.
[[nodiscard]] auto catalog() -> WeaponCatalog {
    const cs::AmmoTypes ammo{.ammo = {cs::AmmoType{.id = "9mm", .maxCarried = 120, .boxRounds = 30},
                                      cs::AmmoType{.id = "762Nato", .maxCarried = 90, .boxRounds = 30}}};
    auto made = WeaponCatalog::make(
        ammo, {weapon("knife", WeaponSlot::Knife, 0, 250.0F, std::nullopt,
                      {.drawSequences = {3},
                       .idles = {WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                       .idlesWhenEmpty = true}),
               weapon("glock18", WeaponSlot::Secondary, 5, 250.0F,
                      cs::WeaponMagazine{.ammo = "9mm", .clipSize = 20},
                      {.drawSequences = {8, 11},
                       .idles = {WeaponIdle{.sequence = 2, .wait = nx::Seconds{3.0625F}, .share = 0.3F},
                                 WeaponIdle{.sequence = 0, .wait = nx::Seconds{3.75F}, .share = 0.3F},
                                 WeaponIdle{.sequence = 1, .wait = nx::Seconds{2.5F}, .share = 0.4F}},
                       .idlesWhenEmpty = false}),
               weapon("ak47", WeaponSlot::Primary, 25, 221.0F,
                      cs::WeaponMagazine{.ammo = "762Nato", .clipSize = 30},
                      {.drawSequences = {2},
                       .idles = {WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                       .idlesWhenEmpty = true})});
    EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
    return std::move(made).value();
}

[[nodiscard]] auto command(std::uint32_t number, std::optional<WeaponIndex> select = std::nullopt)
    -> UserCommand {
    return UserCommand{.number = CommandNumber{number}, .weaponSelect = select};
}

/// An inventory given `weapons` in their order at the map's start, each drawn when heavier.
[[nodiscard]] auto given(const WeaponCatalog& weapons, std::initializer_list<WeaponIndex> indices)
    -> Inventory {
    Inventory inventory{weapons};
    nx::Random random{0, "test"};
    for (const WeaponIndex index : indices) {
        EXPECT_TRUE(inventory.give(index, true, at(0), random));
    }
    return inventory;
}

TEST(InventoryTest, TheFirstWeaponAndEachHeavierOneAreDrawn) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory{weapons};
    EXPECT_EQ(inventory.held(), nullptr);
    EXPECT_EQ(inventory.maxSpeed(), cs::kUnarmedSpeed);
    nx::Random random{0, "test"};

    EXPECT_TRUE(inventory.give(kKnife, true, at(0), random));
    ASSERT_NE(inventory.held(), nullptr);
    EXPECT_EQ(inventory.held()->weapon, kKnife);
    ASSERT_TRUE(inventory.animation().has_value());
    EXPECT_EQ(inventory.animation()->sequence, 3U);
    EXPECT_EQ(inventory.maxSpeed(), nx::UnitsPerSecond{250.0F});

    EXPECT_TRUE(inventory.give(kGlock, true, at(10), random));
    EXPECT_EQ(inventory.held()->weapon, kGlock);
    EXPECT_EQ(inventory.held()->clip, 20U);
    EXPECT_EQ(inventory.reserve("9mm"), 0U);
    EXPECT_TRUE(inventory.animation()->sequence == 8U || inventory.animation()->sequence == 11U);
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(10));
    EXPECT_EQ(inventory.lastWeapon(), kKnife);
}

TEST(InventoryTest, ALighterWeaponOrOneGivenWithoutAutoSwitchIsNotDrawn) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kAk, kGlock});
    EXPECT_EQ(inventory.held()->weapon, kAk);
    EXPECT_EQ(inventory.maxSpeed(), nx::UnitsPerSecond{221.0F});

    Inventory knifeFirst = given(weapons, {kKnife});
    nx::Random random{0, "test"};
    EXPECT_TRUE(knifeFirst.give(kAk, false, at(0), random));
    EXPECT_EQ(knifeFirst.held()->weapon, kKnife);
}

TEST(InventoryTest, AWeaponHadAlreadyGivesItsRoundsAndAmmoStopsAtWhatIsCarried) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    nx::Random random{0, "test"};
    EXPECT_FALSE(inventory.give(kGlock, true, at(0), random));
    EXPECT_EQ(inventory.weapons().size(), 1U);
    EXPECT_EQ(inventory.held()->clip, 20U);
    EXPECT_EQ(inventory.reserve("9mm"), 20U);

    EXPECT_EQ(inventory.giveAmmo("9mm", 200), 100U);
    EXPECT_EQ(inventory.reserve("9mm"), 120U);
    EXPECT_EQ(inventory.giveAmmo("9mm", 1), 0U);
    EXPECT_EQ(inventory.giveAmmo("45ACP", 24), 0U);
    EXPECT_EQ(inventory.reserve("45ACP"), 0U);
}

TEST(InventoryTest, ACommandDrawsTheWeaponItSelectsAndLastinvTheOneBefore) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kKnife, kGlock});

    inventory.play(command(1, kKnife), at(5));
    EXPECT_EQ(inventory.held()->weapon, kKnife);
    EXPECT_EQ(inventory.animation()->sequence, 3U);
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(5));
    EXPECT_EQ(inventory.lastWeapon(), kGlock);

    // A weapon the player lacks, or the one it holds, is not drawn again.
    inventory.play(command(2, kAk), at(6));
    inventory.play(command(3, kKnife), at(7));
    EXPECT_EQ(inventory.held()->weapon, kKnife);
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(5));
}

TEST(InventoryTest, LastinvWithoutAWeaponBeforeTakesTheNewestOfTheFirstSlotWithAnother) {
    const WeaponCatalog weapons = catalog();
    EXPECT_EQ(given(weapons, {kKnife}).lastWeapon(), std::nullopt);

    Inventory inventory = given(weapons, {kAk});
    nx::Random random{0, "test"};
    std::ignore = inventory.give(kKnife, true, at(0), random); // lighter: not drawn
    std::ignore = inventory.give(kGlock, true, at(0), random);
    // The primary slot's newest is held, the secondary's is the Glock.
    EXPECT_EQ(inventory.lastWeapon(), kGlock);
}

TEST(InventoryTest, TheWeaponHeldIdlesAfterItsDrawAndThenAfterEachWait) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kKnife});
    const std::int64_t firstIdle = kTicksPerSecond * 3 / 2;
    const std::int64_t wait = kTicksPerSecond * 20;

    inventory.play(command(1), at(firstIdle - 1));
    EXPECT_EQ(inventory.animation()->sequence, 3U); // still drawing
    inventory.play(command(2), at(firstIdle));
    EXPECT_EQ(inventory.animation()->sequence, 0U);
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(firstIdle));
    inventory.play(command(3), at(firstIdle + wait - 1));
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(firstIdle));
    inventory.play(command(4), at(firstIdle + wait));
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(firstIdle + wait));
}

TEST(InventoryTest, IdlesAreChosenByTheirSharesTheSameForTheSameCommand) {
    const WeaponCatalog weapons = catalog();
    const std::int64_t firstIdle = kTicksPerSecond * 3 / 2;
    std::map<std::uint32_t, int> chosen;
    constexpr std::uint32_t kCommands = 4000;
    for (std::uint32_t number = 1; number <= kCommands; ++number) {
        Inventory inventory = given(weapons, {kGlock});
        inventory.play(command(number), at(firstIdle));
        ++chosen[inventory.animation()->sequence];

        Inventory again = given(weapons, {kGlock});
        again.play(command(number), at(firstIdle));
        ASSERT_EQ(again.animation()->sequence, inventory.animation()->sequence);
    }
    constexpr double kTolerance = 0.03;
    EXPECT_NEAR(chosen[2] / double{kCommands}, 0.3, kTolerance);
    EXPECT_NEAR(chosen[0] / double{kCommands}, 0.3, kTolerance);
    EXPECT_NEAR(chosen[1] / double{kCommands}, 0.4, kTolerance);
}

TEST(InventoryTest, EachDrawSequenceIsChosenAtRandom) {
    const WeaponCatalog weapons = catalog();
    std::set<std::uint32_t> drawn;
    for (std::uint32_t number = 1; number <= 64; ++number) {
        Inventory inventory = given(weapons, {kGlock, kKnife});
        inventory.play(command(number, kKnife), at(1));
        inventory.play(command(number, kGlock), at(2));
        drawn.insert(inventory.animation()->sequence);
    }
    EXPECT_EQ(drawn, (std::set<std::uint32_t>{8, 11}));
}

} // namespace
