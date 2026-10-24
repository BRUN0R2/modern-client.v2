// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// A player's weapons: given with their rounds, drawn when heavier, chosen by a command or `lastinv`,
// idling after their draw and each wait, with the random numbers of the command's number; and the
// guns' fire, as the original's: their cycle, once a press for a pistol, the empty click, the
// accuracy by burst or by pace, the spread as the shooter moves, the kick, and the reload; the timers
// counted down in float by each command, as many commands as the original's last; and the
// alternates `+attack2` switches: the USP's silencer, the Glock's burst; the knife's slash and
// stab, missing or hitting what the tracer finds; and the sounds they all make.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.collision;
import nx.game;
import cs.shared;

namespace {

using cs::Button;
using cs::CommandNumber;
using cs::Inventory;
using cs::Shooter;
using cs::Shot;
using cs::SpreadTerm;
using cs::UserCommand;
using cs::Weapon;
using cs::WeaponCatalog;
using cs::WeaponIdle;
using cs::WeaponIndex;
using cs::WeaponSlot;

constexpr WeaponIndex kKnife{0};
constexpr WeaponIndex kGlock{1};
constexpr WeaponIndex kAk{2};
constexpr WeaponIndex kUsp{3};

/// A command each tick, at the default rate of 100 a second.
constexpr nx::Milliseconds kCommandDuration{10.0F};
/// A weapon given before the first command fires from this one: DefaultDeploy's 0.75 seconds, which
/// counted down in float last 76 commands, as the original's do.
constexpr std::int64_t kReady = 76;
/// It idles first at this one: 1.5 seconds, 151 commands.
constexpr std::int64_t kFirstIdle = 151;
constexpr float kTolerance = 1e-5F;

[[nodiscard]] auto at(std::int64_t tick) -> nx::game::TickTime {
    return nx::game::TickTime{.now = nx::GameTime::atTick(tick)};
}

/// A sound of `file` alone, on `channel`, at full volume.
[[nodiscard]] auto sound(std::string file, cs::SoundChannel channel) -> cs::GameSound {
    return cs::GameSound{.files = {std::move(file)}, .channel = channel};
}

[[nodiscard]] auto weapon(std::string id, WeaponSlot slot, std::uint32_t weight, float maxSpeed,
                          std::optional<cs::WeaponMagazine> magazine, cs::WeaponView view) -> Weapon {
    return Weapon{.id = id,
                  .classname = "weapon_" + id,
                  .name = id,
                  .slot = slot,
                  .weight = weight,
                  .maxSpeed = nx::UnitsPerSecond{maxSpeed},
                  .magazine = std::move(magazine),
                  .models = cs::WeaponModels{.view = std::format("models/v_{}.mdl", id),
                                             .player = std::format("models/p_{}.mdl", id),
                                             .playerAnimation = "onehanded"},
                  .view = std::move(view)};
}

/// A knife, a Glock, an AK-47 and a USP, as the original's.
[[nodiscard]] auto catalog() -> WeaponCatalog {
    const cs::AmmoTypes ammo{.ammo = {cs::AmmoType{.id = "9mm", .maxCarried = 120, .boxRounds = 30},
                                      cs::AmmoType{.id = "762Nato", .maxCarried = 90, .boxRounds = 30},
                                      cs::AmmoType{.id = "45ACP", .maxCarried = 100, .boxRounds = 12}}};
    Weapon knife = weapon("knife", WeaponSlot::Knife, 0, 250.0F, std::nullopt,
                          {.drawSequences = {3},
                           .idles = {WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                           .idlesWhenEmpty = true,
                           .drawSound = sound("weapons/knife_deploy1.wav", cs::SoundChannel::Item)});
    knife.melee =
        cs::WeaponMelee{.slash = {.reach = nx::HammerUnits{48.0F},
                                  .missSequences = {6, 7},
                                  .hitSequences = {6, 7},
                                  .miss = {.primary = nx::Seconds{0.35F}, .secondary = nx::Seconds{0.5F}},
                                  .hit = {.primary = nx::Seconds{0.4F}, .secondary = nx::Seconds{0.5F}},
                                  .idleAfter = nx::Seconds{2.0F},
                                  .missSound = sound("weapons/knife_slash1.wav", cs::SoundChannel::Weapon),
                                  .hitSound = sound("weapons/knife_hitwall1.wav", cs::SoundChannel::Item)},
                        .stab = {.reach = nx::HammerUnits{32.0F},
                                 .missSequences = {5},
                                 .hitSequences = {4},
                                 .miss = {.primary = nx::Seconds{1.0F}, .secondary = nx::Seconds{1.0F}},
                                 .hit = {.primary = nx::Seconds{1.1F}, .secondary = nx::Seconds{1.1F}}}};
    Weapon glock =
        weapon("glock18", WeaponSlot::Secondary, 5, 250.0F, cs::WeaponMagazine{.ammo = "9mm", .clipSize = 20},
               {.drawSequences = {8, 11},
                .idles = {WeaponIdle{.sequence = 2, .wait = nx::Seconds{3.0625F}, .share = 0.3F},
                          WeaponIdle{.sequence = 0, .wait = nx::Seconds{3.75F}, .share = 0.3F},
                          WeaponIdle{.sequence = 1, .wait = nx::Seconds{2.5F}, .share = 0.4F}},
                .idlesWhenEmpty = false});
    glock.firing =
        cs::WeaponFiring{.cycle = nx::Seconds{0.15F},
                         .isAutomatic = false,
                         .idleAfterShot = nx::Seconds{2.5F},
                         .shootSequences = {3, 4, 5},
                         .lastShotSequence = 6,
                         .accuracy = {.initial = 0.9F,
                                      .paced = cs::PacedAccuracy{.threshold = nx::Seconds{0.325F},
                                                                 .rate = 0.275F,
                                                                 .minimum = 0.6F,
                                                                 .maximum = 0.9F}},
                         .spread = {.air = {.scale = 1.0F},
                                    .moving = {.scale = 0.165F},
                                    .ducked = SpreadTerm{.scale = 0.075F},
                                    .standing = {.scale = 0.1F}},
                         .sound = sound("weapons/glock18-2.wav", cs::SoundChannel::Weapon),
                         .emptySound = sound("weapons/dryfire_pistol.wav", cs::SoundChannel::Weapon)};
    glock.reload = cs::WeaponReload{.time = nx::Seconds{2.2F}, .sequences = {7, 12}};
    cs::WeaponFiring burst = *glock.firing;
    burst.cycle = nx::Seconds{0.5F};
    burst.isAutomatic = true;
    burst.spread = {.air = {.scale = 1.2F},
                    .moving = {.scale = 0.185F},
                    .ducked = SpreadTerm{.scale = 0.095F},
                    .standing = {.scale = 0.3F}};
    burst.followUps = cs::FollowUps{.rounds = 2, .spread = 0.05F};
    glock.alternate = cs::WeaponAlternate{.switchTime = nx::Seconds{0.3F}, .firing = burst};
    Weapon ak =
        weapon("ak47", WeaponSlot::Primary, 25, 221.0F, cs::WeaponMagazine{.ammo = "762Nato", .clipSize = 30},
               {.drawSequences = {2},
                .idles = {WeaponIdle{.sequence = 0, .wait = nx::Seconds{20.0F}, .share = 1.0F}},
                .idlesWhenEmpty = true});
    ak.firing = cs::WeaponFiring{
        .cycle = nx::Seconds{0.0955F},
        .isAutomatic = true,
        .idleAfterShot = nx::Seconds{1.9F},
        .shootSequences = {3, 4, 5},
        .accuracy = {.initial = 0.2F,
                     .burst = cs::BurstAccuracy{.divisor = 200, .offset = 0.35F, .maximum = 1.25F}},
        .spread = {.movingAbove = nx::UnitsPerSecond{140.0F},
                   .air = {.base = 0.04F, .scale = 0.4F},
                   .moving = {.base = 0.04F, .scale = 0.07F},
                   .standing = {.scale = 0.0275F}},
        .recoil = {.kicks =
                       cs::KickByMovement{.moving = {1.5F, 0.45F, 0.225F, 0.05F, 6.5F, 2.5F, 7},
                                          .air = {2.0F, 1.0F, 0.5F, 0.35F, 9.0F, 6.0F, 5},
                                          .ducked = {0.9F, 0.35F, 0.15F, 0.025F, 5.5F, 1.5F, 9},
                                          .standing = {1.0F, 0.375F, 0.175F, 0.0375F, 5.75F, 1.75F, 8}}}};
    ak.reload = cs::WeaponReload{.time = nx::Seconds{2.45F}, .sequences = {1}};
    Weapon usp =
        weapon("usp", WeaponSlot::Secondary, 5, 250.0F, cs::WeaponMagazine{.ammo = "45ACP", .clipSize = 12},
               {.drawSequences = {14},
                .idles = {WeaponIdle{.sequence = 8, .wait = nx::Seconds{60.0F}, .share = 1.0F}},
                .idlesWhenEmpty = false});
    usp.position = 4;
    usp.firing = cs::WeaponFiring{
        .cycle = nx::Seconds{0.15F},
        .isAutomatic = false,
        .idleAfterShot = nx::Seconds{2.0F},
        .shootSequences = {9, 10, 11},
        .lastShotSequence = 12,
        .accuracy = {.initial = 0.92F,
                     .paced = cs::PacedAccuracy{.threshold = nx::Seconds{0.3F},
                                                .rate = 0.275F,
                                                .minimum = 0.6F,
                                                .maximum = 0.92F}},
        .spread = {.air = {.scale = 1.2F}, .moving = {.scale = 0.225F}, .standing = {.scale = 0.1F}},
        .recoil = {.punch = nx::Degrees{2.0F}}};
    usp.reload = cs::WeaponReload{.time = nx::Seconds{2.7F}, .sequences = {13}};
    cs::WeaponFiring silenced = *usp.firing;
    silenced.shootSequences = {1, 2, 3};
    silenced.lastShotSequence = 4;
    silenced.spread.standing = {.scale = 0.15F};
    usp.alternate = cs::WeaponAlternate{
        .switchTime = nx::Seconds{3.0F},
        .onSequence = 7,
        .offSequence = 15,
        .view =
            cs::WeaponView{.drawSequences = {6},
                           .idles = {WeaponIdle{.sequence = 0, .wait = nx::Seconds{60.0F}, .share = 1.0F}}},
        .firing = silenced,
        .reload = cs::WeaponReload{.time = nx::Seconds{2.7F}, .sequences = {5}}};
    auto made =
        WeaponCatalog::make(ammo, {std::move(knife), std::move(glock), std::move(ak), std::move(usp)});
    EXPECT_TRUE(made.has_value()) << (made ? "" : nx::describe(made.error()));
    return std::move(made).value();
}

/// The command `number`, holding `held`, selecting `select`.
[[nodiscard]] auto command(std::uint32_t number, std::initializer_list<Button> held = {},
                           std::optional<WeaponIndex> select = std::nullopt) -> UserCommand {
    UserCommand made{.number = CommandNumber{number}, .duration = kCommandDuration, .weaponSelect = select};
    for (const Button button : held) {
        made.buttons.set(button);
    }
    return made;
}

/// A tracer that meets nothing.
[[nodiscard]] auto missAll() -> cs::WeaponTracer {
    return [](nx::collision::Hull, nx::Vec3, nx::Vec3) {
        return nx::collision::Trace{};
    };
}

/// A tracer that meets something halfway, whatever the hull.
[[nodiscard]] auto hitAll() -> cs::WeaponTracer {
    return [](nx::collision::Hull, nx::Vec3, nx::Vec3) {
        return nx::collision::Trace{.fraction = 0.5F};
    };
}

/// A shooter standing still on the ground.
[[nodiscard]] auto standing() -> Shooter {
    return Shooter{.isOnGround = true};
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

/// The shots of the frames from `first` to `last`, each command numbered by its tick, holding `held`.
auto frames(Inventory& inventory, Shooter& shooter, std::int64_t first, std::int64_t last,
            std::initializer_list<Button> held = {}) -> std::vector<Shot> {
    std::vector<Shot> shots;
    for (std::int64_t tick = first; tick <= last; ++tick) {
        std::ranges::move(
            inventory.frame(command(static_cast<std::uint32_t>(tick), held), shooter, missAll(), at(tick)),
            std::back_inserter(shots));
    }
    return shots;
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
    EXPECT_EQ(inventory.held()->accuracy, 0.9F);
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
    EXPECT_EQ(inventory.giveAmmo("50AE", 24), 0U) << "a kind of ammo the catalog lacks";
    EXPECT_EQ(inventory.reserve("50AE"), 0U);
}

TEST(InventoryTest, ACommandDrawsTheWeaponItSelectsAndLastinvTheOneBefore) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kKnife, kGlock});

