// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.console;

#include "consoleFixtures.hpp"

namespace {

using nx::console::Console;
using nx::console::ConsoleError;
using nx::console::CvarError;
using nx::console::LogLevelCvars;
using nx::log::Level;

constexpr nx::log::Category kNetLog{"net"};
constexpr nx::log::Category kAssetsLog{"assets"};
constexpr std::array kCategories{kNetLog, kAssetsLog};

class LogLevelsTest : public testing::Test {
protected:
    auto run(std::string_view script) -> void {
        console.addText(script);
        std::ignore = console.execute(); // the scripts here have no wait
    }

    nx_test::RecordingOutput output;
    nx_test::MemoryFiles files;
    Console console{output, files};
    nx::log::Logger logger;
};

TEST_F(LogLevelsTest, OneCvarPerCategoryStartingAtTheLoggerLevel) {
    logger.setCategoryLevel(kAssetsLog, Level::Warning);
    const auto levels = LogLevelCvars::registerIn(console, logger, kCategories);
    ASSERT_TRUE(levels.has_value());

    run("log_level_net; log_level_assets");
    EXPECT_EQ(output.lines,
              (std::vector<std::string>{R"("log_level_net" is "2")", R"("log_level_assets" is "3")"}));
}

TEST_F(LogLevelsTest, ChangesReachTheLoggerOnApply) {
    auto levels = LogLevelCvars::registerIn(console, logger, kCategories);
    ASSERT_TRUE(levels.has_value());

    run("log_level_net debug; log_level_assets 4");
    EXPECT_FALSE(logger.isEnabled(kNetLog, Level::Debug)) << "nothing changes before apply()";

    levels->apply();
    EXPECT_EQ(logger.categoryLevel(kNetLog), Level::Debug);
    EXPECT_EQ(logger.categoryLevel(kAssetsLog), Level::Error);
    EXPECT_EQ(logger.categoryLevel(nx::log::Category{"other"}), Level::Info);
}

TEST_F(LogLevelsTest, UnknownLevelsAreRefused) {
    auto levels = LogLevelCvars::registerIn(console, logger, kCategories);
    ASSERT_TRUE(levels.has_value());

    run("log_level_net loud; log_level_net 9");
    levels->apply();
    EXPECT_EQ(output.failureCodes(), (std::vector{ConsoleError::InvalidValue, ConsoleError::InvalidValue}));
    EXPECT_EQ(logger.categoryLevel(kNetLog), Level::Info);
}

TEST_F(LogLevelsTest, AFailureRegistersNoCvar) {
    // Regression: the categories before a bad one used to stay registered, bound to nothing.
    constexpr std::array kOneBad{kNetLog, nx::log::Category{"bad name"}};
    EXPECT_EQ(LogLevelCvars::registerIn(console, logger, kOneBad).error().code, CvarError::InvalidName);
    EXPECT_EQ(console.findCvar("log_level_net"), nullptr);

    constexpr std::array kTwice{kNetLog, kAssetsLog, nx::log::Category{"NET"}};
    EXPECT_EQ(LogLevelCvars::registerIn(console, logger, kTwice).error().code, CvarError::NameTaken);
    EXPECT_EQ(console.findCvar("log_level_net"), nullptr);
    EXPECT_EQ(console.findCvar("log_level_assets"), nullptr);
}

TEST_F(LogLevelsTest, CategoriesNeedNamesTheConsoleCanInvoke) {
    constexpr std::array kBadCategories{nx::log::Category{"bad name"}};
    EXPECT_EQ(LogLevelCvars::registerIn(console, logger, kBadCategories).error().code,
              CvarError::InvalidName);

    ASSERT_TRUE(LogLevelCvars::registerIn(console, logger, kCategories).has_value());
    EXPECT_EQ(LogLevelCvars::registerIn(console, logger, kCategories).error().code, CvarError::NameTaken);
}

} // namespace
