#include <gtest/gtest.h>

import std;
import nx.core;

TEST(Source, FileNameDropsTheDirectories) {
    EXPECT_EQ(nx::sourceFileName(std::source_location::current()), "sourceTests.cpp");
}
