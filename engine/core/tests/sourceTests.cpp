// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include <gtest/gtest.h>

import std;
import nx.core;

TEST(Source, FileNameDropsTheDirectories) {
    EXPECT_EQ(nx::sourceFileName(std::source_location::current()), "sourceTests.cpp");
}
