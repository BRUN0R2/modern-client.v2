// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The fog's cvars, the fog they ask for, and the fog a frame shows as they change.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.render;
import cs.client;

#include "consoleFixtures.hpp"
#include "contractRecorder.hpp"

namespace {

class RecordingSink final : public nx::log::Sink {
public:
    void write(const nx::log::Record& record) noexcept override { lines.emplace_back(record.message); }
    [[nodiscard]] auto messages() const noexcept -> std::span<const std::string> { return lines; }

private:
    std::vector<std::string> lines;
};

/// A console and the fog's cvars in it.
struct FogConsole {
    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    cs::FogCvars cvars = cs::FogCvars::registerIn(console).value();
};

void setCvar(FogConsole& fog, std::string_view name, std::string_view value) {
    EXPECT_TRUE(fog.console.setCvar(name, value).has_value()) << name << " " << value;
}

void expectObservedContract(const cs::FogCvars& cvars) {
    const nx::test::ContractRecorder recorder;
    const auto asked = cvars.fog();
    EXPECT_EQ(recorder.count(), 1);
    ASSERT_TRUE(asked.has_value());
    EXPECT_FALSE(asked->has_value());
}

void expectEndNotPastStart(FogConsole& fog, std::string_view end) {
    setCvar(fog, "fog_end", end);
    const auto asked = fog.cvars.fog();
    ASSERT_FALSE(asked.has_value());
    EXPECT_EQ(asked.error().code, cs::FogError::EndNotPastStart);
    EXPECT_NE(asked.error().detail.find("fog_start 100"), std::string::npos);
    EXPECT_NE(asked.error().detail.find(end), std::string::npos);
}

TEST(FogCvarsTest, NoFogShowsUntilFogEnableAndTheyAreNotArchived) {
    FogConsole fog;
    const auto none = fog.cvars.fog();
    ASSERT_TRUE(none.has_value());
    EXPECT_FALSE(none->has_value());
    EXPECT_EQ(fog.console.archivedCvars().find("fog_"), std::string::npos)
        << "a map's or a mod's, not the player's";
    setCvar(fog, "fog_enable", "1");
    setCvar(fog, "fog_color", "51 102 255");
    setCvar(fog, "fog_curve", "linear");
    setCvar(fog, "fog_start", "100");
    setCvar(fog, "fog_end", "400");
    setCvar(fog, "fog_opacity", "0.5");
    const auto asked = fog.cvars.fog();
    ASSERT_TRUE(asked.has_value() && asked->has_value());
    const nx::render::Fog& made = **asked;
    EXPECT_EQ(made.color, (nx::render::FogColor{.red = 0.2F, .green = 0.4F, .blue = 1.0F}));
    EXPECT_EQ(made.curve, nx::render::FogCurve::Linear);
    EXPECT_EQ(made.start, nx::HammerUnits{100.0F});
    EXPECT_EQ(made.end, nx::HammerUnits{400.0F});
    EXPECT_EQ(made.opacity, 0.5F);
    setCvar(fog, "fog_end", "0");
    const auto linearWithoutEnd = fog.cvars.fog();
    ASSERT_FALSE(linearWithoutEnd.has_value());
    EXPECT_EQ(linearWithoutEnd.error().code, cs::FogError::LinearEndRequired);
    EXPECT_NE(linearWithoutEnd.error().detail.find("fog_end"), std::string::npos);
    setCvar(fog, "fog_curve", "exp2");
    const auto endless = fog.cvars.fog();
    ASSERT_TRUE(endless.has_value() && endless->has_value());
    EXPECT_EQ((*endless)->end, nx::render::kEndlessFog) << "fog_end 0, without end";
    EXPECT_FALSE(fog.console.setCvar("fog_curve", "cubic").has_value());
    EXPECT_FALSE(fog.console.setCvar("fog_opacity", "2").has_value());
}

TEST(FogCvarsTest, AFiniteEndMustBePastTheStart) {
    FogConsole fog;
    setCvar(fog, "fog_enable", "1");
    setCvar(fog, "fog_start", "100");
    expectEndNotPastStart(fog, "99");
    expectEndNotPastStart(fog, "100");
    setCvar(fog, "fog_end", "101");
    EXPECT_TRUE(fog.cvars.fog().has_value());
}

TEST(FogCvarsTest, IncompatibleDistancesAreNotReadWhileFogIsDisabled) {
    FogConsole fog;
    setCvar(fog, "fog_start", "100");
    setCvar(fog, "fog_end", "50");
    const auto asked = fog.cvars.fog();
    ASSERT_TRUE(asked.has_value());
    EXPECT_FALSE(asked->has_value());
}

TEST(FogControlTest, AFogTheCvarsAskForComesInOverTheirFadeAndOneTheyMakeNoneOfLeavesItAsItIs) {
    FogConsole fog;
    cs::FogControl control;
    setCvar(fog, "fog_fade", "1");
    control.update(fog.cvars, nx::Seconds{0.5F});
    EXPECT_FALSE(control.shown().has_value());
    setCvar(fog, "fog_enable", "1");
    control.update(fog.cvars, nx::Seconds{0.5F});
    const auto coming = control.shown();
    ASSERT_TRUE(coming.has_value());
    EXPECT_GT(coming->opacity, 0.0F);
    EXPECT_LT(coming->opacity, 1.0F) << "on its way in";
    control.update(fog.cvars, nx::Seconds{0.5F});
    const auto arrived = control.shown();
    ASSERT_TRUE(arrived.has_value());
    EXPECT_EQ(arrived->opacity, 1.0F);
    // An end not past the start makes no fog: the one shown stays.
    setCvar(fog, "fog_start", "500");
    setCvar(fog, "fog_end", "100");
    control.update(fog.cvars, nx::Seconds{0.5F});
    EXPECT_EQ(control.shown(), arrived);
    setCvar(fog, "fog_end", "600");
    control.update(fog.cvars, nx::Seconds{1.0F});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->start, nx::HammerUnits{500.0F});
    EXPECT_EQ(control.shown()->end, nx::HammerUnits{600.0F});
    setCvar(fog, "fog_enable", "0");
    control.update(fog.cvars, nx::Seconds{1.0F});
    EXPECT_FALSE(control.shown().has_value()) << "gone back out over its fade";
}

