// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

namespace {

TEST(Text, LowersOnlyAsciiCapitals) {
    static_assert(nx::toLowerAscii('A') == 'a');
    static_assert(nx::toLowerAscii('Z') == 'z');
    static_assert(nx::toLowerAscii('a') == 'a');
    static_assert(nx::toLowerAscii('_') == '_');
    static_assert(nx::toLowerAscii('@') == '@'); // the byte before 'A'
    static_assert(nx::toLowerAscii('[') == '['); // the byte after 'Z'
    EXPECT_EQ(nx::toLowerAscii('\xC9'), '\xC9') << "bytes beyond ASCII stay as they are";
}

TEST(Text, WordsBounds) {
    EXPECT_EQ(nx::boundsText<int>(0, 100), "in [0, 100]");
    EXPECT_EQ(nx::boundsText<double>(0.5, std::nullopt), "of at least 0.5");
    EXPECT_EQ(nx::boundsText<float>(std::nullopt, 2.5F), "of at most 2.5");
    EXPECT_EQ(nx::boundsText<int>(std::nullopt, std::nullopt), "");
}

} // namespace
