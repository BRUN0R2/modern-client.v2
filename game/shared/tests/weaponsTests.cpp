// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The weapons' data: the repository's files, with the original's values, and what a file or the
// catalog may not hold. The repository's loadouts, and what they may not hold.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import cs.shared;

namespace {

using cs::AmmoType;
using cs::AmmoTypes;
using cs::Buyers;
using cs::Team;
using cs::Weapon;
using cs::WeaponCatalog;
using cs::WeaponError;
using cs::WeaponSlot;

/// The repository's data/cstrike/.
[[nodiscard]] auto dataFolder() -> std::filesystem::path {
    return std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike";
}

[[nodiscard]] auto textOf(const std::filesystem::path& file) -> std::string {
    auto text = nx::platform::readTextFile(file);
    EXPECT_TRUE(text.has_value()) << nx::platform::toUtf8(file);
    return text.value_or("");
}

/// data/cstrike/ammo.toml and every data/cstrike/weapons/<id>.toml, in a catalog.
[[nodiscard]] auto repositoryCatalog() -> std::optional<WeaponCatalog> {
    auto ammo = cs::readAmmoTypes(textOf(dataFolder() / "ammo.toml"), "ammo.toml");
    EXPECT_TRUE(ammo.has_value()) << (ammo ? "" : nx::describe(ammo.error()));
    const auto files = nx::platform::listFiles(dataFolder() / "weapons");
    EXPECT_TRUE(files.has_value());
    std::vector<Weapon> weapons;
    for (const std::filesystem::path& file : files.value_or(std::vector<std::filesystem::path>{})) {
        auto weapon = cs::readWeapon(textOf(dataFolder() / "weapons" / file), nx::platform::toUtf8(file));
        EXPECT_TRUE(weapon.has_value()) << (weapon ? "" : nx::describe(weapon.error()));
        EXPECT_EQ(weapon.value_or(Weapon{}).id, nx::platform::toUtf8(file.stem())) << "named after its id";
        weapons.push_back(weapon.value_or(Weapon{}));
    }
    auto catalog = WeaponCatalog::make(ammo.value_or(AmmoTypes{}), std::move(weapons));
    EXPECT_TRUE(catalog.has_value()) << (catalog ? "" : nx::describe(catalog.error()));
    return catalog ? std::optional{std::move(*catalog)} : std::nullopt;
}

/// A weapon's view, as a weapon file holds it.
constexpr std::string_view kGunView = R"(
[view]
drawSequences = [5]
idles = [{ sequence = 0, wait = 20.0, share = 1.0 }]
idlesWhenEmpty = true
)";

/// A weapon file with `extra` after its keys, its models and `view`.
[[nodiscard]] auto weaponText(std::string_view keys, std::string_view extra = "",
                              std::string_view view = kGunView) -> std::string {
    return std::format(R"(
{}
[models]
view = "models/v_gun.mdl"
player = "models/p_gun.mdl"
playerAnimation = "gun"
viewHand = "left"
{}
{})",
                       keys, view, extra);
}

constexpr std::string_view kGunKeys = R"(
id = "gun"
classname = "weapon_gun"
slot = "primary"
position = 1
weight = 25
maxSpeed = 221.0
)";