    inventory.select(command(1, {}, kKnife), at(5));
    EXPECT_EQ(inventory.held()->weapon, kKnife);
    EXPECT_EQ(inventory.animation()->sequence, 3U);
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(5));
    EXPECT_EQ(inventory.lastWeapon(), kGlock);

    // A weapon the player lacks, or the one it holds, is not drawn again.
    inventory.select(command(2, {}, kAk), at(6));
    inventory.select(command(3, {}, kKnife), at(7));
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
    Shooter shooter = standing();
    const std::int64_t wait = 2000; // 20 seconds

    std::ignore = frames(inventory, shooter, 0, kFirstIdle - 1);
    EXPECT_EQ(inventory.animation()->sequence, 3U); // still drawing
    std::ignore = frames(inventory, shooter, kFirstIdle, kFirstIdle);
    EXPECT_EQ(inventory.animation()->sequence, 0U);
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(kFirstIdle));
    std::ignore = frames(inventory, shooter, kFirstIdle + 1, kFirstIdle + wait - 1);
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(kFirstIdle));
    std::ignore = frames(inventory, shooter, kFirstIdle + wait, kFirstIdle + wait);
    EXPECT_EQ(inventory.animation()->start, nx::GameTime::atTick(kFirstIdle + wait));
}

TEST(InventoryTest, IdlesAreChosenByTheirSharesTheSameForTheSameCommand) {
    const WeaponCatalog weapons = catalog();
    std::map<std::uint32_t, int> chosen;
    constexpr std::uint32_t kCommands = 4000;
    Shooter shooter = standing();
    const auto idledBy = [&](std::uint32_t number) {
        Inventory inventory = given(weapons, {kGlock});
        std::ignore = frames(inventory, shooter, 0, kFirstIdle - 1);
        std::ignore = inventory.frame(command(number), shooter, missAll(), at(kFirstIdle));
        return inventory.animation()->sequence;
    };
    for (std::uint32_t number = 1; number <= kCommands; ++number) {
        const std::uint32_t sequence = idledBy(number);
        ++chosen[sequence];
        ASSERT_EQ(idledBy(number), sequence);
    }
    constexpr double kShareTolerance = 0.03;
    EXPECT_NEAR(chosen[2] / double{kCommands}, 0.3, kShareTolerance);
    EXPECT_NEAR(chosen[0] / double{kCommands}, 0.3, kShareTolerance);
    EXPECT_NEAR(chosen[1] / double{kCommands}, 0.4, kShareTolerance);
}

