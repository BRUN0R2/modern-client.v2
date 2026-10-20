// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

#include "contractRecorder.hpp"

TEST(Conversion, NarrowKeepsValuesThatFit) {
    const nx_test::ContractRecorder recorder;

    EXPECT_EQ(nx::narrow<std::uint8_t>(200), 200U);
    EXPECT_EQ(nx::narrow<std::int16_t>(-300), -300);
    EXPECT_EQ(recorder.count(), 0);
}

TEST(Conversion, NarrowReportsValuesThatDoNotFit) {
    const nx_test::ContractRecorder recorder;

    std::ignore = nx::narrow<std::uint8_t>(256);
    std::ignore = nx::narrow<std::uint32_t>(-1);

    EXPECT_EQ(recorder.count(), 2);
    EXPECT_EQ(recorder.last().kind, nx::ContractKind::Precondition);
}

TEST(Conversion, TryNarrowReturnsATypedFailure) {
    const auto fits = nx::tryNarrow<std::int8_t>(100);
    const auto overflows = nx::tryNarrow<std::int8_t>(1000);

    EXPECT_EQ(fits.value_or(0), 100);
    ASSERT_FALSE(overflows.has_value());
    EXPECT_EQ(overflows.error().code, nx::ConversionError::OutOfRange);
    EXPECT_EQ(overflows.error().detail, "1000 does not fit in [-128, 127]");
}

TEST(Conversion, TruncateKeepsTheLowBits) {
    EXPECT_EQ(nx::truncate<std::uint8_t>(0x1'2C), 0x2CU);
    EXPECT_EQ(nx::truncate<std::uint16_t>(-1), 0xFFFFU);
}

TEST(Conversion, TryRoundRoundsHalfwayAwayFromZero) {
    EXPECT_EQ(nx::tryRound<std::int32_t>(2.5F).value_or(0), 3);
    EXPECT_EQ(nx::tryRound<std::int32_t>(-2.5F).value_or(0), -3);
}

TEST(Conversion, TryRoundRejectsNonFiniteAndOutOfRangeValues) {
    const auto infinite = nx::tryRound<std::int32_t>(std::numeric_limits<float>::infinity());
    const auto notANumber = nx::tryRound<std::int32_t>(std::numeric_limits<double>::quiet_NaN());
    const auto tooLarge = nx::tryRound<std::int16_t>(40000.0);
    const auto negative = nx::tryRound<std::uint8_t>(-1.0F);

    EXPECT_EQ(infinite.error().code, nx::ConversionError::NotFinite);
    EXPECT_EQ(notANumber.error().code, nx::ConversionError::NotFinite);
    EXPECT_EQ(tooLarge.error().code, nx::ConversionError::OutOfRange);
    EXPECT_EQ(negative.error().code, nx::ConversionError::OutOfRange);
}

TEST(Conversion, TryRoundAcceptsTheExactLimits) {
    EXPECT_EQ(nx::tryRound<std::int16_t>(32767.0).value_or(0), 32767);
    EXPECT_EQ(nx::tryRound<std::int16_t>(-32768.0).value_or(0), -32768);
    EXPECT_FALSE(nx::tryRound<std::int16_t>(32768.0).has_value());
}

TEST(Conversion, CheckedArithmeticDetectsOverflow) {
    constexpr auto max = std::numeric_limits<std::int32_t>::max();
    constexpr auto min = std::numeric_limits<std::int32_t>::min();

    EXPECT_EQ(nx::checkedAdd(max - 1, 1), max);
    EXPECT_FALSE(nx::checkedAdd(max, 1).has_value());
    EXPECT_FALSE(nx::checkedSub(min, 1).has_value());
    EXPECT_FALSE(nx::checkedSub(0U, 1U).has_value());
    EXPECT_FALSE(nx::checkedMul(max / 2 + 1, 2).has_value());
    EXPECT_FALSE(nx::checkedMul(std::int8_t{-128}, std::int8_t{-1}).has_value());
    EXPECT_EQ(nx::checkedMul(std::int8_t{-64}, std::int8_t{2}), std::int8_t{-128});
}

TEST(Conversion, SaturatedArithmeticClampsToTheTypeRange) {
    EXPECT_EQ(nx::addSaturated(std::int8_t{120}, std::int8_t{10}), 127);
    EXPECT_EQ(nx::addSaturated(std::int8_t{-120}, std::int8_t{-10}), -128);
    EXPECT_EQ(nx::subSaturated(std::uint8_t{5}, std::uint8_t{10}), 0U);
    EXPECT_EQ(nx::mulSaturated(std::int16_t{300}, std::int16_t{-300}), -32768);
    EXPECT_EQ(nx::mulSaturated(std::uint16_t{300}, std::uint16_t{300}), 65535U);
}

TEST(Conversion, IntegerConceptExcludesBoolAndCharacters) {
    static_assert(nx::Integer<int>);
    static_assert(nx::Integer<std::uint8_t>);
    static_assert(!nx::Integer<bool>);
    static_assert(!nx::Integer<char>);
    static_assert(!nx::Integer<char8_t>);
    SUCCEED();
}
