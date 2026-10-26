// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The repository's data/cstrike/breath.toml, and a player's breath in water as the original's
// CBasePlayer::WaterMove has it: the gasps as it comes up for air, after a while under water and once
// its air ran out, the air whole again with its head out, held while it noclips, the bubbles with its
// head under, the likelier the less air it has left, its drowning once its air ran out, what drowning
// took given back as it breathes again, and the harm of slime and lava.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.collision;
import nx.game;
import nx.platform;
import cs.shared;

namespace {

using cs::Breath;
using cs::BreathEvent;
using cs::BreathRules;
using cs::HarmKind;
using cs::MoveType;
using cs::PlayerHarm;
using cs::PlayerHarmRules;
using cs::SoundMade;
using cs::TimedHarms;
using cs::WaterLevel;
using nx::collision::Contents;

/// The harm of drowning and harmful water, the original's.
const PlayerHarmRules kHarm{};

/// The repository's data/cstrike/breath.toml, read.
[[nodiscard]] auto repositoryRules() -> BreathRules {
    const std::filesystem::path file = std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "breath.toml";
    auto text = nx::platform::readTextFile(file);
    EXPECT_TRUE(text.has_value());
    auto rules = cs::readBreathRules(text.value_or(std::string{}), "breath.toml");
    EXPECT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    return rules.value_or(BreathRules{});
}

/// The moment `seconds` after the map started.
[[nodiscard]] auto at(float seconds) -> nx::game::TickTime {
    return nx::game::TickTime{.now = nx::GameTime{}.after(nx::Seconds{seconds}, nx::kDefaultTickRate)};
}

/// The first sound among `events`, when there is one.
[[nodiscard]] auto firstSound(const std::vector<BreathEvent>& events) -> std::optional<SoundMade> {
    for (const BreathEvent& event : events) {
        if (const auto* const sound = std::get_if<SoundMade>(&event)) {
            return *sound;
        }
    }
    return std::nullopt;
}

/// The harms among `events`, in their order.
[[nodiscard]] auto harmsOf(const std::vector<BreathEvent>& events) -> std::vector<PlayerHarm> {
    std::vector<PlayerHarm> harms;
    for (const BreathEvent& event : events) {
        if (const auto* const harm = std::get_if<PlayerHarm>(&event)) {
            harms.push_back(*harm);
        }
    }
    return harms;
}

/// What a breath at `seconds` did, in `contents` to `level`, walking, its kept harms in `timed`.
[[nodiscard]] auto breathAt(Breath& breath, TimedHarms& timed, float seconds, WaterLevel level,
                            Contents contents = Contents::Water) -> std::vector<BreathEvent> {
    nx::Random random{0, "breath"};
    return breath.breathe(level, contents, MoveType::Walk, timed, at(seconds), random);
}

/// The sound of a breath at `seconds`, in water to `level`, walking.
[[nodiscard]] auto breatheAt(Breath& breath, float seconds, WaterLevel level) -> std::optional<SoundMade> {
    TimedHarms timed;
    return firstSound(breathAt(breath, timed, seconds, level));
}

/// A harm of `damage` and `kind`.
[[nodiscard]] auto harm(float damage, HarmKind kind) -> PlayerHarm {
    return PlayerHarm{.damage = nx::Damage{damage}, .kind = kind};
}

/// The sound `made` played, when one was made.
[[nodiscard]] auto soundOf(const std::optional<SoundMade>& made) -> const cs::GameSound* {
    return made ? made->sound : nullptr;
}

/// How many of `breaths` breaths, each of its own command, bubble with the head under water at
/// `seconds`, the air whole at the map's start.
[[nodiscard]] auto bubblesAt(const BreathRules& rules, float seconds, std::uint64_t breaths)
    -> std::uint64_t {
    std::uint64_t bubbles = 0;
    for (std::uint64_t command = 0; command < breaths; ++command) {
        Breath breath{rules, kHarm};
        breath.fill(at(0.0F));
        TimedHarms timed;
        nx::Random random{command, "breath"};
        const std::optional<SoundMade> made = firstSound(
            breath.breathe(WaterLevel::Eyes, Contents::Water, MoveType::Walk, timed, at(seconds), random));
        if (made && made->sound == &rules.bubbles) {
            ++bubbles;
        }
    }
    return bubbles;
}

TEST(BreathRulesTest, TheRepositorysBreathIsTheOriginals) {
    const BreathRules rules = repositoryRules();
    EXPECT_EQ(rules.shortGasp.files, (std::vector<std::string>{"player/pl_wade2.wav"}));
    EXPECT_EQ(rules.shortGasp.channel, cs::SoundChannel::Voice);
    EXPECT_EQ(rules.shortGasp.volume, 1.0F);
    EXPECT_EQ(rules.longGasp.files, (std::vector<std::string>{"player/pl_wade1.wav"}));
    EXPECT_EQ(rules.longGasp.channel, cs::SoundChannel::Voice);
    EXPECT_EQ(rules.bubbles.files, (std::vector<std::string>{"player/pl_swim1.wav", "player/pl_swim2.wav",
                                                             "player/pl_swim3.wav", "player/pl_swim4.wav"}));
    EXPECT_EQ(rules.bubbles.channel, cs::SoundChannel::Body);
    EXPECT_EQ(rules.bubbles.volume, 0.8F);
    EXPECT_EQ(rules.soundFiles().size(), 6U);
}

TEST(BreathRulesTest, ASoundWithoutAFileIsRefused) {
    const auto rules = cs::readBreathRules(R"(shortGasp = { files = ["a.wav"], channel = "voice" }
longGasp = { files = ["b.wav"], channel = "voice" }
bubbles = { files = [], channel = "body" }
)",
                                           "breath.toml");
    ASSERT_FALSE(rules.has_value());
    EXPECT_EQ(rules.error().code, cs::BreathError::DataInvalid);
}

