// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The fog's cvars, the fog they ask for, and the fog a frame shows as they change.

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

/// A console and the fog's cvars in it.
struct FogConsole {
    SilentOutput output;
    NoFiles files;
    nx::console::Console console{output, files};
    cs::FogCvars cvars = cs::FogCvars::registerIn(console).value();

    void set(std::string_view name, std::string_view value) {
        EXPECT_TRUE(console.setCvar(name, value).has_value()) << name << " " << value;
    }
};

TEST(FogCvarsTest, NoFogShowsUntilFogEnableAndTheyAreNotArchived) {
    FogConsole fog;
    const auto none = fog.cvars.fog();
    ASSERT_TRUE(none.has_value());
    EXPECT_FALSE(none->has_value());
    EXPECT_EQ(fog.console.archivedCvars().find("fog_"), std::string::npos)
        << "a map's or a mod's, not the player's";
    fog.set("fog_enable", "1");
    fog.set("fog_color", "51 102 255");
    fog.set("fog_curve", "linear");
    fog.set("fog_start", "100");
    fog.set("fog_end", "400");
    fog.set("fog_opacity", "0.5");
    const auto asked = fog.cvars.fog();
    ASSERT_TRUE(asked.has_value() && asked->has_value());
    const nx::render::Fog& made = **asked;
    EXPECT_EQ(made.color, (nx::render::FogColor{.red = 0.2F, .green = 0.4F, .blue = 1.0F}));
    EXPECT_EQ(made.curve, nx::render::FogCurve::Linear);
    EXPECT_EQ(made.start, nx::HammerUnits{100.0F});
    EXPECT_EQ(made.end, nx::HammerUnits{400.0F});
    EXPECT_EQ(made.opacity, 0.5F);
    fog.set("fog_end", "0");
    EXPECT_FALSE(fog.cvars.fog().has_value()) << "a linear fog without end";
    fog.set("fog_curve", "exp2");
    const auto endless = fog.cvars.fog();
    ASSERT_TRUE(endless.has_value() && endless->has_value());
    EXPECT_EQ((*endless)->end, nx::render::kEndlessFog) << "fog_end 0, without end";
    EXPECT_FALSE(fog.console.setCvar("fog_curve", "cubic").has_value());
    EXPECT_FALSE(fog.console.setCvar("fog_opacity", "2").has_value());
}

TEST(FogControlTest, AFogTheCvarsAskForComesInOverTheirFadeAndOneTheyMakeNoneOfLeavesItAsItIs) {
    FogConsole fog;
    cs::FogControl control;
    fog.set("fog_fade", "1");
    control.update(fog.cvars, nx::Seconds{0.5F});
    EXPECT_FALSE(control.shown().has_value());
    fog.set("fog_enable", "1");
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
    fog.set("fog_start", "500");
    fog.set("fog_end", "100");
    control.update(fog.cvars, nx::Seconds{0.5F});
    EXPECT_EQ(control.shown(), arrived);
    fog.set("fog_enable", "0");
    control.update(fog.cvars, nx::Seconds{1.0F});
    EXPECT_FALSE(control.shown().has_value()) << "gone back out over its fade";
}

} // namespace
