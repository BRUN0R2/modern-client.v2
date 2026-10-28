// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.platform;

namespace {

using nx::platform::Thread;

/// A body that says it runs, then sleeps until its stop is asked, and says whether it saw it.
struct WaitingBody {
    std::atomic<bool>* hasRun;
    std::atomic<bool>* sawStop;

    void operator()(const std::stop_token& stop) const {
        hasRun->store(true);
        hasRun->notify_one();
        std::mutex mutex;
        std::condition_variable_any woken;
        std::unique_lock lock{mutex};
        std::ignore = woken.wait(lock, stop, [] { return false; }); // until the stop is asked
        sawStop->store(stop.stop_requested());
    }
};

TEST(ThreadTest, RunsItsBodyAndAsksItToStopAsItEnds) {
    std::atomic<bool> hasRun{false};
    std::atomic<bool> sawStop{false};
    {
        const Thread thread{"nxThreadTest", WaitingBody{.hasRun = &hasRun, .sawStop = &sawStop}};
        hasRun.wait(false);
    }
    EXPECT_TRUE(hasRun.load());
    EXPECT_TRUE(sawStop.load());
}

TEST(ThreadTest, StopsWhenAsked) {
    std::atomic<bool> hasRun{false};
    std::atomic<bool> sawStop{false};
    Thread thread{"nxThreadTest", WaitingBody{.hasRun = &hasRun, .sawStop = &sawStop}};
    hasRun.wait(false);
    thread.requestStop();
    thread = Thread{}; // ends it
    EXPECT_TRUE(sawStop.load());
}

TEST(ThreadTest, AMovedThreadRunsOnUntilItsNewOwnerEnds) {
    std::atomic<bool> hasRun{false};
    std::atomic<bool> sawStop{false};
    Thread first{"nxThreadTest", WaitingBody{.hasRun = &hasRun, .sawStop = &sawStop}};
    hasRun.wait(false);
    {
        const Thread second{std::move(first)};
        first = Thread{}; // owns nothing: ends nothing
        EXPECT_FALSE(sawStop.load());
    }
    EXPECT_TRUE(sawStop.load());
}

} // namespace