TEST(BreathTest, ComingUpAfterAWhileUnderWaterGasps) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    EXPECT_EQ(soundOf(breatheAt(breath, 3.0F, WaterLevel::Waist)), nullptr)
        << "9 seconds of the air left are not less than 9";
    breath.fill(at(0.0F));
    const std::optional<SoundMade> gasp = breatheAt(breath, 3.5F, WaterLevel::Waist);
    EXPECT_EQ(soundOf(gasp), &rules.shortGasp);
    ASSERT_TRUE(gasp.has_value());
    EXPECT_EQ(gasp->volume, 1.0F);
    EXPECT_EQ(gasp->at, at(3.5F).now);
    EXPECT_EQ(soundOf(breatheAt(breath, 3.51F, WaterLevel::Waist)), nullptr) << "its air is whole again";
}

TEST(BreathTest, ComingUpOnceTheAirRanOutGaspsLong) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    std::ignore = breatheAt(breath, 6.0F, WaterLevel::Eyes);
    EXPECT_EQ(soundOf(breatheAt(breath, 12.0F, WaterLevel::Feet)), &rules.shortGasp)
        << "its air runs out after 12 seconds, not at them";
    breath.fill(at(0.0F));
    EXPECT_EQ(soundOf(breatheAt(breath, 12.5F, WaterLevel::Dry)), &rules.longGasp);
    EXPECT_EQ(soundOf(breatheAt(breath, 12.51F, WaterLevel::Dry)), nullptr);
}

TEST(BreathTest, TheAirIsWholeWhileTheHeadIsOutOfWater) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    for (int second = 1; second <= 30; ++second) {
        EXPECT_EQ(soundOf(breatheAt(breath, static_cast<float>(second), WaterLevel::Waist)), nullptr)
            << second;
    }
    EXPECT_EQ(soundOf(breatheAt(breath, 33.5F, WaterLevel::Dry)), &rules.shortGasp)
        << "its air was whole at 30 seconds";
}

TEST(BreathTest, ANoclippingPlayerHoldsItsBreathAsItWas) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    nx::Random random{0, "breath"};
    TimedHarms timed;
    EXPECT_TRUE(
        breath.breathe(WaterLevel::Dry, Contents::Empty, MoveType::Noclip, timed, at(20.0F), random).empty());
    EXPECT_EQ(soundOf(breatheAt(breath, 20.0F, WaterLevel::Dry)), &rules.longGasp)
        << "the air ran out while it noclipped";
}

