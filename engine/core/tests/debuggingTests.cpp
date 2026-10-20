// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;

TEST(Debugging, BreakpointIfDebuggingReturnsWithoutADebugger) {
    // Under a debugger this stops once and resumes; under ctest nothing happens.
    if (!nx::isDebuggerPresent()) {
        nx::breakpointIfDebugging();
    }
    SUCCEED();
}

TEST(Debugging, StackTraceHasOneIndentedLinePerFrame) {
    const std::string trace = nx::currentStackTrace();

    ASSERT_FALSE(trace.empty());
    EXPECT_TRUE(trace.ends_with('\n'));
    for (const auto line : std::views::split(std::string_view{trace}, '\n')) {
        const std::string_view frame{line.begin(), line.end()};
        if (!frame.empty()) {
            EXPECT_TRUE(frame.starts_with("    ")) << frame;
        }
    }
}