[[nodiscard]] auto gun(std::string id, WeaponSlot slot, std::uint32_t position) -> Weapon {
    return Weapon{.id = id,
                  .classname = "weapon_" + id,
                  .slot = slot,
                  .position = position,
                  .magazine = cs::WeaponMagazine{.ammo = "9mm", .clipSize = 20},
                  .view = cs::WeaponView{
                      .drawSequences = {5},
                      .idles = {cs::WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                      .idlesWhenEmpty = true}};
}

[[nodiscard]] auto nineMillimeter() -> AmmoTypes {
    return AmmoTypes{
        .ammo = {AmmoType{.id = "9mm", .maxCarried = 120, .boxPrice = nx::Money{20}, .boxRounds = 30}}};
}

TEST(WeaponsTest, TheRepositoryHasTheOriginalsAmmo) {
    const std::optional<WeaponCatalog> catalog = repositoryCatalog();
    ASSERT_TRUE(catalog.has_value());
    struct Expected {
        std::string_view id;
        std::uint32_t maxCarried;
        std::int32_t boxPrice;
        std::uint32_t boxRounds;
    };
    for (const Expected& expected : {Expected{"9mm", 120, 20, 30}, Expected{"45ACP", 100, 25, 12},
                                     Expected{"556Nato", 90, 60, 30}, Expected{"762Nato", 90, 80, 30}}) {
        const AmmoType* const ammo = catalog->findAmmo(expected.id);
        ASSERT_NE(ammo, nullptr) << expected.id;
        EXPECT_EQ(ammo->maxCarried, expected.maxCarried) << expected.id;
        EXPECT_EQ(ammo->boxPrice, nx::Money{expected.boxPrice}) << expected.id;
        EXPECT_EQ(ammo->boxRounds, expected.boxRounds) << expected.id;
    }
}

TEST(WeaponsTest, TheRepositoryHasTheOriginalsWeapons) {
    const std::optional<WeaponCatalog> catalog = repositoryCatalog();
    ASSERT_TRUE(catalog.has_value());
    struct Expected {
        std::string_view id;
        WeaponSlot slot;
        std::uint32_t position;
        std::uint32_t weight;
        float maxSpeed;
        std::optional<Buyers> buyers;
        std::int32_t price;
        std::string_view ammo;
        std::uint32_t clipSize;
        std::string_view playerAnimation;
    };
    for (const Expected& expected : {
             Expected{"knife", WeaponSlot::Knife, 1, 0, 250.0F, std::nullopt, 0, "", 0, "knife"},
             Expected{"glock18", WeaponSlot::Secondary, 2, 5, 250.0F, Buyers::Everyone, 400, "9mm", 20,
                      "onehanded"},
             Expected{"usp", WeaponSlot::Secondary, 4, 5, 250.0F, Buyers::Everyone, 500, "45ACP", 12,
                      "onehanded"},
             Expected{"ak47", WeaponSlot::Primary, 1, 25, 221.0F, Buyers::Terrorists, 2500, "762Nato", 30,
                      "ak47"},
             Expected{"m4a1", WeaponSlot::Primary, 6, 25, 230.0F, Buyers::CounterTerrorists, 3100, "556Nato",
                      30, "rifle"},
         }) {
        const Weapon* const weapon = catalog->findWeapon(expected.id);
        ASSERT_NE(weapon, nullptr) << expected.id;
        EXPECT_EQ(weapon->classname, std::format("weapon_{}", expected.id));
        EXPECT_EQ(weapon->slot, expected.slot) << expected.id;
        EXPECT_EQ(weapon->position, expected.position) << expected.id;
        EXPECT_EQ(weapon->weight, expected.weight) << expected.id;
        EXPECT_EQ(weapon->maxSpeed, nx::UnitsPerSecond{expected.maxSpeed}) << expected.id;
        EXPECT_EQ(weapon->models.view, std::format("models/v_{}.mdl", expected.id));
        EXPECT_EQ(weapon->models.player, std::format("models/p_{}.mdl", expected.id));
        EXPECT_EQ(weapon->models.playerAnimation, expected.playerAnimation) << expected.id;
        // Counter-Strike's guns are modeled in the left hand, its knife in the right (docs/05, section 9).
        EXPECT_EQ(weapon->models.viewHand, expected.id == "knife" ? cs::Hand::Right : cs::Hand::Left)
            << expected.id;
        EXPECT_EQ(weapon->purchase.has_value(), expected.buyers.has_value()) << expected.id;
        EXPECT_EQ(weapon->magazine.has_value(), !expected.ammo.empty()) << expected.id;
        if (weapon->purchase) {
            EXPECT_EQ(weapon->purchase->buyers, expected.buyers) << expected.id;
            EXPECT_EQ(weapon->purchase->price, nx::Money{expected.price}) << expected.id;
        }
        if (weapon->magazine) {
            EXPECT_EQ(weapon->magazine->ammo, expected.ammo) << expected.id;
            EXPECT_EQ(weapon->magazine->clipSize, expected.clipSize) << expected.id;
        }
    }
}

TEST(WeaponsTest, TheRepositorysWeaponsDrawAndIdleAsTheOriginalsDo) {
    const std::optional<WeaponCatalog> catalog = repositoryCatalog();
    ASSERT_TRUE(catalog.has_value());
    struct Idle {
        std::uint32_t sequence;
        float wait;
        float share;
    };
    struct Expected {
        std::string_view id;
        std::vector<std::uint32_t> drawSequences;
        std::vector<Idle> idles;
        bool idlesWhenEmpty;
    };
    for (const Expected& expected : {
             Expected{"knife", {3}, {{0, 20.0F, 1.0F}}, true},
             Expected{"glock18", {8, 11}, {{2, 3.0625F, 0.3F}, {0, 3.75F, 0.3F}, {1, 2.5F, 0.4F}}, false},
             Expected{"usp", {14}, {{8, 60.0F, 1.0F}}, false},
             Expected{"ak47", {2}, {{0, 20.0F, 1.0F}}, true},
             Expected{"m4a1", {12}, {{7, 20.0F, 1.0F}}, true},
         }) {
        const Weapon* const weapon = catalog->findWeapon(expected.id);
        ASSERT_NE(weapon, nullptr) << expected.id;
        EXPECT_EQ(weapon->view.drawSequences, expected.drawSequences) << expected.id;
        ASSERT_EQ(weapon->view.idles.size(), expected.idles.size()) << expected.id;
        for (std::size_t index = 0; index < expected.idles.size(); ++index) {
            EXPECT_EQ(weapon->view.idles[index].sequence, expected.idles[index].sequence) << expected.id;
            EXPECT_EQ(weapon->view.idles[index].wait, nx::Seconds{expected.idles[index].wait}) << expected.id;
            EXPECT_EQ(weapon->view.idles[index].share, expected.idles[index].share) << expected.id;
        }
        EXPECT_EQ(weapon->view.idlesWhenEmpty, expected.idlesWhenEmpty) << expected.id;
    }
}

TEST(WeaponsTest, AWeaponReadsItsKeys) {
    const auto weapon = cs::readWeapon(weaponText(kGunKeys, R"(
[purchase]
buyers = "counterTerrorists"
price = 3100

[magazine]
ammo = "556Nato"
clipSize = 30
)"),
                                       "gun.toml");
    ASSERT_TRUE(weapon.has_value()) << nx::describe(weapon.error());
    EXPECT_EQ(weapon->id, "gun");
    EXPECT_EQ(weapon->slot, WeaponSlot::Primary);
    EXPECT_EQ(weapon->maxSpeed, nx::UnitsPerSecond{221.0F});
    ASSERT_TRUE(weapon->purchase.has_value());
    EXPECT_EQ(weapon->purchase->buyers, Buyers::CounterTerrorists);
    ASSERT_TRUE(weapon->magazine.has_value());
    EXPECT_EQ(weapon->magazine->clipSize, 30U);
}

TEST(WeaponsTest, AWeaponBreakingItsSchemaIsInvalid) {
    for (const std::string& text : {
             weaponText(R"(id = "gun")"),                                      // missing keys
             weaponText(kGunKeys, "[magazine]\nammo = \"9mm\"\nclipSize = 0"), // an empty clip
             weaponText(std::string{kGunKeys} + "range = 8192"),               // an unknown key
             weaponText(R"(
id = "gun"
classname = "weapon_gun"
slot = "sidearm"
position = 1
weight = 5
maxSpeed = 250.0
)"),
         }) {
        const auto weapon = cs::readWeapon(text, "gun.toml");
        ASSERT_FALSE(weapon.has_value()) << text;
        EXPECT_EQ(weapon.error().code, WeaponError::DataInvalid);
        EXPECT_TRUE(weapon.error().detail.starts_with("gun.toml")) << weapon.error().detail;
    }
}

TEST(WeaponsTest, AWeaponWithoutWhatNamesOrShowsItIsInvalid) {
    constexpr std::string_view kNoId = R"(
id = ""
classname = "weapon_gun"
slot = "primary"
position = 1
weight = 25
maxSpeed = 221.0
)";
    const std::string badModels = std::string{kGunKeys} + R"(
[models]
view = "models/../v_gun.mdl"
player = "models/p_gun.mdl"
playerAnimation = "gun"
viewHand = "left"
)" + std::string{kGunView};
    const std::string noAnimation = std::string{kGunKeys} + R"(