TEST(BreathTest, UnderWaterItBubblesTheLikelierTheLessAirIsLeft) {
    const BreathRules rules = repositoryRules();
    constexpr std::uint64_t kBreaths = 38'400;
    EXPECT_EQ(bubblesAt(rules, 0.0F, kBreaths), 0U) << "with its air whole, none of its 0 to 11 is 12";
    // One in 32, then one in 12: an 11 drawn against 11 seconds left.
    const std::uint64_t withAir = bubblesAt(rules, 0.5F, kBreaths);
    EXPECT_GT(withAir, 50U);
    EXPECT_LT(withAir, 160U);
    // One in 32, then any of 0 to 11 against 0 seconds left.
    const std::uint64_t withoutAir = bubblesAt(rules, 12.5F, kBreaths);
    EXPECT_GT(withoutAir, 1'050U);
    EXPECT_LT(withoutAir, 1'350U);
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    std::optional<SoundMade> bubble;
    TimedHarms timed;
    for (std::uint64_t command = 0; !bubble && command < kBreaths; ++command) {
        nx::Random random{command, "breath"};
        bubble = firstSound(
            breath.breathe(WaterLevel::Eyes, Contents::Water, MoveType::Walk, timed, at(12.5F), random));
    }
    ASSERT_TRUE(bubble.has_value());
    EXPECT_EQ(bubble->sound, &rules.bubbles);
    EXPECT_EQ(bubble->volume, 0.8F);
    EXPECT_EQ(bubble->at, at(12.5F).now);
}

TEST(BreathTest, WithItsHeadOutOfWaterItDoesNotBubble) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    for (std::uint64_t command = 0; command < 4'000; ++command) {
        nx::Random random{command, "breath"};
        const float seconds = 1.0F + (static_cast<float>(command) * 0.01F);
        TimedHarms timed;
        EXPECT_TRUE(
            breath.breathe(WaterLevel::Waist, Contents::Water, MoveType::Walk, timed, at(seconds), random)
                .empty())
            << seconds;
    }
}

TEST(BreathTest, OnceItsAirRanOutItDrownsASecondApartEachHarmAStepMoreUpToFive) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    TimedHarms timed;
    EXPECT_TRUE(harmsOf(breathAt(breath, timed, 12.0F, WaterLevel::Eyes)).empty())
        << "its air runs out after 12";
    std::vector<PlayerHarm> harms;
    for (int command = 1201; command <= 1503; ++command) {
        std::ranges::copy(
            harmsOf(breathAt(breath, timed, static_cast<float>(command) / 100.0F, WaterLevel::Eyes)),
            std::back_inserter(harms));
    }
    // At 12.01, then a second and a hundredth apart, as pain_finished is waited past.
    EXPECT_EQ(harms, (std::vector<PlayerHarm>{harm(3.0F, HarmKind::Drown), harm(4.0F, HarmKind::Drown),
                                              harm(5.0F, HarmKind::Drown)}));
    EXPECT_EQ(harmsOf(breathAt(breath, timed, 15.04F, WaterLevel::Eyes)),
              (std::vector<PlayerHarm>{harm(5.0F, HarmKind::Drown)}))
        << "five at most";
}

TEST(BreathTest, ComingUpGivesBackWhatDrowningTookAndStartsItsHarmOver) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    TimedHarms timed;
    for (const float seconds : {12.01F, 13.02F, 14.03F, 15.04F}) {
        for (const PlayerHarm& harm : harmsOf(breathAt(breath, timed, seconds, WaterLevel::Eyes))) {
            timed.took(harm.kind);
        }
    }
    // It took 3, 4, 5 and 5: given back 10 at a time, at least two seconds apart.
    EXPECT_EQ(soundOf(firstSound(breathAt(breath, timed, 15.5F, WaterLevel::Waist))), &rules.longGasp);
    EXPECT_EQ(timed.check(kHarm.drowning.givenBack, at(15.5F).now, nx::kDefaultTickRate),
              nx::HitPoints{10.0F});
    std::ignore = breathAt(breath, timed, 16.0F, WaterLevel::Waist);
    EXPECT_EQ(timed.check(kHarm.drowning.givenBack, at(16.0F).now, nx::kDefaultTickRate), nx::HitPoints{})
        << "half a second after";
    std::ignore = breathAt(breath, timed, 17.5F, WaterLevel::Waist);
    EXPECT_EQ(timed.check(kHarm.drowning.givenBack, at(17.5F).now, nx::kDefaultTickRate),
              nx::HitPoints{7.0F});
    // Its air whole as it came up, it drowns again at 3 once that runs out.
    EXPECT_TRUE(harmsOf(breathAt(breath, timed, 29.5F, WaterLevel::Eyes)).empty());
    EXPECT_EQ(harmsOf(breathAt(breath, timed, 29.51F, WaterLevel::Eyes)),
              (std::vector<PlayerHarm>{harm(3.0F, HarmKind::Drown)}));
}

