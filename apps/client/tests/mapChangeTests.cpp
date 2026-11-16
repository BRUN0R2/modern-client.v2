// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// How the map the client plays changes: `map` asks for a map, a frame tells that it is loading, the next
// loads it and plays it, and the commands after it on the line act on it from then on; `disconnect` leaves
// the map played, the commands after it acting on none; a map that cannot be loaded leaves the client where
// it was. The frames run as the client's do.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;
import app.client;

#include "consoleFixtures.hpp"
#include "logRecorder.hpp"

namespace {

using app::KeptMesh;
using app::MapSwitch;
using nx::console::ConsoleError;

/// The map the loader cannot load.
constexpr std::string_view kMissingMap = "missing";

/// A map played, of no game: its name, and whether its agents' mesh was read from the cache.
struct StandInMap {
    std::string name;
    KeptMesh kept = KeptMesh::Read;
};

/// A change of stand-in maps and the console that asks for it, with `bot_add` standing for the commands that
/// act on the map played: it records that map's name, empty for none.
class MapChangeTest : public ::testing::Test {
protected:
    MapChangeTest() {
        EXPECT_TRUE(change.registerCommands(console).has_value());
        EXPECT_TRUE(console
                        .registerCommand({.name = "bot_add", .description = "Records the map acted on."},
                                         [this](nx::console::Console&, const nx::console::CommandLine&)
                                             -> nx::Result<void, ConsoleError> {
                                             const StandInMap* map = change.played();
                                             actedOn.push_back(map != nullptr ? map->name : std::string{});
                                             return {};
                                         })
                        .has_value());
    }

    /// Runs `script` over the next `frames` frames.
    void play(std::string_view script, std::size_t frames) {
        console.addText(script);
        playFrames(frames);
    }

    /// The client's start: the player's files run, as nx::settings::runStartupFiles runs them, then the
    /// command line's commands queued after them.
    void start(std::string_view startupFiles, std::string_view commandLine) {
        console.addText(startupFiles);
        std::ignore = console.execute();
        console.addText(commandLine);
        std::ignore = console.execute();
    }

    /// The next `frames` frames, as the client's run plays them: the map told loading is loaded, the commands
    /// run but those a change of map holds, the switch due is made, and the frame drawn tells that the map
    /// asked for is loading.
    void playFrames(std::size_t frames) {
        for (std::size_t frame = 0; frame < frames; ++frame) {
            if (auto loaded = change.loadAsked(); !loaded) {
                loadFailures.push_back(loaded.error().code);
            }
            std::ignore = console.execute();
            if (const MapSwitch due = change.switchDue(); due != MapSwitch::Stay) {
                switches.push_back(due);
                change.switchMap();
            }
            change.frameShown();
        }
    }

    /// The name of the map played; none on the menu.
    [[nodiscard]] auto playedName() const -> std::optional<std::string> {
        const StandInMap* map = change.played();
        return map != nullptr ? std::optional{map->name} : std::nullopt;
    }

