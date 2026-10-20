// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

import std;
import nx.core;

namespace sample {

enum class ParseError : std::uint8_t {
    Empty,
    NotANumber,
};

enum class SettingError : std::uint8_t {
    Missing,
    Corrupt,
};

[[nodiscard]] auto parseNumber(std::string_view text) -> nx::Result<int, ParseError> {
    if (text.empty()) {
        return nx::fail(ParseError::Empty);
    }
    int value = 0;
    const auto* const end = text.data() + text.size();
    const auto [stop, status] = std::from_chars(text.data(), end, value);
    if (status != std::errc{} || stop != end) {
        return nx::fail(ParseError::NotANumber, std::format("\"{}\"", text));
    }
    return value;
}

[[nodiscard]] auto loadRate(std::string_view text) -> nx::Result<int, SettingError> {
    auto parsed = parseNumber(text);
    if (!parsed) {
        return nx::fail(SettingError::Corrupt, "cvar \"rate\"", std::move(parsed.error()));
    }
    return *parsed;
}

} // namespace sample

TEST(Failure, SuccessCarriesTheValue) {
    const auto result = sample::parseNumber("25000");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, 25000);
}

TEST(Failure, FailCarriesCodeDetailAndCallSite) {
    const auto result = sample::parseNumber("abc");

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, sample::ParseError::NotANumber);
    EXPECT_EQ(result.error().detail, "\"abc\"");
    EXPECT_TRUE(std::string_view{result.error().where.function_name()}.contains("parseNumber"));
    EXPECT_TRUE(result.error().cause.empty());
}

TEST(Failure, TranslatingAFailureKeepsTheOriginalAsCause) {
    const auto result = sample::loadRate("fast");

    ASSERT_FALSE(result.has_value());
    const auto& failure = result.error();
    EXPECT_EQ(failure.code, sample::SettingError::Corrupt);
    EXPECT_EQ(failure.detail, "cvar \"rate\"");

    ASSERT_EQ(failure.cause.size(), 1U);
    const auto& cause = failure.cause.records().front();
    EXPECT_EQ(cause.domain, "ParseError");
    EXPECT_EQ(cause.code, "NotANumber");
    EXPECT_EQ(cause.value, std::to_underlying(sample::ParseError::NotANumber));
    EXPECT_EQ(cause.detail, "\"fast\"");
}

TEST(Failure, ChainsKeepEveryLayerInOrder) {
    enum class MapError : std::uint8_t { ConfigInvalid };

    auto rate = sample::loadRate("");
    ASSERT_FALSE(rate.has_value());
    const auto failure = nx::fail(MapError::ConfigInvalid, std::move(rate.error())).error();

    ASSERT_EQ(failure.cause.size(), 2U);
    EXPECT_EQ(failure.cause.records()[0].code, "Corrupt");
    EXPECT_EQ(failure.cause.records()[1].code, "Empty");
}

TEST(Failure, DescribePrintsTheWholeChain) {
    const auto result = sample::loadRate("fast");
    ASSERT_FALSE(result.has_value());

    const auto text = std::format("{}", result.error());

    EXPECT_TRUE(text.starts_with("SettingError::Corrupt: cvar \"rate\" [failureTests.cpp:"));
    EXPECT_TRUE(text.contains("\n  caused by: ParseError::NotANumber: \"fast\" [failureTests.cpp:"));
}

TEST(Failure, CodesWithoutEnumeratorPrintTheirValue) {
    const auto failure = nx::fail(static_cast<sample::ParseError>(7)).error();

    EXPECT_TRUE(nx::describe(failure).starts_with("ParseError::#7 ["));
}

TEST(Failure, VoidResultsSignalSuccessWithoutAValue) {
    const auto check = [](bool valid) -> nx::Result<void, sample::ParseError> {
        if (!valid) {
            return nx::fail(sample::ParseError::Empty);
        }
        return {};
    };

    EXPECT_TRUE(check(true).has_value());
    EXPECT_EQ(check(false).error().code, sample::ParseError::Empty);
}

TEST(Failure, ComposesWithMonadicOperations) {
    const auto doubled = sample::parseNumber("21").transform([](int value) { return value * 2; });
    const auto failed = sample::parseNumber("").transform([](int value) { return value * 2; });

    EXPECT_EQ(doubled.value_or(0), 42);
    EXPECT_EQ(failed.error().code, sample::ParseError::Empty);
}