[models]
view = "models/v_gun.mdl"
player = "models/p_gun.mdl"
playerAnimation = ""
viewHand = "left"
)" + std::string{kGunView};
    for (const std::string& text :
         {weaponText(kNoId), weaponText(kGunKeys, "[magazine]\nammo = \"\"\nclipSize = 30"), badModels,
          noAnimation}) {
        const auto weapon = cs::readWeapon(text, "gun.toml");
        ASSERT_FALSE(weapon.has_value()) << text;
        EXPECT_EQ(weapon.error().code, WeaponError::DataInvalid);
    }
}

TEST(WeaponsTest, AViewThatDrawsOrIdlesWithNothingIsInvalid) {
    for (const std::string_view view : {
             "[view]\ndrawSequences = []\nidles = [{ sequence = 0, wait = 20.0, share = 1.0 "
             "}]\nidlesWhenEmpty = true",
             "[view]\ndrawSequences = [5]\nidles = []\nidlesWhenEmpty = true",
             "[view]\ndrawSequences = [5]\nidles = [{ sequence = 0, wait = 0.0, share = 1.0 "
             "}]\nidlesWhenEmpty = true",
             "[view]\ndrawSequences = [5]\nidles = [{ sequence = 0, wait = 2.0, share = 0.5 }, "
             "{ sequence = 1, wait = 2.0, share = 0.4 }]\nidlesWhenEmpty = true",
             "[view]\ndrawSequences = [5]\nidles = [{ sequence = 0, wait = 2.0, share = 1.5 "
             "}]\nidlesWhenEmpty = true",
         }) {
        const std::string text = weaponText(kGunKeys, "", view);
        const auto weapon = cs::readWeapon(text, "gun.toml");
        ASSERT_FALSE(weapon.has_value()) << text;
        EXPECT_EQ(weapon.error().code, WeaponError::DataInvalid);
        EXPECT_TRUE(weapon.error().detail.starts_with("gun.toml")) << weapon.error().detail;
    }
}