TEST(InventoryTest, EachDrawSequenceIsChosenAtRandom) {
    const WeaponCatalog weapons = catalog();
    std::set<std::uint32_t> drawn;
    for (std::uint32_t number = 1; number <= 64; ++number) {
        Inventory inventory = given(weapons, {kGlock, kKnife});
        inventory.select(command(number, {}, kKnife), at(1));
        inventory.select(command(number, {}, kGlock), at(2));
        drawn.insert(inventory.animation()->sequence);
    }
    EXPECT_EQ(drawn, (std::set<std::uint32_t>{8, 11}));
}

TEST(FiringTest, AWeaponBeingDrawnDoesNotFire) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kAk});
    Shooter shooter = standing();
    EXPECT_TRUE(frames(inventory, shooter, 0, kReady - 1, {Button::Attack}).empty());
    EXPECT_EQ(frames(inventory, shooter, kReady, kReady, {Button::Attack}).size(), 1U);
}

TEST(FiringTest, AnAutomaticGunFiresEachCycleWhileItsButtonIsDown) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kAk});
    Shooter shooter = standing();
    // 0.0955 seconds apart: a shot each tenth tick at 100 ticks a second, as the original's timers.
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    EXPECT_EQ(frames(inventory, shooter, kReady, kReady + 29, {Button::Attack}).size(), 3U);
    EXPECT_EQ(inventory.held()->clip, 27U);
    ASSERT_TRUE(inventory.animation().has_value());
    EXPECT_GE(inventory.animation()->sequence, 3U);
    EXPECT_LE(inventory.animation()->sequence, 5U);
}

