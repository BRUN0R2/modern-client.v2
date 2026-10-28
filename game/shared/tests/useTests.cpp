// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The repository's data/cstrike/use.toml, what a player hears as its use key goes down, as the
// original's CBasePlayer::PlayerUse and UseEmpty play it, and what its reader refuses.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import cs.shared;

namespace {

TEST(UseRulesTest, TheRepositorysUseSoundsAreTheOriginals) {
    const std::filesystem::path file = std::filesystem::path{NX_DATA_DIRECTORY} / "cstrike" / "use.toml";
    auto text = nx::platform::readTextFile(file);
    ASSERT_TRUE(text.has_value());
    auto rules = cs::readUseRules(*text, "use.toml");
    ASSERT_TRUE(rules.has_value()) << (rules ? "" : nx::describe(rules.error()));
    EXPECT_EQ(rules->found.files, (std::vector<std::string>{"common/wpn_select.wav"}));
    EXPECT_EQ(rules->found.channel, cs::SoundChannel::Item);
    EXPECT_EQ(rules->found.volume, 0.4F);
    EXPECT_EQ(rules->nothing.files, (std::vector<std::string>{"common/wpn_denyselect.wav"}));
    EXPECT_EQ(rules->nothing.channel, cs::SoundChannel::Item);
    EXPECT_EQ(rules->nothing.volume, 0.4F);
    EXPECT_EQ(rules->soundFiles().size(), 2U);
}

TEST(UseRulesTest, ASoundWithoutAFileIsRefused) {
    const auto rules = cs::readUseRules(R"(found = { files = ["a.wav"], channel = "item", volume = 0.4 }
nothing = { files = [], channel = "item", volume = 0.4 }
)",
                                        "use.toml");
    ASSERT_FALSE(rules.has_value());
    EXPECT_EQ(rules.error().code, cs::UseError::DataInvalid);
}

} // namespace