TEST(WeaponsTest, AmmoWithoutAnIdOrRoundsIsInvalid) {
    for (const std::string_view text : {
             "[[ammo]]\nid = \"\"\nmaxCarried = 90\nboxPrice = 80\nboxRounds = 30",
             "[[ammo]]\nid = \"762Nato\"\nmaxCarried = 90\nboxPrice = 80\nboxRounds = 0",
         }) {
        const auto ammo = cs::readAmmoTypes(text, "ammo.toml");
        ASSERT_FALSE(ammo.has_value()) << text;
        EXPECT_EQ(ammo.error().code, WeaponError::DataInvalid);
    }
}

TEST(WeaponsTest, TheCatalogFindsWhatItHas) {
    const auto catalog = WeaponCatalog::make(
        nineMillimeter(), {gun("glock18", WeaponSlot::Secondary, 2), gun("mp5navy", WeaponSlot::Primary, 7)});
    ASSERT_TRUE(catalog.has_value()) << nx::describe(catalog.error());
    ASSERT_EQ(catalog->weapons().size(), 2U);
    EXPECT_EQ(catalog->weapons()[1].id, "mp5navy");
    ASSERT_NE(catalog->findWeapon("glock18"), nullptr);
    EXPECT_EQ(catalog->findWeapon("glock18")->position, 2U);
    EXPECT_EQ(catalog->findWeapon("ak47"), nullptr);
    EXPECT_EQ(catalog->indexOf("mp5navy"), cs::WeaponIndex{1});
    EXPECT_EQ(catalog->weapon(cs::WeaponIndex{1}).id, "mp5navy");
    EXPECT_EQ(catalog->indexOf("ak47"), std::nullopt);
    ASSERT_NE(catalog->findAmmo("9mm"), nullptr);
    EXPECT_EQ(catalog->findAmmo("9mm")->maxCarried, 120U);
    EXPECT_EQ(catalog->findAmmo("45ACP"), nullptr);
}