TEST(FiringTest, AGunThatIsNotAutomaticFiresOnceAPress) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    EXPECT_EQ(frames(inventory, shooter, kReady, kReady + 50, {Button::Attack}).size(), 1U);
    std::ignore = frames(inventory, shooter, kReady + 51, kReady + 51); // let go
    EXPECT_EQ(frames(inventory, shooter, kReady + 52, kReady + 52, {Button::Attack}).size(), 1U);
    EXPECT_EQ(inventory.held()->clip, 18U);
    // 0.15 seconds apart, which counted down in float last 16 commands, as the original's.
    std::ignore = frames(inventory, shooter, kReady + 53, kReady + 53);
    EXPECT_TRUE(frames(inventory, shooter, kReady + 54, kReady + 67, {Button::Attack}).empty());
    EXPECT_EQ(frames(inventory, shooter, kReady + 68, kReady + 68, {Button::Attack}).size(), 1U);
}

TEST(FiringTest, TheLastRoundPlaysItsOwnSequenceAndAnEmptyClipClicks) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::int64_t tick = kReady;
    for (int shot = 0; shot < 20; ++shot) {
        ASSERT_EQ(frames(inventory, shooter, tick, tick, {Button::Attack}).size(), 1U) << shot;
        std::ignore = frames(inventory, shooter, tick + 1, tick + 19);
        tick += 20;
    }
    EXPECT_EQ(inventory.held()->clip, 0U);
    EXPECT_EQ(inventory.animation()->sequence, 6U);
    // Empty, with nothing to reload with: a click, and the next no sooner than 0.2 seconds after,
    // counted down by the click's command already.
    EXPECT_TRUE(frames(inventory, shooter, tick, tick, {Button::Attack}).empty());
    EXPECT_NEAR(inventory.held()->nextShot, 0.19F, kTolerance);
    EXPECT_FALSE(inventory.isReloading());
}

TEST(FiringTest, ABurstWorsensTheAccuracyByItsShotsCubedInWholeNumbers) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kAk});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::int64_t tick = kReady;
    // 1³ to 5³ over 200 are 0 in whole numbers; 6³ = 216 is 1: 1.35, held to 1.25.
    for (const float expected : {0.35F, 0.35F, 0.35F, 0.35F, 0.35F, 1.25F}) {
        ASSERT_EQ(frames(inventory, shooter, tick, tick + 9, {Button::Attack}).size(), 1U);
        EXPECT_FLOAT_EQ(inventory.held()->accuracy, expected);
        tick += 10;
    }
    // Let go, the burst keeps its shots for 0.4 seconds, then loses one every 0.0225.
    std::ignore = frames(inventory, shooter, tick, tick + 40);
    EXPECT_EQ(inventory.held()->shotsFired, 6U);
    std::ignore = frames(inventory, shooter, tick + 41, tick + 45);
    EXPECT_LT(inventory.held()->shotsFired, 6U);
}

