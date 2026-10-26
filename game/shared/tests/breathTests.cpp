// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The repository's data/cstrike/breath.toml, and a player's breath in water as the original's
// CBasePlayer::WaterMove has it: the gasps as it comes up for air, after a while under water and once
// its air ran out, the air whole again with its head out, held while it noclips, and the bubbles with
// its head under, the likelier the less air it has left.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.game;
import nx.platform;
import cs.shared;

namespace {

using cs::Breath;
using cs::BreathRules;
using cs::MoveType;
using cs::SoundMade;
using cs::WaterLevel;

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

/// The sound of a breath at `seconds`, in water to `level`, walking.
[[nodiscard]] auto breatheAt(Breath& breath, float seconds, WaterLevel level) -> std::optional<SoundMade> {
    nx::Random random{0, "breath"};
    return breath.breathe(level, MoveType::Walk, at(seconds), random);
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
        Breath breath{rules};
        breath.fill(at(0.0F));
        nx::Random random{command, "breath"};
        const std::optional<SoundMade> made =
            breath.breathe(WaterLevel::Eyes, MoveType::Walk, at(seconds), random);
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
    Breath breath{rules};
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
    Breath breath{rules};
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
    Breath breath{rules};
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
    Breath breath{rules};
    breath.fill(at(0.0F));
    nx::Random random{0, "breath"};
    EXPECT_FALSE(breath.breathe(WaterLevel::Dry, MoveType::Noclip, at(20.0F), random).has_value());
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
    Breath breath{rules};
    breath.fill(at(0.0F));
    std::optional<SoundMade> bubble;
    for (std::uint64_t command = 0; !bubble && command < kBreaths; ++command) {
        nx::Random random{command, "breath"};
        bubble = breath.breathe(WaterLevel::Eyes, MoveType::Walk, at(12.5F), random);
    }
    ASSERT_TRUE(bubble.has_value());
    EXPECT_EQ(bubble->sound, &rules.bubbles);
    EXPECT_EQ(bubble->volume, 0.8F);
    EXPECT_EQ(bubble->at, at(12.5F).now);
}

TEST(BreathTest, WithItsHeadOutOfWaterItDoesNotBubble) {
    const BreathRules rules = repositoryRules();
    Breath breath{rules};
    breath.fill(at(0.0F));
    for (std::uint64_t command = 0; command < 4'000; ++command) {
        nx::Random random{command, "breath"};
        const float seconds = 1.0F + (static_cast<float>(command) * 0.01F);
        EXPECT_FALSE(breath.breathe(WaterLevel::Waist, MoveType::Walk, at(seconds), random).has_value())
            << seconds;
    }
}

} // namespace
