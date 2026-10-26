// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;

namespace {

using namespace std::chrono_literals;
using nx::platform::FrameRateMeter;
using nx::platform::Instant;
using nx::platform::MonotonicClock;
using nx::platform::SleepTimer;
using nx::platform::Stopwatch;

[[nodiscard]] auto makeTimer() -> std::optional<SleepTimer> {
    auto created = SleepTimer::create();
    if (!created) {
        ADD_FAILURE() << nx::describe(created.error());
        return std::nullopt;
    }
    return std::move(*created);
}

TEST(ClockTest, TheMonotonicClockIsSteady) {
    static_assert(MonotonicClock::is_steady);
    const Instant before = MonotonicClock::now();
    EXPECT_LE(before, MonotonicClock::now());
}

TEST(StopwatchTest, MeasuresAndRestarts) {
    Stopwatch stopwatch;
    std::this_thread::sleep_for(2ms);
    const auto first = stopwatch.elapsed();
    EXPECT_GE(first, 2ms);

    const auto lap = stopwatch.restart();
    EXPECT_GE(lap, first);
    EXPECT_LT(stopwatch.elapsed(), lap) << "restarting starts a new measurement";
}

TEST(SleepTimerTest, NeverWakesBeforeTheDeadline) {
    const auto timer = makeTimer();
    ASSERT_TRUE(timer.has_value());
    for (const auto wait : {0us, 50us, 500us, 1000us, 2500us}) {
        const Instant deadline = MonotonicClock::now() + wait;
        timer->sleepUntil(deadline);
        EXPECT_GE(MonotonicClock::now(), deadline) << wait;
    }
}

TEST(SleepTimerTest, APassedDeadlineReturnsAtOnce) {
    const auto timer = makeTimer();
    ASSERT_TRUE(timer.has_value());
    const Stopwatch stopwatch;
    timer->sleepUntil(MonotonicClock::now() - 1s);
    EXPECT_LT(stopwatch.elapsed(), 100ms);
}

TEST(SleepTimerTest, PacesAFixedRateLoop) {
    // Eleven ticks at 500 Hz. A sleep rounded to the default Windows scheduler period (15.6 ms)
    // would be about 13 ms late every tick; the median lateness must stay below one tick.
    constexpr auto kTick = 2ms;
    constexpr int kTicks = 11;
    constexpr auto kMedianLatenessLimit = 2ms;

    const auto timer = makeTimer();
    ASSERT_TRUE(timer.has_value());
    std::vector<MonotonicClock::duration> lateness;
    Instant deadline = MonotonicClock::now();
    for (int tick = 0; tick < kTicks; ++tick) {
        deadline += kTick;
        timer->sleepUntil(deadline);
        lateness.push_back(MonotonicClock::now() - deadline);
    }
    std::ranges::sort(lateness);
    const auto median = lateness[lateness.size() / 2];
    EXPECT_LT(median, kMedianLatenessLimit)
        << std::chrono::duration_cast<std::chrono::microseconds>(median).count() << " us";
}

TEST(SleepTimerTest, MovesWithItsTimer) {
    auto timer = makeTimer();
    ASSERT_TRUE(timer.has_value());
    SleepTimer moved = std::move(*timer);
    const Instant deadline = MonotonicClock::now() + 1ms;
    moved.sleepUntil(deadline);
    EXPECT_GE(MonotonicClock::now(), deadline);
}

TEST(FrameRateMeterTest, TellsTheFramesASecondOfEachWholeSecond) {
    FrameRateMeter meter;
    const Instant start{};
    meter.frameStarted(start);
    for (int frame = 1; frame < 100; ++frame) {
        meter.frameStarted(start + (frame * 10ms));
    }
    EXPECT_FALSE(meter.framesPerSecond().has_value()) << "99 frames after the first span 0.99 s";
    meter.frameStarted(start + 1s);
    ASSERT_TRUE(meter.framesPerSecond().has_value());
    EXPECT_DOUBLE_EQ(*meter.framesPerSecond(), 100.0) << "100 frames after the first, in a second";

    // The next window starts with the frame that closed the last: its rate stays until it closes too.
    for (int frame = 1; frame <= 3; ++frame) {
        meter.frameStarted(start + 1s + (frame * 400ms));
    }
    EXPECT_DOUBLE_EQ(*meter.framesPerSecond(), 2.5) << "3 frames in 1.2 s";
}

TEST(FrameRateMeterTest, AFrameLongerThanAWindowClosesItAlone) {
    FrameRateMeter meter;
    const Instant start{};
    meter.frameStarted(start);
    meter.frameStarted(start + 4s);
    ASSERT_TRUE(meter.framesPerSecond().has_value());
    EXPECT_DOUBLE_EQ(*meter.framesPerSecond(), 0.25) << "a map that took 4 s to load in a frame";
}

TEST(WallClockTest, IsTheSystemClock) {
    static_assert(std::same_as<nx::platform::WallClock, std::chrono::system_clock>);
    EXPECT_GT(nx::platform::WallClock::now().time_since_epoch().count(), 0);
}

} // namespace