    nx::test::RecordingOutput output;
    nx::test::MemoryFiles files;
    nx::console::Console console{output, files};
    app::MapChange<StandInMap> change{
        [](std::string_view name, KeptMesh kept) -> nx::Result<std::unique_ptr<StandInMap>, ConsoleError> {
            if (name == kMissingMap) {
                return nx::fail(ConsoleError::CommandFailed, std::format("map {}", name));
            }
            return std::make_unique<StandInMap>(StandInMap{.name = std::string{name}, .kept = kept});
        }};
    std::vector<std::string> actedOn;
    std::vector<MapSwitch> switches;
    std::vector<ConsoleError> loadFailures;
};

TEST_F(MapChangeTest, AFrameTellsTheMapIsLoadingAndTheCommandsAfterItActOnIt) {
    play("map de_dust2; bot_add\n", 1);
    EXPECT_EQ(change.askedMap(), "de_dust2") << "the frame drawn tells that it is loading";
    EXPECT_EQ(playedName(), std::nullopt);
    EXPECT_TRUE(actedOn.empty()) << "bot_add waits for the map";

    playFrames(1);
    EXPECT_EQ(playedName(), "de_dust2") << "played from the frame that loads it";
    EXPECT_EQ(change.played()->kept, KeptMesh::Read);
    EXPECT_EQ(change.askedMap(), std::nullopt);
    EXPECT_TRUE(actedOn.empty()) << "bot_add waits for the switch to it";
    EXPECT_EQ(switches, std::vector{MapSwitch::Play});

    playFrames(1);
    EXPECT_EQ(actedOn, std::vector<std::string>{"de_dust2"}) << "bot_add acts on the map played";
}

TEST_F(MapChangeTest, TheCommandsAfterAMapThePlayersFilesAskForWaitForItAtTheStart) {
    start("map de_dust2; bot_add\n", "bot_add\nmap cs_italy; bot_add\n");
    EXPECT_TRUE(actedOn.empty()) << "the files' bot_add and the command line's wait for de_dust2";
    EXPECT_EQ(change.askedMap(), "de_dust2");

    playFrames(3);
    EXPECT_EQ(actedOn, (std::vector<std::string>{"de_dust2", "de_dust2"}));
    EXPECT_EQ(change.askedMap(), "cs_italy") << "the command line's map, asked for once de_dust2 is played";

    playFrames(2);
    EXPECT_EQ(actedOn, (std::vector<std::string>{"de_dust2", "de_dust2", "cs_italy"}));
    EXPECT_EQ(playedName(), "cs_italy");
    EXPECT_EQ(switches, (std::vector{MapSwitch::Play, MapSwitch::Play}));
}

TEST_F(MapChangeTest, TheCommandsAfterAMapTheCommandLineAsksForWaitForItAtTheStart) {
    start("bot_add\n", "map de_dust2; bot_add\n");
    EXPECT_EQ(actedOn, std::vector<std::string>{""}) << "the files' bot_add, before any map";
    EXPECT_EQ(change.askedMap(), "de_dust2");

    playFrames(3);
    EXPECT_EQ(actedOn, (std::vector<std::string>{"", "de_dust2"}));
    EXPECT_EQ(playedName(), "de_dust2");
}

TEST_F(MapChangeTest, AMapAskedForAfterAnotherIsAskedForOnceTheFirstIsPlayed) {
    play("map cs_italy; map de_dust2\n", 2);
    EXPECT_EQ(playedName(), "cs_italy");
    EXPECT_EQ(change.askedMap(), std::nullopt) << "de_dust2 waits for cs_italy to be played";

    playFrames(1);
    EXPECT_EQ(change.askedMap(), "de_dust2");

    playFrames(1);
    EXPECT_EQ(playedName(), "de_dust2");
    EXPECT_EQ(switches, (std::vector{MapSwitch::Play, MapSwitch::Play}));
}

TEST_F(MapChangeTest, DisconnectLeavesTheMapPlayedAndAMapAfterItIsAskedForThen) {
    play("disconnect\n", 1);
    EXPECT_TRUE(switches.empty()) << "nothing to leave";

    play("map cs_italy\n", 2);
    play("disconnect; map de_dust2; bot_add\n", 1);
    EXPECT_EQ(playedName(), std::nullopt) << "left at the frame's switch";
    EXPECT_EQ(change.askedMap(), std::nullopt) << "de_dust2 waits for cs_italy to be left";

    playFrames(1);
    EXPECT_EQ(change.askedMap(), "de_dust2");

    playFrames(2);
    EXPECT_EQ(playedName(), "de_dust2");
    EXPECT_EQ(actedOn, std::vector<std::string>{"de_dust2"});
    EXPECT_EQ(switches, (std::vector{MapSwitch::Play, MapSwitch::Leave, MapSwitch::Play}));
}

TEST_F(MapChangeTest, TheCommandsAfterDisconnectActOnNoMap) {
    play("map de_dust2; disconnect; bot_add\n", 4);
    EXPECT_EQ(playedName(), std::nullopt);
    EXPECT_EQ(actedOn, std::vector<std::string>{""}) << "bot_add acts on no map, once de_dust2 is left";
    EXPECT_EQ(switches, (std::vector{MapSwitch::Play, MapSwitch::Leave}));
}

TEST_F(MapChangeTest, AMapThatCannotBeLoadedLeavesTheClientWhereItWas) {
    play(std::format("map {}; bot_add\n", kMissingMap), 2);
    EXPECT_EQ(loadFailures, std::vector{ConsoleError::CommandFailed});
    EXPECT_EQ(playedName(), std::nullopt) << "still on the menu, without a map";
    EXPECT_EQ(change.askedMap(), std::nullopt);
    EXPECT_EQ(actedOn, std::vector<std::string>{""}) << "the commands after it wait no longer";
    EXPECT_TRUE(switches.empty());

    play("map cs_italy\n", 2);
    play(std::format("map {}; bot_add\n", kMissingMap), 2);
    EXPECT_EQ(loadFailures, (std::vector{ConsoleError::CommandFailed, ConsoleError::CommandFailed}));
    EXPECT_EQ(playedName(), "cs_italy") << "the map played stays";
    EXPECT_EQ(actedOn, (std::vector<std::string>{"", "cs_italy"}));
    EXPECT_EQ(switches, std::vector{MapSwitch::Play});
}

TEST_F(MapChangeTest, NavGeneratePlaysTheMapPlayedAgainItsMeshBuiltAgain) {
    play("nav_generate\n", 1);
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::CommandFailed}) << "no map to build again";

    play("map cs_italy\n", 2);
    play("nav_generate\n", 2);
    EXPECT_EQ(playedName(), "cs_italy");
    EXPECT_EQ(change.played()->kept, KeptMesh::Ignored);
    EXPECT_EQ(switches, (std::vector{MapSwitch::Play, MapSwitch::Play}));
}