TEST(FogControlTest, ChangedInputsAreAppliedAndDisabledChangesAreReadWhenEnabledAgain) {
    FogConsole fog;
    cs::FogControl control;
    setCvar(fog, "fog_fade", "0");
    setCvar(fog, "fog_enable", "1");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());

    setCvar(fog, "fog_color", "255 0 0");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->color.red, 1.0F);
    setCvar(fog, "fog_density", "0.5");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->density, 0.5F);
    setCvar(fog, "fog_start", "100");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->start, nx::HammerUnits{100.0F});
    setCvar(fog, "fog_end", "400");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->end, nx::HammerUnits{400.0F});
    setCvar(fog, "fog_opacity", "0.5");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->opacity, 0.5F);
    setCvar(fog, "fog_curve", "linear");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->curve, nx::render::FogCurve::Linear);

    setCvar(fog, "fog_enable", "0");
    control.update(fog.cvars, nx::Seconds{});
    EXPECT_FALSE(control.shown().has_value());
    setCvar(fog, "fog_color", "0 0 255");
    control.update(fog.cvars, nx::Seconds{});
    setCvar(fog, "fog_enable", "1");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->color.blue, 1.0F);
    EXPECT_EQ(control.shown()->color.red, 0.0F);
}

TEST(FogControlTest, DistinctConsolesWithEqualRevisionsDoNotShareAStaleFog) {
    FogConsole first;
    FogConsole second;
    for (FogConsole* fog : {&first, &second}) {
        setCvar(*fog, "fog_fade", "0");
        setCvar(*fog, "fog_enable", "1");
    }
    setCvar(first, "fog_color", "255 0 0");
    setCvar(second, "fog_color", "0 0 255");
    ASSERT_EQ(first.cvars.color.revision(), second.cvars.color.revision());

    cs::FogControl control;
    control.update(first.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->color.red, 1.0F);
    control.update(second.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->color.blue, 1.0F);
    EXPECT_EQ(control.shown()->color.red, 0.0F);
}

