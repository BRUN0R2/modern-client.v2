// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;

#include "consoleFixtures.hpp"

namespace {

using nx::console::Console;
using nx::console::CvarDesc;
using nx::console::CvarError;
using nx::console::CvarFlag;
using nx::console::CvarKind;
using nx::console::TypedCvar;

enum class ForceCamera : std::uint8_t {
    Anyone,
    TeamOnly,
    FirstPerson,
    Count,
};

class CvarRegistrationTest : public testing::Test {
protected:
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    Console console{output, files};
};

TEST_F(CvarRegistrationTest, RegistersWithTheDefaultValue) {
    const auto gravity = console.registerCvar(CvarDesc<float>{.name = "sv_gravity",
                                                              .defaultValue = 800,
                                                              .description = "World gravity",
                                                              .flags = {CvarFlag::Notify}});
    ASSERT_TRUE(gravity.has_value());
    EXPECT_EQ(gravity->value(), 800.0F);
    EXPECT_EQ(gravity->name(), "sv_gravity");
    EXPECT_TRUE(gravity->flags().test(CvarFlag::Notify));
    EXPECT_EQ(gravity->revision(), 0U);

    const nx::console::CvarBase* found = console.findCvar("SV_GRAVITY");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->kind(), CvarKind::Float);
    EXPECT_EQ(found->text(), "800");
    EXPECT_EQ(found->defaultText(), "800");
    EXPECT_EQ(found->description(), "World gravity");
}

TEST_F(CvarRegistrationTest, RefusesBadNamesAndTakenNames) {
    EXPECT_EQ(console.registerCvar(CvarDesc<bool>{.name = "two words"}).error().code, CvarError::InvalidName);
    EXPECT_EQ(console.registerCvar(CvarDesc<bool>{.name = ""}).error().code, CvarError::InvalidName);
    ASSERT_TRUE(console.registerCvar(CvarDesc<bool>{.name = "sv_cheats"}).has_value());
    EXPECT_EQ(console.registerCvar(CvarDesc<bool>{.name = "SV_CHEATS"}).error().code, CvarError::NameTaken);
    // Commands and cvars share one namespace.
    EXPECT_EQ(console.registerCvar(CvarDesc<bool>{.name = "exec"}).error().code, CvarError::NameTaken);
}

TEST_F(CvarRegistrationTest, RefusesDefinitionsThatContradictThemselves) {
    const auto emptyRange =
        console.registerCvar(CvarDesc<std::int32_t>{.name = "a", .minimum = 10, .maximum = 5});
    EXPECT_EQ(emptyRange.error().code, CvarError::InvalidDefinition);

    const auto outside =
        console.registerCvar(CvarDesc<float>{.name = "b", .defaultValue = 100, .maximum = 60.0F});
    EXPECT_EQ(outside.error().code, CvarError::InvalidDefinition);
    ASSERT_EQ(outside.error().cause.size(), 1U);
    EXPECT_EQ(outside.error().cause.records().front().code, "OutOfRange");

    const auto unwritable = console.registerCvar(CvarDesc<std::string>{.name = "c", .defaultValue = "a\"b"});
    EXPECT_EQ(unwritable.error().code, CvarError::InvalidDefinition);
    EXPECT_EQ(console.findCvar("a"), nullptr);
}

TEST_F(CvarRegistrationTest, SettingFromCodeChecksTheRangeAndCountsChanges) {
    const auto freezeTime = console.registerCvar(
        CvarDesc<std::int32_t>{.name = "mp_freezetime", .defaultValue = 6, .minimum = 0, .maximum = 60});
    ASSERT_TRUE(freezeTime.has_value());

    EXPECT_TRUE(freezeTime->set(10).has_value());
    EXPECT_EQ(freezeTime->value(), 10);
    EXPECT_EQ(freezeTime->revision(), 1U);
    EXPECT_TRUE(freezeTime->set(10).has_value());
    EXPECT_EQ(freezeTime->revision(), 1U) << "setting the same value is not a change";

    const auto refused = freezeTime->set(61);
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, CvarError::OutOfRange);
    EXPECT_EQ(refused.error().detail, R"(mp_freezetime: expected a whole number in [0, 60], got "61")");
    EXPECT_EQ(freezeTime->value(), 10);
}

TEST(CvarTextTest, BoolsAreZeroOrOne) {
    TypedCvar<bool> cvar{CvarDesc<bool>{.name = "mp_friendlyfire"}};
    EXPECT_EQ(cvar.kind(), CvarKind::Bool);
    EXPECT_TRUE(cvar.assign("1").has_value());
    EXPECT_TRUE(cvar.value());
    EXPECT_EQ(cvar.text(), "1");
    EXPECT_TRUE(cvar.assign(" 0 ").has_value());
    EXPECT_EQ(cvar.text(), "0");
    for (const std::string_view refused : {"2", "true", "", "1x"}) {
        const auto result = cvar.assign(refused);
        ASSERT_FALSE(result.has_value()) << refused;
        EXPECT_EQ(result.error().code, CvarError::InvalidValue);
    }
    EXPECT_EQ(cvar.expectation(), "0 or 1");
}

