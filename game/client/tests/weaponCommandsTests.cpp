// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The console's weapon commands (docs/10, section 7.5): a weapon named in any case, made, set,
// replaced and given through the console.

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

[[nodiscard]] auto view() -> cs::WeaponView {
    return cs::WeaponView{.drawSequences = {1},
                          .idles = {cs::WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                          .idlesWhenEmpty = true};
}

[[nodiscard]] auto models() -> cs::WeaponModels {
    return cs::WeaponModels{
        .view = "models/v_gun.mdl", .player = "models/p_gun.mdl", .playerAnimation = "gun"};
}

/// A knife and an AK-47.
[[nodiscard]] auto catalog() -> cs::WeaponCatalog {
    const cs::AmmoTypes ammo{.ammo = {cs::AmmoType{.id = "762Nato", .maxCarried = 90, .boxRounds = 30}}};
    auto made = cs::WeaponCatalog::make(
        ammo, {cs::Weapon{.id = "knife",
                          .classname = "weapon_knife",
                          .name = "Knife",
                          .slot = cs::WeaponSlot::Knife,
                          .position = 1,
                          .models = models(),
                          .view = view()},
               cs::Weapon{.id = "ak47",
                          .classname = "weapon_ak47",
                          .name = "CV-47",
                          .slot = cs::WeaponSlot::Primary,
                          .position = 1,
                          .weight = 25,
                          .magazine = cs::WeaponMagazine{.ammo = "762Nato", .clipSize = 30},
                          .models = models(),
                          .view = view()}});
    EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
    return std::move(made).value();
}

/// A console with the weapon commands over a knife and an AK-47, a map played, and what they did.
class WeaponConsole {
public:
    WeaponConsole() : console{output, files}, weapons{catalog()} {
        const cs::WeaponActions actions{.select =
                                            [this](WeaponIndex weapon) {
                                                selected.push_back(weapon);
                                                return true;
                                            },
                                        .give =
                                            [this](WeaponIndex weapon) {
                                                given.push_back(weapon);
                                                return true;
                                            }};
        nx::invariant(cs::registerWeaponCommands(console, weapons, actions).has_value(),
                      "the weapon commands");
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
    std::vector<WeaponIndex> selected;
    std::vector<WeaponIndex> given;
};

TEST(WeaponCommandsTest, AWeaponIsNamedByItsIdOrClassnameInAnyCase) {
    const cs::WeaponCatalog weapons = catalog();
    for (const std::string_view name : {"ak47", "AK47", "weapon_ak47", "WEAPON_AK47"}) {
        EXPECT_EQ(cs::findWeaponNamed(weapons, name), kAk) << name;
    }
    EXPECT_EQ(cs::findWeaponNamed(weapons, "m4a1"), std::nullopt);
}

TEST(WeaponCommandsTest, TheValueIsTheTextAfterTheWordsAsWritten) {
    EXPECT_EQ(cs::textAfterWords("DrakarIII name   Drakar III  ", 2), "Drakar III");
    EXPECT_EQ(cs::textAfterWords("DrakarIII magazine { ammo = \"9mm\", clipSize = 30 }", 2),
              "{ ammo = \"9mm\", clipSize = 30 }");
    EXPECT_EQ(cs::textAfterWords("DrakarIII name", 2), "");
}

TEST(WeaponCommandsTest, AWeaponIsMadeSetReplacedAndGivenThroughTheConsole) {
    WeaponConsole game;
    game.run("create_weapon DrakarIII primary ak47");
    const std::optional<WeaponIndex> drakar = game.weapons.indexOf("DrakarIII");
    ASSERT_TRUE(drakar.has_value());

    game.run("set_weapon_var drakariii name Drakar III");
    game.run("set_weapon_var DRAKARIII magazine.clipSize 60");
    EXPECT_EQ(game.weapons.weapon(*drakar).name, "Drakar III");
    EXPECT_EQ(game.weapons.weapon(*drakar).magazine->clipSize, 60U);

    game.run("give drakariii");
    game.run("weapon_drakariii");
    game.run("replace_weapon AK47 drakarIII");
    game.run("give weapon_ak47");
    EXPECT_EQ(game.given, (std::vector<WeaponIndex>{*drakar, kAk}))
        << "the replacement is the inventory's to make";
    EXPECT_EQ(game.selected, (std::vector<WeaponIndex>{*drakar}));
    EXPECT_EQ(game.weapons.madeFor(kAk), *drakar);
    EXPECT_TRUE(game.output.failures.empty());
}

TEST(WeaponCommandsTest, WhatTheCatalogRefusesIsATypedFailure) {
    WeaponConsole game;
    game.run("create_weapon staff knife");
    game.run("give staff");                            // incomplete
    game.run("create_weapon AK47 primary");            // taken
    game.run("set_weapon_var ak47 magazine.ammo 9mm"); // no such kind of ammo
    game.run("give m4a1");                             // no such weapon
    game.run("create_weapon gun sidearm");             // no such slot
    EXPECT_TRUE(game.given.empty());
    EXPECT_EQ(game.output.failures,
              (std::vector<ConsoleError>{ConsoleError::CommandFailed, ConsoleError::CommandFailed,
                                         ConsoleError::CommandFailed, ConsoleError::InvalidArguments,
                                         ConsoleError::InvalidArguments}));
    EXPECT_EQ(game.weapons.weapon(kKnife).id, "knife");
}

} // namespace