TEST(FogControlTest, ChangingFadeAloneDoesNotRestartATransition) {
    FogConsole fog;
    cs::FogControl control;
    setCvar(fog, "fog_fade", "2");
    setCvar(fog, "fog_enable", "1");
    control.update(fog.cvars, nx::Seconds{0.5F});
    setCvar(fog, "fog_fade", "0");
    control.update(fog.cvars, nx::Seconds{0.5F});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_FLOAT_EQ(control.shown()->opacity, 0.5F);

    setCvar(fog, "fog_color", "255 0 0");
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_TRUE(control.shown().has_value());
    EXPECT_EQ(control.shown()->opacity, 1.0F);
    EXPECT_EQ(control.shown()->color.red, 1.0F);
}

TEST(FogControlTest, AnUnchangedFailureIsLoggedOnceWithItsTypedReason) {
    nx::log::Logger logger;
    auto sink = std::make_unique<RecordingSink>();
    const RecordingSink* const recorded = sink.get();
    logger.addSink(std::move(sink));
    const nx::log::DefaultLoggerScope installed{logger};

    FogConsole fog;
    cs::FogControl control;
    setCvar(fog, "fog_enable", "1");
    setCvar(fog, "fog_curve", "linear");
    control.update(fog.cvars, nx::Seconds{});
    control.update(fog.cvars, nx::Seconds{});
    ASSERT_EQ(recorded->messages().size(), 1U);
    EXPECT_NE(recorded->messages().front().find("FogError::LinearEndRequired"), std::string::npos);
    EXPECT_NE(recorded->messages().front().find("fog_end"), std::string::npos);
}

TEST(FogCvarsTest, InvalidColorHandleReportsAContractViolationWithoutAnExpectedFailure) {
    FogConsole fog;
    auto invalid = fog.console.registerCvar(
        nx::console::CvarDesc<std::string>{.name = "unvalidatedColor", .defaultValue = "invalid"});
    ASSERT_TRUE(invalid.has_value());
    fog.cvars.color = *invalid;
    setCvar(fog, "fog_enable", "1");

    expectObservedContract(fog.cvars);
}

TEST(FogCvarsTest, InvalidEndHandleReportsAContractViolationWithoutAnExpectedFailure) {
    FogConsole fog;
    auto invalid = fog.console.registerCvar(
        nx::console::CvarDesc<float>{.name = "unvalidatedEnd", .defaultValue = -1.0F});
    ASSERT_TRUE(invalid.has_value());
    fog.cvars.end = *invalid;
    setCvar(fog, "fog_enable", "1");

    expectObservedContract(fog.cvars);
}

TEST(FogCvarsTest, InvalidCurveHandleReportsAContractViolationWithoutAnExpectedFailure) {
    FogConsole fog;
    auto invalid = fog.console.registerCvar(
        nx::console::CvarDesc<std::string>{.name = "unvalidatedCurve", .defaultValue = "cubic"});
    ASSERT_TRUE(invalid.has_value());
    fog.cvars.curve = *invalid;
    setCvar(fog, "fog_enable", "1");

    expectObservedContract(fog.cvars);
}

TEST(FogCvarsTest, InvalidDensityHandleReportsAContractViolationWithoutAnExpectedFailure) {
    FogConsole fog;
    auto invalid = fog.console.registerCvar(
        nx::console::CvarDesc<float>{.name = "unvalidatedDensity", .defaultValue = -1.0F});
    ASSERT_TRUE(invalid.has_value());
    fog.cvars.density = *invalid;
    setCvar(fog, "fog_enable", "1");

    expectObservedContract(fog.cvars);
}

} // namespace
