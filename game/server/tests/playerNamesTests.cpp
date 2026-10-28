// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The names the server gives its players (docs/10, section 2): SV_ExtractFromUserinfo's blanks and
// SV_CheckForDuplicateNames' "(1)", "(2)"... in any case.

#include <gtest/gtest.h>

import std;
import cs.server;

namespace {

[[nodiscard]] auto nameOf(std::string_view requested, std::initializer_list<std::string_view> taken = {})
    -> std::string {
    return cs::playerNameOf(requested, std::span{taken.begin(), taken.size()});
}

TEST(PlayerNamesTest, ANameIsBlankedWhereTheEngineReadsItsOwnAndTrimmed) {
    EXPECT_EQ(nameOf("Player"), "Player");
    EXPECT_EQ(nameOf("  Bruno  "), "Bruno");
    EXPECT_EQ(nameOf("50%&fun"), "50  fun");
    EXPECT_EQ(nameOf("##12"), "12") << "no player's number";
    EXPECT_EQ(nameOf("a#b"), "a#b") << "but where it starts";
}

TEST(PlayerNamesTest, ANameTheEngineCannotHoldIsUnnamed) {
    for (const std::string_view requested :
         {"", "   ", "console", "CONSOLE", "../maps", "say\"hi", "a\\b", "%&"}) {
        EXPECT_EQ(nameOf(requested), cs::kUnnamed) << requested;
    }
}

TEST(PlayerNamesTest, ANameAnotherHasInAnyCaseIsNumberedUntilNoneHas) {
    EXPECT_EQ(nameOf("Player", {"player"}), "(1)Player");
    EXPECT_EQ(nameOf("Player", {"Player", "(1)Player", "(2)PLAYER"}), "(3)Player");
    EXPECT_EQ(nameOf("", {"unnamed"}), "(1)unnamed");
    EXPECT_EQ(nameOf("Bruno", {"Player"}), "Bruno");
}

} // namespace