TEST(CvarTextTest, WholeNumbersAcceptRealNotationWithoutAFraction) {
    TypedCvar<std::int32_t> cvar{CvarDesc<std::int32_t>{.name = "mp_maxrounds"}};
    for (const auto& [input, text] : std::to_array<std::pair<std::string_view, std::string_view>>(
             {{"30", "30"}, {"+12", "12"}, {"-4", "-4"}, {"10.0", "10"}, {"1e3", "1000"}})) {
        ASSERT_TRUE(cvar.assign(input).has_value()) << input;
        EXPECT_EQ(cvar.text(), text);
    }
    for (const std::string_view refused : {"10.5", "abc", "3000000000", "12abc", "nan", ""}) {
        EXPECT_EQ(cvar.assign(refused).error().code, CvarError::InvalidValue) << refused;
    }
    EXPECT_EQ(cvar.expectation(), "a whole number");
}

TEST(CvarTextTest, NumbersAreFinite) {
    TypedCvar<float> cvar{CvarDesc<float>{.name = "mp_roundtime", .defaultValue = 5, .minimum = 1.0F}};
    EXPECT_TRUE(cvar.assign("2.5").has_value());
    EXPECT_EQ(cvar.text(), "2.5");
    EXPECT_TRUE(cvar.assign(".5e1").has_value());
    EXPECT_EQ(cvar.text(), "5");
    EXPECT_EQ(cvar.assign("inf").error().code, CvarError::InvalidValue);
    EXPECT_EQ(cvar.assign("1e40").error().code, CvarError::InvalidValue);
    EXPECT_EQ(cvar.assign("0.5").error().code, CvarError::OutOfRange);
    EXPECT_EQ(cvar.expectation(), "a number of at least 1");
    EXPECT_EQ(cvar.defaultText(), "5");
}

TEST(CvarTextTest, TextKeepsWhatTheConsoleCanWriteBack) {
    TypedCvar<std::string> cvar{
        CvarDesc<std::string>{.name = "hostname", .defaultValue = "Modern-Client.v2"}};
    EXPECT_TRUE(cvar.assign("My Server; really").has_value());
    EXPECT_EQ(cvar.value(), "My Server; really");
    EXPECT_EQ(cvar.set("quote\"inside").error().code, CvarError::InvalidValue);
    EXPECT_EQ(cvar.set("two\nlines").error().code, CvarError::InvalidValue);
    EXPECT_EQ(cvar.value(), "My Server; really");
}

/// The sizes of a crosshair, as a text cvar names them.
[[nodiscard]] auto isSize(std::string_view text) noexcept -> bool {
    return text == "small" || text == "large";
}

TEST(CvarTextTest, TextWithARuleTakesOnlyWhatTheRuleAccepts) {
    TypedCvar<std::string> cvar{
        CvarDesc<std::string>{.name = "cl_size",
                              .defaultValue = "small",
                              .rule = {.expectation = "small or large", .accepts = isSize}}};
    EXPECT_TRUE(cvar.checkDefinition().has_value());
    EXPECT_TRUE(cvar.assign("large").has_value());
    EXPECT_EQ(cvar.value(), "large");
    const auto refused = cvar.assign("huge");
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, CvarError::InvalidValue);
    EXPECT_EQ(refused.error().detail, R"(cl_size: expected small or large, got "huge")");
    EXPECT_EQ(cvar.value(), "large");

    const TypedCvar<std::string> refusesItsDefault{
        CvarDesc<std::string>{.name = "cl_size",
                              .defaultValue = "huge",
                              .rule = {.expectation = "small or large", .accepts = isSize}}};
    EXPECT_EQ(refusesItsDefault.checkDefinition().error().code, CvarError::InvalidDefinition);
}

TEST(CvarTextTest, EnumsTakeTheValueOrTheName) {
    TypedCvar<ForceCamera> cvar{CvarDesc<ForceCamera>{.name = "mp_forcecamera"}};
    EXPECT_EQ(cvar.kind(), CvarKind::Enum);
    EXPECT_TRUE(cvar.assign("2").has_value());
    EXPECT_EQ(cvar.value(), ForceCamera::FirstPerson);
    EXPECT_TRUE(cvar.assign("teamonly").has_value());
    EXPECT_EQ(cvar.value(), ForceCamera::TeamOnly);
    EXPECT_EQ(cvar.text(), "1") << "written as the number, like the original configs";
    EXPECT_EQ(cvar.assign("3").error().code, CvarError::InvalidValue);
    EXPECT_EQ(cvar.assign("Count").error().code, CvarError::InvalidValue);
    EXPECT_EQ(cvar.set(ForceCamera::Count).error().code, CvarError::InvalidValue);
    EXPECT_EQ(cvar.expectation(), "one of 0 (Anyone), 1 (TeamOnly), 2 (FirstPerson)");
}

} // namespace