TEST(BreathTest, SlimeAndLavaHurtEachCommandByHowDeepThePlayerIs) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    TimedHarms timed;
    EXPECT_EQ(harmsOf(breathAt(breath, timed, 1.0F, WaterLevel::Feet, Contents::Lava)),
              (std::vector<PlayerHarm>{harm(10.0F, HarmKind::Burn)}));
    EXPECT_EQ(harmsOf(breathAt(breath, timed, 1.01F, WaterLevel::Eyes, Contents::Lava)),
              (std::vector<PlayerHarm>{harm(30.0F, HarmKind::Burn)}))
        << "each command";
    EXPECT_EQ(harmsOf(breathAt(breath, timed, 1.02F, WaterLevel::Waist, Contents::Slime)),
              (std::vector<PlayerHarm>{harm(8.0F, HarmKind::Acid)}));
    EXPECT_EQ(harmsOf(breathAt(breath, timed, 1.03F, WaterLevel::Feet, Contents::Slime)),
              (std::vector<PlayerHarm>{harm(4.0F, HarmKind::Acid)}));
    EXPECT_TRUE(harmsOf(breathAt(breath, timed, 1.04F, WaterLevel::Waist, Contents::Water)).empty());
}

TEST(BreathTest, LavaSparesThePlayerASecondAfterSlimeUntilItGoesIntoWaterAgain) {
    // Slime sets the original's dmgtime a second on, which lava waits past; going into water puts it back.
    const BreathRules rules = repositoryRules();
    Breath breath{rules, kHarm};
    breath.fill(at(0.0F));
    TimedHarms timed;
    std::ignore = breathAt(breath, timed, 0.9F, WaterLevel::Feet, Contents::Water);
    std::ignore = breathAt(breath, timed, 1.0F, WaterLevel::Feet, Contents::Slime);
    EXPECT_TRUE(harmsOf(breathAt(breath, timed, 1.5F, WaterLevel::Feet, Contents::Lava)).empty());
    EXPECT_EQ(harmsOf(breathAt(breath, timed, 2.01F, WaterLevel::Feet, Contents::Lava)).size(), 1U);

    std::ignore = breathAt(breath, timed, 3.0F, WaterLevel::Feet, Contents::Slime);
    std::ignore = breathAt(breath, timed, 3.1F, WaterLevel::Dry, Contents::Empty);
    EXPECT_TRUE(harmsOf(breathAt(breath, timed, 3.2F, WaterLevel::Feet, Contents::Lava)).empty())
        << "going in, it is weighed before it is put back";
    EXPECT_EQ(harmsOf(breathAt(breath, timed, 3.21F, WaterLevel::Feet, Contents::Lava)).size(), 1U);
}

TEST(BreathTest, ItDrownsBeforeItBubblesAndBeforeSlimeHurtsIt) {
    const BreathRules rules = repositoryRules();
    for (std::uint64_t command = 0; command < 4'000; ++command) {
        TimedHarms timed;
        Breath fresh{rules, kHarm};
        fresh.fill(at(0.0F));
        nx::Random random{command, "breath"};
        const std::vector<BreathEvent> events =
            fresh.breathe(WaterLevel::Eyes, Contents::Slime, MoveType::Walk, timed, at(12.5F), random);
        if (events.size() == 3) {
            EXPECT_EQ(std::get<PlayerHarm>(events[0]), harm(3.0F, HarmKind::Drown));
            EXPECT_EQ(std::get<SoundMade>(events[1]).sound, &rules.bubbles);
            EXPECT_EQ(std::get<PlayerHarm>(events[2]), harm(12.0F, HarmKind::Acid));
            return;
        }
    }
    ADD_FAILURE() << "no breath bubbled";
}

} // namespace
