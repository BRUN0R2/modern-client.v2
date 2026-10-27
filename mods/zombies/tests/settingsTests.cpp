// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>
import std;
import nx.core;
import nx.platform;
import mod.zombies;

TEST(ZombieSettings, ReadsTheExampleAndRejectsAnUnboundedPerceptionBudget) {
    auto text =
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "mods" / "zombies.toml");
    ASSERT_TRUE(text.has_value());
    auto read = zombies::readSettings(*text, "zombies.toml");
    ASSERT_TRUE(read.has_value()) << (read ? "" : nx::describe(read.error()));
    EXPECT_EQ(read->body.health, nx::HitPoints{500});
    const auto at = text->find("perceptionBudget = 8");
    ASSERT_NE(at, std::string::npos);
    text->replace(at, std::string_view{"perceptionBudget = 8"}.size(), "perceptionBudget = 0");
    const auto invalid = zombies::readSettings(*text, "zombies.toml");
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, zombies::ZombieError::InvalidData);
}
