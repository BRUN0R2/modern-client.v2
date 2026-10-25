// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The client's cvars: the original's names and starting values, what config.cfg keeps of them, the
// ranges the original holds the picture's cvars to, and r_decals held as a game of more than one
// player holds it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import nx.settings;

namespace {

/// A console's output that drops everything.
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

TEST(ClientCvarsTest, StartAsCounterStrikeStarts) {
    SilentOutput output;
    NoFiles files;
    nx::console::Console console{output, files};
    const auto cvars = nx::settings::registerClientCvars(console);
    ASSERT_TRUE(cvars.has_value()) << nx::describe(cvars.error());

    EXPECT_FLOAT_EQ(cvars->mouse.sensitivity.value(), 3.0F);
    EXPECT_FLOAT_EQ(cvars->mouse.yaw.value(), 0.022F);
    EXPECT_FLOAT_EQ(cvars->mouse.pitch.value(), 0.022F);
    EXPECT_FLOAT_EQ(cvars->video.fpsMax.value(), 100.0F);
    EXPECT_TRUE(cvars->video.vsync.value());
    EXPECT_FLOAT_EQ(cvars->video.gamma.value(), 2.5F);
    EXPECT_FLOAT_EQ(cvars->video.brightness.value(), 1.0F);
    EXPECT_FLOAT_EQ(cvars->video.textureGamma.value(), 2.0F);
    EXPECT_FLOAT_EQ(cvars->video.lightGamma.value(), 2.5F);
    EXPECT_TRUE(cvars->video.hudDraw.value());
    EXPECT_EQ(cvars->video.decals.value(), 4096);
    EXPECT_EQ(cvars->video.multiplayerDecals.value(), 300);
    EXPECT_TRUE(cvars->player.autoWeaponSwitch.value());
    EXPECT_FLOAT_EQ(cvars->audio.volume.value(), 0.7F);
    EXPECT_EQ(cvars->mouse.yaw.name(), "m_yaw");
    EXPECT_EQ(cvars->video.vsync.name(), "gl_vsync");

    // What config.cfg keeps: what the original archives, not texgamma nor lightgamma.
    const std::string archived = console.archivedCvars();
    for (const std::string_view name :
         {"sensitivity", "m_yaw", "m_pitch", "fps_max", "gl_vsync", "gamma", "brightness", "hud_draw",
          "r_decals", "mp_decals", "_cl_autowepswitch", "volume"}) {
        EXPECT_NE(archived.find(std::format("{} \"", name)), std::string::npos) << name;
    }
    EXPECT_EQ(archived.find("texgamma"), std::string::npos);
    EXPECT_EQ(archived.find("lightgamma"), std::string::npos);
}

TEST(ClientCvarsTest, ThePictureStaysInTheOriginalsRanges) {
    SilentOutput output;
    NoFiles files;
    nx::console::Console console{output, files};
    const auto cvars = nx::settings::registerClientCvars(console);
    ASSERT_TRUE(cvars.has_value());

    EXPECT_FALSE(console.setCvar("gamma", "4").has_value());
    EXPECT_FALSE(console.setCvar("gamma", "1.7").has_value());
    EXPECT_TRUE(console.setCvar("gamma", "1.8").has_value());
    EXPECT_FALSE(console.setCvar("brightness", "-1").has_value());
    EXPECT_FALSE(console.setCvar("fps_max", "-1").has_value());
    EXPECT_TRUE(console.setCvar("fps_max", "0").has_value()) << "no limit";
    EXPECT_TRUE(console.setCvar("m_pitch", "-0.022").has_value()) << "an inverted mouse";
}

TEST(ClientCvarsTest, AGameOfMorePlayersHoldsDecalsToMpDecals) {
    SilentOutput output;
    NoFiles files;
    nx::console::Console console{output, files};
    const auto cvars = nx::settings::registerClientCvars(console);
    ASSERT_TRUE(cvars.has_value());
    EXPECT_FALSE(console.setCvar("r_decals", "-1").has_value());

    ASSERT_TRUE(nx::settings::enterMultiplayerGame(cvars->video).has_value());
    EXPECT_EQ(cvars->video.decals.value(), 300) << "4096 held to mp_decals' 300";
    ASSERT_TRUE(console.setCvar("r_decals", "100").has_value());
    ASSERT_TRUE(nx::settings::enterMultiplayerGame(cvars->video).has_value());
    EXPECT_EQ(cvars->video.decals.value(), 100) << "below it, as it was";
    ASSERT_TRUE(console.setCvar("mp_decals", "50").has_value());
    ASSERT_TRUE(nx::settings::enterMultiplayerGame(cvars->video).has_value());
    EXPECT_EQ(cvars->video.decals.value(), 50);
}

} // namespace
