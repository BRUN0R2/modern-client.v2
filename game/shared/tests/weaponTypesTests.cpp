// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Weapons made as the game runs (docs/10, section 7.5): standalone, from nothing or from another's
// values copied once; their values set by the key paths of their data; replacements; no limits.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.data;
import nx.game;
import cs.shared;

namespace {

using cs::AmmoType;
using cs::AmmoTypes;
using cs::Inventory;
using cs::Weapon;
using cs::WeaponCatalog;
using cs::WeaponDraft;
using cs::WeaponError;
using cs::WeaponIndex;
using cs::WeaponSlot;

constexpr WeaponIndex kKnife{0};
constexpr WeaponIndex kAk{1};

[[nodiscard]] auto view() -> cs::WeaponView {
    return cs::WeaponView{.drawSequences = {1},
                          .idles = {cs::WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                          .idlesWhenEmpty = true};
}

[[nodiscard]] auto models(std::string_view id) -> cs::WeaponModels {
    return cs::WeaponModels{.view = std::format("models/v_{}.mdl", id),
                            .player = std::format("models/p_{}.mdl", id),
                            .playerAnimation = std::string{id}};
}

/// A knife and an AK-47, of 762Nato and 9mm.
[[nodiscard]] auto catalog() -> WeaponCatalog {
    const AmmoTypes ammo{.ammo = {AmmoType{.id = "762Nato", .maxCarried = 90, .boxRounds = 30},
                                  AmmoType{.id = "9mm", .maxCarried = 120, .boxRounds = 30}}};
    auto made =
        WeaponCatalog::make(ammo, {Weapon{.id = "knife",
                                          .classname = "weapon_knife",
                                          .name = "Knife",
                                          .slot = WeaponSlot::Knife,
                                          .position = 1,
                                          .models = models("knife"),
                                          .view = view()},
                                   Weapon{.id = "ak47",
                                          .classname = "weapon_ak47",
                                          .name = "CV-47",
                                          .slot = WeaponSlot::Primary,
                                          .position = 1,
                                          .weight = 25,
                                          .maxSpeed = nx::UnitsPerSecond{221.0F},
                                          .magazine = cs::WeaponMagazine{.ammo = "762Nato", .clipSize = 30},
                                          .models = models("ak47"),
                                          .view = view()}});
    EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
    return std::move(made).value();
}

[[nodiscard]] auto created(WeaponCatalog& weapons, const WeaponDraft& draft) -> WeaponIndex {
    auto made = weapons.create(draft);
    EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
    return made.value_or(WeaponIndex{});
}

/// The value of `weapon` at `keyPath`, which must be a key path of its data.
[[nodiscard]] auto varOf(const WeaponCatalog& weapons, WeaponIndex weapon, std::string_view keyPath)
    -> std::optional<std::string> {
    auto value = weapons.var(weapon, keyPath);
    EXPECT_TRUE(value.has_value()) << keyPath << ": " << (value ? "" : nx::describe(value.error()));
    return value ? *value : std::nullopt;
}

void set(WeaponCatalog& weapons, WeaponIndex weapon, std::string_view keyPath, std::string_view text) {
    auto setVar = weapons.setVar(weapon, keyPath, text);
    EXPECT_TRUE(setVar.has_value()) << keyPath << ": " << (setVar ? "" : nx::describe(setVar.error()));
}

TEST(WeaponTypesTest, AWeaponFromAnotherCopiesItsValuesOnce) {
    WeaponCatalog weapons = catalog();
    const WeaponIndex drakar =
        created(weapons, {.id = "DrakarIII", .slot = WeaponSlot::Primary, .from = kAk});
    EXPECT_EQ(drakar, WeaponIndex{2});
    const Weapon& made = weapons.weapon(drakar);
    EXPECT_EQ(made.classname, "weapon_DrakarIII");
    EXPECT_EQ(made.name, "DrakarIII") << "its name its id until set";
    EXPECT_EQ(made.position, 2U) << "the next of its slot";
    EXPECT_EQ(made.magazine->clipSize, 30U);
    EXPECT_TRUE(weapons.isComplete(drakar));
    EXPECT_EQ(weapons.findWeapon("DrakarIII"), &made);
    EXPECT_EQ(weapons.findWeapon("drakariii"), &made) << "in any case";
    EXPECT_EQ(weapons.indexOf("DRAKARIII"), drakar);

    set(weapons, kAk, "magazine.clipSize", "45");
    EXPECT_EQ(weapons.weapon(drakar).magazine->clipSize, 30U) << "linked to the AK-47 no more";
    set(weapons, drakar, "name", "Drakar III");
    EXPECT_EQ(weapons.weapon(drakar).name, "Drakar III");
    EXPECT_EQ(weapons.weapon(kAk).name, "CV-47");
}

TEST(WeaponTypesTest, MakingAWeaponMovesNoOther) {
    WeaponCatalog weapons = catalog();
    const Weapon* const ak = &weapons.weapon(kAk);
    for (std::uint32_t made = 0; made < 100; ++made) {
        std::ignore =
            created(weapons, {.id = std::format("gun{}", made), .slot = WeaponSlot::Primary, .from = kAk});
    }
    EXPECT_EQ(&weapons.weapon(kAk), ak);
}

TEST(WeaponTypesTest, AWeaponFromNothingIsIncompleteUntilItsValuesMakeAWeapon) {
    WeaponCatalog weapons = catalog();
    const WeaponIndex staff = created(weapons, {.id = "staff", .slot = WeaponSlot::Knife});
    EXPECT_EQ(weapons.weapon(staff).position, 2U);
    EXPECT_FALSE(weapons.isComplete(staff));
    const auto lacking = weapons.checkComplete(staff);
    ASSERT_FALSE(lacking.has_value());
    EXPECT_EQ(lacking.error().code, WeaponError::Incomplete);

    set(weapons, staff, "models",
        R"({ view = "models/v_staff.mdl", player = "models/p_staff.mdl", playerAnimation = "knife", )"
        R"(viewHand = "right" })");
    EXPECT_FALSE(weapons.isComplete(staff)) << "without a view yet";
    set(weapons, staff, "view.drawSequences", "[3]");
    set(weapons, staff, "view.idles", "[{ sequence = 0, wait = 20.0, share = 1.0 }]");
    EXPECT_TRUE(weapons.isComplete(staff));
    EXPECT_TRUE(weapons.checkComplete(staff).has_value());
}

TEST(WeaponTypesTest, AValueThatLeavesACompleteWeaponIncompleteIsRefused) {
    WeaponCatalog weapons = catalog();
    const std::uint32_t revision = weapons.revision(kAk);
    for (const auto& [keyPath, text] : std::initializer_list<std::pair<std::string_view, std::string_view>>{
             {"magazine.ammo", "45ACP"},         // no such kind of ammo
             {"view.drawSequences", "[]"},       // no draw sequence
             {"models.view", "models/../v.mdl"}, // not a path of the game's files
         }) {
        const auto setVar = weapons.setVar(kAk, keyPath, text);
        ASSERT_FALSE(setVar.has_value()) << keyPath;
        EXPECT_EQ(setVar.error().code, WeaponError::VarInvalid);
        EXPECT_TRUE(setVar.error().detail.contains("leaves it incomplete")) << setVar.error().detail;
    }
    EXPECT_EQ(weapons.weapon(kAk).magazine->ammo, "762Nato");
    EXPECT_EQ(weapons.revision(kAk), revision) << "nothing set";
    set(weapons, kAk, "magazine.ammo", "9mm");
    EXPECT_EQ(weapons.revision(kAk), revision + 1);
}

TEST(WeaponTypesTest, VarsAreTheKeyPathsOfTheWeaponsData) {
    WeaponCatalog weapons = catalog();
    EXPECT_EQ(varOf(weapons, kAk, "magazine.clipSize"), std::optional<std::string>{"30"});
    EXPECT_EQ(varOf(weapons, kAk, "maxSpeed"), std::optional<std::string>{"221"});
    EXPECT_EQ(varOf(weapons, kAk, "firing.cycle"), std::nullopt) << "it has no firing";
    const auto unknown = weapons.var(kAk, "magazine.size");
    ASSERT_FALSE(unknown.has_value());
    EXPECT_EQ(unknown.error().code, WeaponError::VarInvalid);
    EXPECT_EQ(unknown.error().cause.records().front().domain, "DataError");

    const std::vector<nx::data::FieldText> vars = weapons.vars(kAk);
    EXPECT_TRUE(std::ranges::contains(vars, nx::data::FieldText{.keyPath = "name", .value = R"("CV-47")"}));
    EXPECT_TRUE(std::ranges::contains(
        vars, nx::data::FieldText{.keyPath = "magazine.ammo", .value = R"("762Nato")"}));

    for (const std::string_view keyPath : {"id", "classname", "slot", "position"}) {
        const auto setVar = weapons.setVar(kAk, keyPath, "x");
        ASSERT_FALSE(setVar.has_value()) << keyPath;
        EXPECT_EQ(setVar.error().code, WeaponError::VarReadOnly);
    }
    const auto wrongType = weapons.setVar(kAk, "weight", "heavy");
    ASSERT_FALSE(wrongType.has_value());
    EXPECT_EQ(wrongType.error().code, WeaponError::VarInvalid);
}

TEST(WeaponTypesTest, AWeaponsMuzzleFlashIsSetByItsKeyPaths) {
    WeaponCatalog weapons = catalog();
    EXPECT_EQ(varOf(weapons, kAk, "view.muzzleFlash"), std::nullopt) << "as its view model's events say";
    set(weapons, kAk, "view.muzzleFlash.sprite", "sprites/muzzleflash341.spr");
    set(weapons, kAk, "view.muzzleFlash.brightness", "6");
    set(weapons, kAk, "view.muzzleFlash.light",
        "{ color = [3000.0, 1800.0, 600.0], reach = 96.0, time = 0.06 }");
    const cs::Weapon& ak = weapons.weapon(kAk);
    ASSERT_TRUE(ak.view.muzzleFlash.has_value());
    EXPECT_EQ(ak.view.muzzleFlash->sprite, "sprites/muzzleflash341.spr");
    EXPECT_EQ(ak.view.muzzleFlash->brightness, 6.0F);
    ASSERT_TRUE(ak.view.muzzleFlash->light.has_value());
    EXPECT_EQ(ak.view.muzzleFlash->light->reach, nx::HammerUnits{96.0F});
    EXPECT_EQ(varOf(weapons, kAk, "view.muzzleFlash.sprite"),
              std::optional<std::string>{R"("sprites/muzzleflash341.spr")"});

    const auto outOfRange = weapons.setVar(kAk, "view.muzzleFlash.roll", "400");
    ASSERT_FALSE(outOfRange.has_value());
    EXPECT_EQ(outOfRange.error().code, WeaponError::VarInvalid);
}

TEST(WeaponTypesTest, AnIdIsLettersAndDigitsNotTakenInAnyCase) {
    WeaponCatalog weapons = catalog();
    for (const std::string_view id : {"", "drakar iii", "drakar_iii", "drakar.iii"}) {
        const auto made = weapons.create({.id = id, .slot = WeaponSlot::Primary});
        ASSERT_FALSE(made.has_value()) << id;
        EXPECT_EQ(made.error().code, WeaponError::IdInvalid);
    }
    for (const std::string_view id : {"ak47", "AK47"}) {
        const auto made = weapons.create({.id = id, .slot = WeaponSlot::Primary});
        ASSERT_FALSE(made.has_value()) << id;
        EXPECT_EQ(made.error().code, WeaponError::IdTaken);
    }
    EXPECT_EQ(weapons.weapons().size(), 2U);
}

TEST(WeaponTypesTest, AReplacedWeaponIsMadeAsItsReplacement) {
    WeaponCatalog weapons = catalog();
    const WeaponIndex drakar =
        created(weapons, {.id = "DrakarIII", .slot = WeaponSlot::Primary, .from = kAk});
    const WeaponIndex staff = created(weapons, {.id = "staff", .slot = WeaponSlot::Knife});
    ASSERT_TRUE(weapons.replace(kAk, drakar).has_value());
    EXPECT_EQ(weapons.madeFor(kAk), drakar);
    EXPECT_EQ(weapons.madeFor(drakar), drakar);

    struct Case {
        WeaponIndex original;
        WeaponIndex replacement;
        std::string_view problem;
    };
    for (const Case& refused : {
             Case{kKnife, kKnife, "replaced by itself"},
             Case{kKnife, kAk, "replaced in its turn"},
             Case{drakar, kKnife, "replaces another"},
             Case{kKnife, staff, "incomplete"},
         }) {
        const auto replaced = weapons.replace(refused.original, refused.replacement);
        ASSERT_FALSE(replaced.has_value()) << refused.problem;
        EXPECT_EQ(replaced.error().code, WeaponError::ReplacementInvalid);
        EXPECT_TRUE(replaced.error().detail.contains(refused.problem)) << replaced.error().detail;
    }
    ASSERT_TRUE(weapons.replace(kAk, std::nullopt).has_value());
    EXPECT_EQ(weapons.madeFor(kAk), kAk);
}

TEST(WeaponTypesTest, ThePlayerIsGivenAndSelectsTheReplacement) {
    WeaponCatalog weapons = catalog();
    const WeaponIndex drakar =
        created(weapons, {.id = "DrakarIII", .slot = WeaponSlot::Primary, .from = kAk});
    set(weapons, drakar, "magazine.clipSize", "60");
    Inventory inventory{weapons};
    nx::Random random{0, "test"};
    ASSERT_TRUE(inventory.give(kKnife, cs::AutoSwitch::ToHeavier, {}, random));
    ASSERT_TRUE(weapons.replace(kAk, drakar).has_value());
    ASSERT_TRUE(inventory.give(kAk, cs::AutoSwitch::ToHeavier, {}, random));
    ASSERT_EQ(inventory.weapons().size(), 2U);
    EXPECT_EQ(inventory.weapons()[1].weapon, drakar);
    EXPECT_EQ(inventory.weapons()[1].clip, 60U);

    inventory.select(cs::UserCommand{.number = cs::CommandNumber{1}, .weaponSelect = kKnife}, {});
    EXPECT_EQ(inventory.held()->weapon, kKnife);
    inventory.select(cs::UserCommand{.number = cs::CommandNumber{2}, .weaponSelect = kAk}, {});
    EXPECT_EQ(inventory.held()->weapon, drakar) << "weapon_ak47 selects its replacement";
}

TEST(WeaponTypesTest, TheCatalogHasNoLimitOfWeaponsNorOfKindsOfAmmo) {
    // The original's has 32 of each: m_rgAmmo[MAX_AMMO_SLOTS], and the bits of pev->weapons.
    constexpr std::uint32_t kKinds = 300;
    AmmoTypes ammo;
    std::vector<Weapon> made;
    for (std::uint32_t kind = 0; kind < kKinds; ++kind) {
        ammo.ammo.push_back(AmmoType{.id = std::format("ammo{}", kind), .maxCarried = 90, .boxRounds = 30});
        made.push_back(
            Weapon{.id = std::format("gun{}", kind),
                   .classname = std::format("weapon_gun{}", kind),
                   .name = std::format("Gun {}", kind),
                   .slot = WeaponSlot::Primary,
                   .position = kind,
                   .magazine = cs::WeaponMagazine{.ammo = std::format("ammo{}", kind), .clipSize = 30},
                   .models = models("gun"),
                   .view = view()});
    }
    auto weapons = WeaponCatalog::make(std::move(ammo), std::move(made));
    ASSERT_TRUE(weapons.has_value()) << nx::describe(weapons.error());
    const WeaponIndex last =
        created(*weapons, {.id = "gunLast", .slot = WeaponSlot::Primary, .from = WeaponIndex{0}});
    EXPECT_EQ(last, WeaponIndex{kKinds});
    Inventory inventory{*weapons};
    nx::Random random{0, "test"};
    for (std::uint32_t index = 0; index <= kKinds; ++index) {
        EXPECT_TRUE(inventory.give(WeaponIndex{index}, cs::AutoSwitch::Off, {}, random));
    }
    EXPECT_EQ(inventory.weapons().size(), kKinds + 1);
    EXPECT_EQ(inventory.giveAmmo("ammo299", 90), 90U);
}

} // namespace
