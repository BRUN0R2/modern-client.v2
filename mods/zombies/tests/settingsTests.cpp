// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>
import std;
import nx.core;
import nx.platform;
import cs.shared;
import mod.zombies;

TEST(ZombieSettings, ReadsTheExampleAndRejectsAnUnboundedPerceptionBudget) {
    auto text =
        nx::platform::readTextFile(std::filesystem::path{NX_DATA_DIRECTORY} / "mods" / "zombies.toml");
    ASSERT_TRUE(text.has_value());
    auto read = zombies::readSettings(*text, "zombies.toml");
    ASSERT_TRUE(read.has_value()) << (read ? "" : nx::describe(read.error()));
    EXPECT_EQ(read->body.health, nx::HitPoints{500});
    constexpr std::uint8_t kRed = 247; // BLOOD_COLOR_RED
    EXPECT_EQ(read->body.bloodColor, kRed) << "it bleeds red";
    ASSERT_TRUE(read->body.knockback.has_value()) << "shots knock it back";
    EXPECT_TRUE(cs::isValid(*read->body.knockback));
    EXPECT_EQ(read->body.stride, nx::HammerUnits{56}) << "its steps are heard";
    EXPECT_EQ(read->spawnBatchLimit, 64U);
    EXPECT_EQ(read->spawnRadius, nx::HammerUnits{512});
    const auto at = text->find("perceptionBudget = 8");
    ASSERT_NE(at, std::string::npos);
    text->replace(at, std::string_view{"perceptionBudget = 8"}.size(), "perceptionBudget = 0");
    const auto invalid = zombies::readSettings(*text, "zombies.toml");
    ASSERT_FALSE(invalid.has_value());
    EXPECT_EQ(invalid.error().code, zombies::ZombieError::InvalidData);
}