TEST_F(MapChangeTest, NavGenerateAfterAMapBuildsThatMapAgain) {
    play("map cs_italy\n", 2);
    play("map de_dust2; nav_generate\n", 4);
    EXPECT_TRUE(output.failureCodes().empty());
    EXPECT_EQ(playedName(), "de_dust2") << "not cs_italy, the map played as the line was typed";
    EXPECT_EQ(change.played()->kept, KeptMesh::Ignored);
    EXPECT_EQ(switches, (std::vector{MapSwitch::Play, MapSwitch::Play, MapSwitch::Play}));
}

TEST_F(MapChangeTest, NavGenerateAfterDisconnectHasNoMapToBuild) {
    play("map cs_italy\n", 2);
    play("disconnect; nav_generate\n", 2);
    EXPECT_EQ(output.failureCodes(), std::vector{ConsoleError::CommandFailed});
    EXPECT_EQ(playedName(), std::nullopt);
    EXPECT_EQ(change.askedMap(), std::nullopt);
}

TEST_F(MapChangeTest, MapTakesOneName) {
    play("map\nmap de_dust2 cs_italy\nnav_generate now\n", 1);
    EXPECT_EQ(output.failureCodes(),
              (std::vector{ConsoleError::InvalidArguments, ConsoleError::InvalidArguments,
                           ConsoleError::InvalidArguments}));
    EXPECT_EQ(change.askedMap(), std::nullopt);
}

TEST_F(MapChangeTest, TheLoadingAndTheSwitchesAreLogged) {
    const nx::test::LogRecorder log;
    play("map de_dust2\n", 2);
    play("disconnect\n", 1);
    EXPECT_EQ(log.messagesAt(nx::log::Level::Info),
              (std::vector<std::string>{"loading the map de_dust2", "playing the map de_dust2",
                                        "left the map de_dust2"}));
}

} // namespace
