// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How long a client shows the dead: cl_corpsestay, how opaque a corpse is drawn by how long ago it died,
// and the clock of when each body a client shows died.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.render;
import cs.client;

namespace {

/// Prints nothing.
class SilentOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view /*line*/) override {}
    void reportFailure(const nx::Failure<nx::console::ConsoleError>& /*failure*/) override {}
};

/// No files.
class NoFiles final : public nx::console::ConfigFiles {
public:
    [[nodiscard]] auto read(std::string_view path)
        -> nx::Result<std::string, nx::console::ConfigFileError> override {
        return nx::fail(nx::console::ConfigFileError::NotFound, std::string{path});
    }
};

constexpr nx::TickRate kRate = nx::kDefaultTickRate;
constexpr nx::Seconds kStay{10.0F};

[[nodiscard]] auto atSecond(float seconds) -> nx::GameTime {
    return nx::GameTime::atTick(
        static_cast<std::int64_t>(std::lround(seconds * static_cast<float>(kRate.value()))));
}

TEST(CorpseTest, ACorpseIsWholeThroughItsStayThenFadesOutAndIsGone) {
    EXPECT_EQ(cs::corpseAmountOf(std::nullopt, kStay), cs::kWholeBody) << "alive";
    EXPECT_EQ(cs::corpseAmountOf(nx::Seconds{0.0F}, kStay), cs::kWholeBody) << "just dead";
    EXPECT_EQ(cs::corpseAmountOf(kStay, kStay), cs::kWholeBody) << "its stay's last moment";
    const std::uint8_t halfway = cs::corpseAmountOf(kStay + (cs::kCorpseFade / 2.0F), kStay);
    EXPECT_NEAR(halfway, 128, 1) << "half faded";
    EXPECT_EQ(cs::corpseAmountOf(kStay + cs::kCorpseFade, kStay), 0U) << "gone";
    EXPECT_EQ(cs::corpseAmountOf(nx::Seconds{600.0F}, kStay), 0U);
    // Of no stay, a corpse fades out at once.
    EXPECT_LT(cs::corpseAmountOf(cs::kCorpseFade / 2.0F, {}), cs::kWholeBody);
    EXPECT_EQ(cs::renderModeOf(cs::kWholeBody), nx::render::RenderMode::Normal);
    EXPECT_EQ(cs::renderModeOf(halfway), nx::render::RenderMode::TransTexture) << "blended as it fades";
}

TEST(CorpseTest, ABodysClockStartsAsItIsFirstSeenDeadAndAnotherDeathStartsItAnew) {
    cs::CorpseClock<std::uint32_t> clock;
    EXPECT_EQ(clock.deadFor(1, false, atSecond(0.0F), kRate), std::nullopt) << "alive";
    EXPECT_EQ(clock.deadFor(1, true, atSecond(2.0F), kRate), nx::Seconds{0.0F}) << "first seen dead";
    const std::optional<nx::Seconds> later = clock.deadFor(1, true, atSecond(5.0F), kRate);
    ASSERT_TRUE(later.has_value());
    EXPECT_NEAR(later->count(), 3.0F, 1e-4F);
    // Alive again, a player respawned: its next death is timed anew.
    EXPECT_EQ(clock.deadFor(1, false, atSecond(6.0F), kRate), std::nullopt);
    EXPECT_EQ(clock.deadFor(1, true, atSecond(7.0F), kRate), nx::Seconds{0.0F});
}

TEST(CorpseTest, ABodyNoLongerAskedAboutIsForgotten) {
    cs::CorpseClock<std::uint32_t> clock;
    std::ignore = clock.deadFor(1, true, atSecond(0.0F), kRate);
    std::ignore = clock.deadFor(2, true, atSecond(0.0F), kRate);
    clock.forgetUnasked();
    EXPECT_EQ(clock.size(), 2U) << "both asked about";
    std::ignore = clock.deadFor(2, true, atSecond(1.0F), kRate);
    clock.forgetUnasked();
    EXPECT_EQ(clock.size(), 1U) << "the first, gone from what the client shows";
    // Seen dead again after it was forgotten, its clock starts anew.
    EXPECT_EQ(clock.deadFor(1, true, atSecond(4.0F), kRate), nx::Seconds{0.0F});
}

TEST(CorpseCvarsTest, ClCorpsestayIsArchivedSixHundredSecondsByDefaultAndHeldToItsRange) {
    SilentOutput output;
    NoFiles files;
    nx::console::Console console{output, files};
    const auto cvars = cs::CorpseCvars::registerIn(console);
    ASSERT_TRUE(cvars.has_value());
    EXPECT_EQ(cvars->stayTime(), cs::kLongestCorpseStay) << "Counter-Strike's 600";
    EXPECT_NE(console.archivedCvars().find("cl_corpsestay"), std::string::npos) << "the player's setting";
    for (const auto& [value, expected] :
         {std::pair{"10", 10.0F}, std::pair{"1000", 600.0F}, std::pair{"-5", 0.0F}, std::pair{"0", 0.0F}}) {
        ASSERT_TRUE(console.setCvar("cl_corpsestay", value).has_value()) << value;
        EXPECT_EQ(cvars->stayTime(), nx::Seconds{expected}) << value;
    }
}

} // namespace