TEST(FiringTest, APacedAccuracyRecoversWithTheTimeBetweenShots) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::ignore = frames(inventory, shooter, kReady, kReady, {Button::Attack});
    EXPECT_FLOAT_EQ(inventory.held()->accuracy, 0.9F) << "the first shot changes nothing";
    std::ignore = frames(inventory, shooter, kReady + 1, kReady + 15);
    std::ignore = frames(inventory, shooter, kReady + 16, kReady + 16, {Button::Attack});
    EXPECT_NEAR(inventory.held()->accuracy, 0.9F - ((0.325F - 0.16F) * 0.275F), kTolerance);
    std::ignore = frames(inventory, shooter, kReady + 17, kReady + 115);
    std::ignore = frames(inventory, shooter, kReady + 116, kReady + 116, {Button::Attack});
    EXPECT_FLOAT_EQ(inventory.held()->accuracy, 0.9F) << "held to its maximum";
}

TEST(FiringTest, TheSpreadIsOfHowTheShooterMoves) {
    const WeaponCatalog weapons = catalog();
    const auto firstSpread = [&weapons](WeaponIndex index, Shooter shooter) {
        Inventory inventory = given(weapons, {index});
        std::ignore = frames(inventory, shooter, 0, kReady - 1);
        const std::vector<Shot> shots = frames(inventory, shooter, kReady, kReady, {Button::Attack});
        EXPECT_EQ(shots.size(), 1U);
        return shots.empty() ? 0.0F : shots.front().spread;
    };
    // The AK-47's first shot, of an accuracy of 0.2: in the air, moving faster than 140, else standing.
    EXPECT_NEAR(firstSpread(kAk, Shooter{}), 0.04F + (0.4F * 0.2F), kTolerance);
    EXPECT_NEAR(firstSpread(kAk, Shooter{.horizontalSpeed = nx::UnitsPerSecond{200.0F}, .isOnGround = true}),
                0.04F + (0.07F * 0.2F), kTolerance);
    EXPECT_NEAR(firstSpread(kAk, Shooter{.horizontalSpeed = nx::UnitsPerSecond{100.0F}, .isOnGround = true}),
                0.0275F * 0.2F, kTolerance);
    EXPECT_NEAR(firstSpread(kAk, Shooter{.isOnGround = true, .isDucked = true}), 0.0275F * 0.2F, kTolerance);
    // The Glock's, of an accuracy of 0.9, by 1 less it: moving at all, ducked, standing.
    EXPECT_NEAR(firstSpread(kGlock, Shooter{.horizontalSpeed = nx::UnitsPerSecond{1.0F}, .isOnGround = true}),
                0.165F * 0.1F, kTolerance);
    EXPECT_NEAR(firstSpread(kGlock, Shooter{.isOnGround = true, .isDucked = true}), 0.075F * 0.1F,
                kTolerance);
    EXPECT_NEAR(firstSpread(kGlock, standing()), 0.1F * 0.1F, kTolerance);
}

TEST(FiringTest, AShotStraysWithinTwiceItsSpreadTheSameForTheSameCommand) {
    const WeaponCatalog weapons = catalog();
    for (std::uint32_t number = 1; number <= 200; ++number) {
        Inventory inventory = given(weapons, {kAk});
        Shooter shooter{}; // in the air: a wide spread
        std::ignore = frames(inventory, shooter, 0, kReady - 1);
        const std::vector<Shot> shots =
            inventory.frame(command(number, {Button::Attack}), shooter, missAll(), at(kReady));
        ASSERT_EQ(shots.size(), 1U);
        const nx::AngleVectors axes = nx::angleVectors(nx::EulerAngles{});
        const Shot& shot = shots.front();
        EXPECT_FLOAT_EQ(nx::dot(shot.direction, axes.forward), 1.0F);
        EXPECT_LE(std::abs(nx::dot(shot.direction, axes.right)), 2.0F * shot.spread);
        EXPECT_LE(std::abs(nx::dot(shot.direction, axes.up)), 2.0F * shot.spread);

        Inventory again = given(weapons, {kAk});
        Shooter same{};
        std::ignore = frames(again, same, 0, kReady - 1);
        const std::vector<Shot> repeated =
            again.frame(command(number, {Button::Attack}), same, missAll(), at(kReady));
        ASSERT_EQ(repeated.size(), 1U);
        EXPECT_EQ(repeated.front().direction, shot.direction);
    }
}

