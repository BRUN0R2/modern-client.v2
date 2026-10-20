// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

#include "contractRecorder.hpp"

namespace {

struct LengthTag {
    static constexpr std::string_view kSuffix = "m";
};
struct CountTag {};

using Length = nx::Quantity<float, LengthTag>;
using Count = nx::Quantity<std::int32_t, CountTag>;

template <class Left, class Right>
concept Addable = requires(Left left, Right right) { left + right; };

template <class Left, class Right>
concept Comparable = requires(Left left, Right right) { left == right; };

} // namespace

TEST(Quantity, IsAStrongType) {
    static_assert(!std::is_convertible_v<float, Length>, "no implicit construction from the representation");
    static_assert(!std::is_convertible_v<Length, float>, "no implicit conversion to the representation");
    static_assert(!std::is_convertible_v<nx::HitPoints, nx::ArmorPoints>, "different units never convert");
    static_assert(!Addable<nx::HitPoints, nx::ArmorPoints>, "different units never mix");
    static_assert(!Addable<Length, float>, "raw numbers need a unit");
    static_assert(!Comparable<nx::HitPoints, float>, "comparisons need the same unit");
    static_assert(sizeof(Length) == sizeof(float), "no storage overhead");
    SUCCEED();
}

TEST(Quantity, DefaultsToZero) {
    EXPECT_EQ(Length{}.value(), 0.0F);
    EXPECT_EQ(Length{}, Length::zero());
}

TEST(Quantity, ArithmeticKeepsTheUnit) {
    constexpr Length start{2.0F};
    constexpr Length step{0.5F};

    static_assert(std::same_as<decltype(start + step), Length>);
    EXPECT_EQ((start + step).value(), 2.5F);
    EXPECT_EQ((start - step).value(), 1.5F);
    EXPECT_EQ((start * 3.0F).value(), 6.0F);
    EXPECT_EQ((3.0F * start).value(), 6.0F);
    EXPECT_EQ((start / 4.0F).value(), 0.5F);
    EXPECT_EQ((-start).value(), -2.0F);
}

TEST(Quantity, RatioOfTheSameUnitIsDimensionless) {
    static_assert(std::same_as<decltype(Length{6.0F} / Length{2.0F}), float>);
    EXPECT_EQ(Length{6.0F} / Length{2.0F}, 3.0F);
}

TEST(Quantity, OrdersLikeItsRepresentation) {
    EXPECT_LT(Count{1}, Count{2});
    EXPECT_EQ(std::max(Count{4}, Count{9}), Count{9});
    EXPECT_EQ(std::clamp(Count{50}, Count{0}, Count{10}), Count{10});
}

TEST(Quantity, IntegerOverflowIsReportedAndSaturates) {
    const nx_test::ContractRecorder recorder;
    constexpr Count largest{std::numeric_limits<std::int32_t>::max()};

    const auto sum = largest + Count{1};

    EXPECT_EQ(recorder.count(), 1);
    EXPECT_EQ(recorder.last().kind, nx::ContractKind::Invariant);
    EXPECT_EQ(sum, largest);
}

TEST(Quantity, IntegerDivisionByZeroIsReported) {
    const nx_test::ContractRecorder recorder;

    std::ignore = Count{10} / 0;

    EXPECT_EQ(recorder.count(), 1);
}

TEST(Quantity, SaturatedOperationsClampOnPurpose) {
    const nx_test::ContractRecorder recorder;
    constexpr nx::Money limit{std::numeric_limits<std::int32_t>::max()};

    EXPECT_EQ(nx::addSaturated(limit, nx::Money{100}), limit);
    EXPECT_EQ(nx::subSaturated(nx::Money{0}, limit), nx::Money{-std::numeric_limits<std::int32_t>::max()});
    EXPECT_EQ(recorder.count(), 0);
}

TEST(Quantity, FormatsWithTheUnit) {
    EXPECT_EQ(std::format("{}", Length{1.5F}), "1.5m");
    EXPECT_EQ(std::format("{:.2f}", Length{1.5F}), "1.50m");
    EXPECT_EQ(std::format("{}", nx::Money{800}), "$800");
    EXPECT_EQ(std::format("{}", Count{7}), "7");
}
