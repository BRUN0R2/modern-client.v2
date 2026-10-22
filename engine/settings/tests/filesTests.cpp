// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The player's configuration files, in a folder of their own: what exec may read, the order they run
// in as the game starts, and config.cfg written and read back.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.console;
import nx.settings;

#include "temporaryDirectory.hpp"

namespace {

using nx::console::ConfigFileError;
using nx::console::Console;
using nx::console::CvarDesc;
using nx::console::CvarFlag;
using nx::settings::UserConfigFiles;

/// A console's output, kept.
class RecordingOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view line) override { lines.emplace_back(line); }
    void reportFailure(const nx::Failure<nx::console::ConsoleError>& failure) override {
        failures.push_back(failure.code);
    }

    std::vector<std::string> lines;
    std::vector<nx::console::ConsoleError> failures;
};

/// A console reading the files of `directory`, with an archived cvar `order` and a cvar `kept`
/// config.cfg does not save.
class Profile {
public:
    explicit Profile(const std::filesystem::path& directory)
        : files{directory}, console{output, files},
          order{console.registerCvar(CvarDesc<std::int32_t>{.name = "order", .flags = {CvarFlag::Archive}})
                    .value()},
          kept{console.registerCvar(CvarDesc<std::int32_t>{.name = "kept"}).value()} {}

    RecordingOutput output;
    UserConfigFiles files;
    Console console;
    nx::console::Cvar<std::int32_t> order;
    nx::console::Cvar<std::int32_t> kept;
};

TEST(UserConfigFilesTest, ReadsOnlyCfgFilesInsideItsFolder) {
    const nx::test::TemporaryDirectory folder;
    folder.write("config.cfg", "order 1\n");
    folder.write("maps/de_dust2.CFG", "order 2\n");
    folder.write("notes.txt", "order 3\n");
    UserConfigFiles files{folder.path()};

    EXPECT_EQ(files.read("config.cfg").value_or(""), "order 1\n");
    EXPECT_EQ(files.read("maps/de_dust2.CFG").value_or(""), "order 2\n") << "any case";
    const auto errorOf = [&files](std::string_view path) {
        const auto read = files.read(path);
        return read ? std::nullopt : std::optional{read.error().code};
    };
    EXPECT_EQ(errorOf("notes.txt"), ConfigFileError::InvalidPath) << "not a .cfg file";
    EXPECT_EQ(errorOf("../config.cfg"), ConfigFileError::InvalidPath) << "out of the folder";
    EXPECT_EQ(errorOf(nx::platform::toUtf8(folder.path() / "config.cfg")), ConfigFileError::InvalidPath)
        << "absolute";
    EXPECT_EQ(errorOf(""), ConfigFileError::InvalidPath);
    EXPECT_EQ(errorOf("autoexec.cfg"), ConfigFileError::NotFound);
}

TEST(SettingsFilesTest, TheStartupFilesRunInOrderThoseMissingSkipped) {
    const nx::test::TemporaryDirectory folder;
    folder.write("config.cfg", "order 1\nkept 1\n");
    folder.write("autoexec.cfg", "order 3\n");
    Profile setup{folder.path()};

    nx::settings::runStartupFiles(setup.console, setup.files);
    EXPECT_EQ(setup.order.value(), 3) << "autoexec.cfg last";
    EXPECT_EQ(setup.kept.value(), 1);
    EXPECT_TRUE(setup.output.lines.empty()) << "no userconfig.cfg is no failure";
    EXPECT_TRUE(setup.output.failures.empty());

    folder.write("userconfig.cfg", "order 2\nkept 2\n");
    folder.write("autoexec.cfg", "");
    nx::settings::runStartupFiles(setup.console, setup.files);
    EXPECT_EQ(setup.order.value(), 2) << "userconfig.cfg after config.cfg";
    EXPECT_EQ(setup.kept.value(), 2);
}

TEST(SettingsFilesTest, ConfigCfgKeepsTheArchivedCvarsForTheNextStart) {
    const nx::test::TemporaryDirectory folder;
    const std::filesystem::path profile = folder.path() / "Modern-Client.v2";
    {
        Profile setup{profile};
        ASSERT_TRUE(setup.order.set(7).has_value());
        ASSERT_TRUE(setup.kept.set(7).has_value());
        const auto saved = nx::settings::saveConfig(setup.console, setup.files);
        ASSERT_TRUE(saved.has_value()) << nx::describe(saved.error());
    }
    const auto text = nx::platform::readTextFile(profile / "config.cfg");
    ASSERT_TRUE(text.has_value()) << "the folder made";
    EXPECT_TRUE(text->starts_with("//"));
    EXPECT_NE(text->find("order \"7\""), std::string::npos);
    EXPECT_EQ(text->find("kept"), std::string::npos) << "not archived";
    EXPECT_FALSE(std::filesystem::exists(profile / "config.cfg.partial"));

    Profile next{profile};
    nx::settings::runStartupFiles(next.console, next.files);
    EXPECT_EQ(next.order.value(), 7);
    EXPECT_EQ(next.kept.value(), 0);
}

} // namespace