TEST(FiringTest, ARifleKicksTheViewUpAndAsideWithinItsLimits) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kAk});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::ignore = frames(inventory, shooter, kReady, kReady, {Button::Attack});
    EXPECT_FLOAT_EQ(shooter.punch.pitch.value(), -1.0F);
    EXPECT_FLOAT_EQ(std::abs(shooter.punch.yaw.value()), 0.375F);
    // The second shot: 2 x 0.175 + 1.0 more up.
    std::ignore = frames(inventory, shooter, kReady + 1, kReady + 10, {Button::Attack});
    EXPECT_FLOAT_EQ(shooter.punch.pitch.value(), -2.35F);
    // A long burst, the kick held to its limits (the view's own drop back is the movement's).
    std::ignore = frames(inventory, shooter, kReady + 11, kReady + 250, {Button::Attack});
    EXPECT_FLOAT_EQ(shooter.punch.pitch.value(), -5.75F);
    EXPECT_LE(std::abs(shooter.punch.yaw.value()), 1.75F);

    // The Glock does not kick.
    Inventory pistol = given(weapons, {kGlock});
    Shooter still = standing();
    std::ignore = frames(pistol, still, 0, kReady, {Button::Attack});
    EXPECT_EQ(still.punch, nx::EulerAngles{});
}

TEST(FiringTest, AReloadFillsTheClipOnceItsTimeIsOverAndAnEmptyClipIsReloadedOnItsOwn) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kAk});
    std::ignore = inventory.giveAmmo("762Nato", 90);
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady + 9, {Button::Attack});
    ASSERT_EQ(inventory.held()->clip, 29U);

    std::ignore = frames(inventory, shooter, kReady + 10, kReady + 10, {Button::Reload});
    EXPECT_TRUE(inventory.isReloading());
    EXPECT_EQ(inventory.animation()->sequence, 1U);
    EXPECT_FLOAT_EQ(inventory.held()->accuracy, 0.2F);
    const std::int64_t done = kReady + 10 + 246; // 2.45 seconds, which count down in 246 commands
    std::ignore = frames(inventory, shooter, kReady + 11, done - 1, {Button::Attack});
    EXPECT_EQ(inventory.held()->clip, 29U) << "neither firing nor reloaded meanwhile";
    std::ignore = frames(inventory, shooter, done, done);
    EXPECT_FALSE(inventory.isReloading());
    EXPECT_EQ(inventory.held()->clip, 30U);
    EXPECT_EQ(inventory.reserve("762Nato"), 89U);

    // A full clip is not reloaded.
    std::ignore = frames(inventory, shooter, done + 1, done + 1, {Button::Reload});
    EXPECT_FALSE(inventory.isReloading());

    // An empty clip, once the gun may fire again and no fire button is down, is reloaded.
    std::int64_t tick = done + 2;
    while (inventory.held()->clip > 0) {
        std::ignore = frames(inventory, shooter, tick, tick, {Button::Attack});
        ++tick;
    }
    std::ignore = frames(inventory, shooter, tick, tick + 20);
    EXPECT_TRUE(inventory.isReloading());
}

TEST(AlternateTest, TheUspsSilencerGoesOnHoldingItsFireAndIdleAsLong) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kKnife, kUsp});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::ignore = frames(inventory, shooter, kReady, kReady, {Button::Attack2});
    EXPECT_TRUE(inventory.held()->isAlternate);
    EXPECT_EQ(inventory.animation()->sequence, 7U);
    // 3 seconds, 301 commands: neither firing nor switching back meanwhile.
    EXPECT_TRUE(frames(inventory, shooter, kReady + 1, kReady + 300, {Button::Attack}).empty());
    std::ignore = frames(inventory, shooter, kReady + 301, kReady + 301);
    const std::vector<Shot> shots = frames(inventory, shooter, kReady + 302, kReady + 302, {Button::Attack});
    ASSERT_EQ(shots.size(), 1U);
    EXPECT_NEAR(shots.front().spread, 0.15F * (1.0F - 0.92F), kTolerance) << "the silenced spread";
    EXPECT_GE(inventory.animation()->sequence, 1U);
    EXPECT_LE(inventory.animation()->sequence, 3U);

    // Put away and drawn again, it is drawn with its silencer on.
    inventory.select(command(1, {}, kKnife), at(kReady + 303));
    inventory.select(command(2, {}, kUsp), at(kReady + 304));
    EXPECT_TRUE(inventory.held()->isAlternate);
    EXPECT_EQ(inventory.animation()->sequence, 6U);

    std::ignore = frames(inventory, shooter, kReady + 304, kReady + 304 + 75);
    std::ignore = frames(inventory, shooter, kReady + 380, kReady + 380, {Button::Attack2});
    EXPECT_FALSE(inventory.held()->isAlternate);
    EXPECT_EQ(inventory.animation()->sequence, 15U);
}

TEST(AlternateTest, HoldingAttack2SwitchesOnceItsTimeIsOver) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::ignore = frames(inventory, shooter, kReady, kReady + 30, {Button::Attack2});
    EXPECT_TRUE(inventory.held()->isAlternate) << "0.3 seconds, 31 commands, before it switches back";
    std::ignore = frames(inventory, shooter, kReady + 31, kReady + 31, {Button::Attack2});
    EXPECT_FALSE(inventory.held()->isAlternate);
}