TEST(WeaponsTest, TheCatalogRefusesWhatClashesOrIsMissing) {
    Weapon renamed = gun("glock", WeaponSlot::Secondary, 3);
    renamed.classname = "weapon_glock18";
    Weapon unknownAmmo = gun("usp", WeaponSlot::Secondary, 4);
    unknownAmmo.magazine->ammo = "45ACP";
    AmmoTypes twice = nineMillimeter();
    twice.ammo.push_back(twice.ammo.front());
    Weapon unseen = gun("usp", WeaponSlot::Secondary, 4);
    unseen.view = cs::WeaponView{};
    const Weapon glock = gun("glock18", WeaponSlot::Secondary, 2);
    struct Case {
        AmmoTypes ammo;
        std::vector<Weapon> weapons;
        std::string_view detail;
    };
    for (const Case& clash : {
             Case{nineMillimeter(), {glock, glock}, "the weapon \"glock18\" twice"},
             Case{nineMillimeter(), {glock, renamed}, "the weapon class \"weapon_glock18\" twice"},
             Case{nineMillimeter(), {glock, gun("elite", WeaponSlot::Secondary, 2)}, "position 2"},
             Case{nineMillimeter(), {glock, unknownAmmo}, "\"45ACP\""},
             Case{twice, {glock}, "the kind of ammo \"9mm\" twice"},
             Case{nineMillimeter(), {glock, unseen}, "\"usp\" without a draw sequence"},
         }) {
        const auto catalog = WeaponCatalog::make(clash.ammo, clash.weapons);
        ASSERT_FALSE(catalog.has_value()) << clash.detail;
        EXPECT_EQ(catalog.error().code, WeaponError::DataInvalid);
        EXPECT_TRUE(catalog.error().detail.contains(clash.detail)) << catalog.error().detail;
    }
}

TEST(LoadoutsTest, TheRepositoryHasTheOriginalsLoadouts) {
    const std::optional<WeaponCatalog> catalog = repositoryCatalog();
    ASSERT_TRUE(catalog.has_value());
    const auto loadouts = cs::readLoadouts(textOf(dataFolder() / "loadouts.toml"), "loadouts.toml", *catalog);
    ASSERT_TRUE(loadouts.has_value()) << nx::describe(loadouts.error());
    const cs::Loadout* const terrorists = loadouts->find(Team::Terrorists);
    const cs::Loadout* const counterTerrorists = loadouts->find(Team::CounterTerrorists);
    ASSERT_NE(terrorists, nullptr);
    ASSERT_NE(counterTerrorists, nullptr);
    EXPECT_EQ(terrorists->weapons, (std::vector<std::string>{"knife", "glock18"}));
    ASSERT_EQ(terrorists->ammo.size(), 1U);
    EXPECT_EQ(terrorists->ammo[0].id, "9mm");
    EXPECT_EQ(terrorists->ammo[0].rounds, 40U);
    EXPECT_EQ(counterTerrorists->weapons, (std::vector<std::string>{"knife", "usp"}));
    ASSERT_EQ(counterTerrorists->ammo.size(), 1U);
    EXPECT_EQ(counterTerrorists->ammo[0].id, "45ACP");
    EXPECT_EQ(counterTerrorists->ammo[0].rounds, 24U);
}

TEST(LoadoutsTest, ALoadoutOfWhatTheCatalogLacksOrATeamTwiceIsInvalid) {
    const auto catalog = WeaponCatalog::make(nineMillimeter(), {gun("glock18", WeaponSlot::Secondary, 2)});
    ASSERT_TRUE(catalog.has_value()) << nx::describe(catalog.error());
    for (const std::string_view text : {
             R"([[loadouts]]
team = "terrorists"
weapons = ["glock18"]
ammo = [{ id = "9mm", rounds = 40 }]

[[loadouts]]
team = "terrorists"
weapons = []
ammo = [])",
             "[[loadouts]]\nteam = \"terrorists\"\nweapons = [\"ak47\"]\nammo = []",
             "[[loadouts]]\nteam = \"terrorists\"\nweapons = []\nammo = [{ id = \"45ACP\", rounds = 24 }]",
             "[[loadouts]]\nteam = \"spectators\"\nweapons = []\nammo = []",
         }) {
        const auto loadouts = cs::readLoadouts(text, "loadouts.toml", *catalog);
        ASSERT_FALSE(loadouts.has_value()) << text;
        EXPECT_EQ(loadouts.error().code, WeaponError::DataInvalid);
        EXPECT_TRUE(loadouts.error().detail.starts_with("loadouts.toml")) << loadouts.error().detail;
    }
}

} // namespace
