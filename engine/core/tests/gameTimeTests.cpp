// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

namespace {

using nx::GameTime;
using nx::Seconds;

constexpr nx::TickRate kRate = nx::kDefaultTickRate;
/// A month of ticks at kRate.
constexpr std::int64_t kMonth = std::int64_t{100} * 60 * 60 * 24 * 31;

} // namespace

TEST(GameTime, ATickAtTheDefaultRateIsTenMilliseconds) {
    EXPECT_EQ(kRate.value(), 100U);
    EXPECT_FLOAT_EQ(nx::tickLengthOf(kRate).count(), 0.01F);
    EXPECT_FLOAT_EQ(nx::tickLengthOf(nx::TickRate{64}).count(), 1.0F / 64.0F);
}

TEST(GameTime, ATickLaterIsTheNextTicksStartAfterAMonthAsAtTheStart) {
    for (const std::int64_t tick : {std::int64_t{0}, kMonth}) {
        const GameTime later = GameTime::atTick(tick).after(nx::tickLengthOf(kRate), kRate);
        EXPECT_EQ(later.tick(), tick + 1);
        EXPECT_EQ(later.fraction(), 0.0F);
        EXPECT_EQ(GameTime::atTick(tick).ticksLater(1), GameTime::atTick(tick + 1));
    }
}

TEST(GameTime, AMomentInsideATickKeepsItsFraction) {
    const GameTime moment = GameTime::atTick(kMonth).after(Seconds{0.0025F}, kRate);
    EXPECT_EQ(moment.tick(), kMonth);
    EXPECT_NEAR(moment.fraction(), 0.25F, 1e-6F);
    // Float seconds a month in are a quarter of a second apart: this is a quarter of a tick.
    EXPECT_NEAR(moment.since(GameTime::atTick(kMonth), kRate).count(), 0.0025F, 1e-8F);

    const GameTime carried = moment.after(Seconds{0.01F}, kRate);
    EXPECT_EQ(carried.tick(), kMonth + 1);
    EXPECT_NEAR(carried.fraction(), 0.25F, 1e-6F);
}

TEST(GameTime, ADurationThatIsAWholeNumberOfTicksAsAFloatLastsThatNumber) {
    // Each a hair above or below its ticks as a float: 0.1 is 0.100000001, 0.7 is 0.699999988.
    for (const auto& [seconds, ticks] :
         {std::pair{0.1F, std::int64_t{10}}, std::pair{0.3F, std::int64_t{30}},
          std::pair{0.7F, std::int64_t{70}}, std::pair{0.01F, std::int64_t{1}},
          std::pair{-0.1F, std::int64_t{-10}}, std::pair{6.0F, std::int64_t{600}}}) {
        const GameTime later = GameTime::atTick(kMonth).after(Seconds{seconds}, kRate);
        EXPECT_EQ(later, GameTime::atTick(kMonth + ticks)) << seconds;
        EXPECT_EQ(later.since(GameTime::atTick(kMonth), kRate), Seconds{seconds}) << seconds;
    }
    const nx::TickRate sixtyFour{64};
    EXPECT_EQ(GameTime{}.after(Seconds{0.1F}, sixtyFour).tick(), 6) << "6.4 ticks: no whole number";
    EXPECT_NEAR(GameTime{}.after(Seconds{0.1F}, sixtyFour).fraction(), 0.4F, 1e-5F);
    EXPECT_EQ(GameTime{}.after(Seconds{5.0F / 64.0F}, sixtyFour), GameTime::atTick(5));
}

TEST(GameTime, ADurationBetweenTicksKeepsItsShareOfThem) {
    const GameTime between = GameTime::atTick(kMonth, 0.25F).after(Seconds{0.105F}, kRate);
    EXPECT_EQ(between.tick(), kMonth + 10);
    EXPECT_NEAR(between.fraction(), 0.75F, 1e-5F);
}

TEST(GameTime, ItGoesBackAndMeasuresBackwards) {
    const GameTime earlier = GameTime::atTick(10).after(Seconds{-0.005F}, kRate);
    EXPECT_EQ(earlier.tick(), 9);
    EXPECT_NEAR(earlier.fraction(), 0.5F, 1e-6F);
    EXPECT_NEAR(earlier.since(GameTime::atTick(10), kRate).count(), -0.005F, 1e-8F);
    EXPECT_NEAR(GameTime::atTick(kMonth).since(GameTime{}, kRate).count(), 2678400.0F, 1.0F);
}

TEST(GameTime, MomentsAreOrderedByTickThenFraction) {
    EXPECT_LT(GameTime{}, GameTime::atTick(0, 0.5F));
    EXPECT_LT(GameTime::atTick(1, 0.75F), GameTime::atTick(2));
    EXPECT_GT(GameTime::atTick(-1), GameTime::atTick(-2, 0.5F));
    EXPECT_EQ(GameTime::atTick(3, 0.5F), GameTime::atTick(2, 0.5F).ticksLater(1));
}