TEST(AlternateTest, TheGlocksBurstFiresTheRestOfItsRoundsOneAFrame) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::ignore = frames(inventory, shooter, kReady, kReady, {Button::Attack2});
    ASSERT_TRUE(inventory.held()->isAlternate);

    const std::vector<Shot> first = frames(inventory, shooter, kReady + 1, kReady + 1, {Button::Attack});
    ASSERT_EQ(first.size(), 1U);
    EXPECT_NEAR(first.front().spread, 0.3F * (1.0F - 0.9F), kTolerance) << "the burst's spread";
    const std::vector<Shot> rest = frames(inventory, shooter, kReady + 2, kReady + 4);
    ASSERT_EQ(rest.size(), 2U);
    EXPECT_FLOAT_EQ(rest[0].spread, 0.05F);
    EXPECT_EQ(inventory.held()->clip, 17U);

    // Held, it fires the next burst 0.5 seconds after, 51 commands: the burst is automatic.
    EXPECT_EQ(frames(inventory, shooter, kReady + 5, kReady + 51, {Button::Attack}).size(), 0U);
    EXPECT_EQ(frames(inventory, shooter, kReady + 52, kReady + 52, {Button::Attack}).size(), 1U);
}

TEST(AlternateTest, AWeaponWithoutAnAlternateIgnoresAttack2) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kAk});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kFirstIdle, {Button::Attack2});
    EXPECT_FALSE(inventory.held()->isAlternate);
    EXPECT_EQ(inventory.animation()->sequence, 0U) << "idling, as with no fire button down";
}

TEST(MeleeTest, TheKnifeSlashesItsSequencesInTurnHeldByItsMissOrHit) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kKnife});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);

    std::ignore = inventory.frame(command(1, {Button::Attack}), shooter, missAll(), at(kReady));
    EXPECT_EQ(inventory.animation()->sequence, 6U);
    // A miss holds a slash 0.35 seconds and a stab 0.5, already counted down by its command.
    EXPECT_NEAR(inventory.held()->nextShot, 0.34F, kTolerance);
    EXPECT_NEAR(inventory.held()->nextSecondary, 0.49F, kTolerance);

    std::ignore = frames(inventory, shooter, kReady + 1, kReady + 40);
    std::ignore = inventory.frame(command(2, {Button::Attack}), shooter, hitAll(), at(kReady + 41));
    EXPECT_EQ(inventory.animation()->sequence, 7U) << "the next of the two";
    EXPECT_NEAR(inventory.held()->nextShot, 0.39F, kTolerance) << "a hit holds it 0.4";
}

TEST(MeleeTest, TheKnifeStabsWithAttack2ItsMissOrHitSequence) {
    const WeaponCatalog weapons = catalog();
    for (const bool hits : {false, true}) {
        Inventory inventory = given(weapons, {kKnife});
        Shooter shooter = standing();
        std::ignore = frames(inventory, shooter, 0, kReady - 1);
        std::ignore =
            inventory.frame(command(1, {Button::Attack2}), shooter, hits ? hitAll() : missAll(), at(kReady));
        EXPECT_EQ(inventory.animation()->sequence, hits ? 4U : 5U);
        EXPECT_NEAR(inventory.held()->nextShot, hits ? 1.09F : 0.99F, kTolerance);
        EXPECT_NEAR(inventory.held()->nextSecondary, hits ? 1.09F : 0.99F, kTolerance);
    }
}

TEST(MeleeTest, AnAttackMeetsALineAheadThenADuckedHull) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kKnife});
    Shooter shooter{.eye = nx::Vec3{.x = 10.0F, .z = 64.0F},
                    .view = nx::EulerAngles{.yaw = nx::Degrees{90.0F}},
                    .punch = nx::EulerAngles{.pitch = nx::Degrees{-5.0F}},
                    .isOnGround = true};
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::vector<std::pair<nx::collision::Hull, nx::Vec3>> traced;
    const cs::WeaponTracer tracer = [&traced](nx::collision::Hull hull, nx::Vec3 start, nx::Vec3 end) {
        EXPECT_EQ(start, (nx::Vec3{.x = 10.0F, .z = 64.0F}));
        traced.emplace_back(hull, end);
        return nx::collision::Trace{};
    };
    std::ignore = inventory.frame(command(1, {Button::Attack}), shooter, tracer, at(kReady));
    ASSERT_EQ(traced.size(), 2U);
    EXPECT_EQ(traced[0].first, nx::collision::Hull::Point);
    EXPECT_EQ(traced[1].first, nx::collision::Hull::Ducked);
    // 48 units along the view, without its kick: straight along +y.
    EXPECT_NEAR(traced[0].second.x, 10.0F, kTolerance);
    EXPECT_NEAR(traced[0].second.y, 48.0F, kTolerance);
    EXPECT_NEAR(traced[0].second.z, 64.0F, kTolerance);
}

/// The first file of each sound `inventory` made since the last call, in their order.
[[nodiscard]] auto soundsOf(Inventory& inventory) -> std::vector<std::string> {
    std::vector<std::string> files;
    for (const cs::SoundMade& made : inventory.takeSounds()) {
        files.push_back(made.sound->files.front());
    }
    return files;
}

using Files = std::vector<std::string>;

TEST(SoundTest, EachSoundTakenCarriesTheTickItWasMadeAt) {
    // Many ticks before the sounds are taken, as a long frame of the client runs them: each shot keeps
    // the moment of its own tick.
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::ignore = inventory.takeSounds();
    for (const std::int64_t press : {kReady, kReady + 20, kReady + 40}) {
        std::ignore = frames(inventory, shooter, press, press, {Button::Attack});
        std::ignore = frames(inventory, shooter, press + 1, press + 19);
    }
    const std::vector<cs::SoundMade> made = inventory.takeSounds();
    ASSERT_EQ(made.size(), 3U);
    EXPECT_EQ(made[0].at, nx::GameTime::atTick(kReady));
    EXPECT_EQ(made[1].at, nx::GameTime::atTick(kReady + 20));
    EXPECT_EQ(made[2].at, nx::GameTime::atTick(kReady + 40));
}

TEST(SoundTest, AWeaponDrawnMakesItsDrawSoundAndTheSoundsAreTakenOnce) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kKnife});
    EXPECT_EQ(soundsOf(inventory), Files{"weapons/knife_deploy1.wav"});
    EXPECT_EQ(soundsOf(inventory), Files{});
    nx::Random random{0, "test"};
    EXPECT_TRUE(inventory.give(kGlock, true, at(1), random));
    EXPECT_EQ(soundsOf(inventory), Files{}) << "the Glock's view model plays its own";
}

TEST(SoundTest, AGunSoundsEachRoundItFiresAndClicksEmpty) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::int64_t tick = kReady;
    for (int shot = 0; shot < 20; ++shot) {
        std::ignore = frames(inventory, shooter, tick, tick, {Button::Attack});
        ASSERT_EQ(soundsOf(inventory), Files{"weapons/glock18-2.wav"}) << shot;
        std::ignore = frames(inventory, shooter, tick + 1, tick + 19);
        tick += 20;
    }
    std::ignore = frames(inventory, shooter, tick, tick, {Button::Attack});
    EXPECT_EQ(soundsOf(inventory), Files{"weapons/dryfire_pistol.wav"});
    // Held, a pistol clicks once a press; pressed again after its 0.2 seconds, it clicks again.
    std::ignore = frames(inventory, shooter, tick + 1, tick + 20, {Button::Attack});
    EXPECT_EQ(soundsOf(inventory), Files{});
    std::ignore = frames(inventory, shooter, tick + 21, tick + 21);
    std::ignore = frames(inventory, shooter, tick + 22, tick + 22, {Button::Attack});
    EXPECT_EQ(soundsOf(inventory), Files{"weapons/dryfire_pistol.wav"});
}

TEST(SoundTest, EachRoundOfTheGlocksBurstSounds) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kGlock});
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady - 1);
    std::ignore = frames(inventory, shooter, kReady, kReady, {Button::Attack2});
    std::ignore = frames(inventory, shooter, kReady + 1, kReady + 1, {Button::Attack});
    std::ignore = frames(inventory, shooter, kReady + 2, kReady + 4);
    EXPECT_EQ(soundsOf(inventory), Files(3, "weapons/glock18-2.wav"));
}

TEST(SoundTest, TheKnifeSoundsItsMissOrItsHitOnTheWorld) {
    const WeaponCatalog weapons = catalog();
    for (const bool hits : {false, true}) {
        Inventory inventory = given(weapons, {kKnife});
        Shooter shooter = standing();
        std::ignore = frames(inventory, shooter, 0, kReady - 1);
        std::ignore = soundsOf(inventory);
        std::ignore =
            inventory.frame(command(1, {Button::Attack}), shooter, hits ? hitAll() : missAll(), at(kReady));
        EXPECT_EQ(soundsOf(inventory),
                  Files{hits ? "weapons/knife_hitwall1.wav" : "weapons/knife_slash1.wav"});
    }
}

TEST(FiringTest, DrawingAnotherWeaponStopsAReload) {
    const WeaponCatalog weapons = catalog();
    Inventory inventory = given(weapons, {kKnife, kAk});
    std::ignore = inventory.giveAmmo("762Nato", 90);
    Shooter shooter = standing();
    std::ignore = frames(inventory, shooter, 0, kReady, {Button::Attack});
    std::ignore = frames(inventory, shooter, kReady + 1, kReady + 19);
    std::ignore = frames(inventory, shooter, kReady + 20, kReady + 20, {Button::Reload});
    ASSERT_TRUE(inventory.isReloading());
    inventory.select(command(1, {}, kKnife), at(kReady + 21));
    EXPECT_FALSE(inventory.isReloading());
    inventory.select(command(2, {}, kAk), at(kReady + 22));
    std::ignore = frames(inventory, shooter, kReady + 400, kReady + 400);
    EXPECT_EQ(inventory.held()->clip, 29U);
}

} // namespace
