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
using nx::platform::kWakeLatenessStep;
using nx::platform::MonotonicClock;
using nx::platform::SleepTimer;
using nx::platform::Stopwatch;
using nx::platform::WakeLateness;

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
    auto timer = makeTimer();
    ASSERT_TRUE(timer.has_value());
    for (const auto wait : {0us, 50us, 500us, 1000us, 2500us}) {
        const Instant deadline = MonotonicClock::now() + wait;
        timer->sleepUntil(deadline);
        EXPECT_GE(MonotonicClock::now(), deadline) << wait;
    }
}

TEST(SleepTimerTest, APassedDeadlineReturnsAtOnce) {
    auto timer = makeTimer();
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

    auto timer = makeTimer();
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

TEST(SleepTimerTest, StartsFramesOfAMillisecondOnTime) {
    // fps_max 1000: a frame every millisecond, half of it working. The OS's timer alone wakes 0.4 to
    // 0.8 ms late on some systems; the frames must start within a few tens of microseconds instead.
    constexpr auto kFrame = 1ms;
    constexpr auto kWork = 500us;
    constexpr int kFrames = 200;
    constexpr auto kMedianLatenessLimit = 100us;

    auto timer = makeTimer();
    ASSERT_TRUE(timer.has_value());
    std::vector<MonotonicClock::duration> lateness;
    Instant deadline = MonotonicClock::now();
    for (int frame = 0; frame < kFrames; ++frame) {
        deadline += kFrame;
        timer->sleepUntil(deadline);
        const Instant started = MonotonicClock::now();
        lateness.push_back(started - deadline);
        while (MonotonicClock::now() - started < kWork) {
        }
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

TEST(WakeLatenessTest, TheFirstWakeSetsTheMargin) {
    WakeLateness lateness;
    EXPECT_EQ(lateness.margin(), MonotonicClock::duration::zero()) << "nothing to wake early by yet";
    lateness.observe(640us);
    EXPECT_EQ(lateness.margin(), 640us);
}

TEST(WakeLatenessTest, AnEarlyWakeCountsAsOnTime) {
    WakeLateness lateness;
    lateness.observe(-30us);
    EXPECT_EQ(lateness.margin(), MonotonicClock::duration::zero());
}

TEST(WakeLatenessTest, AWakeFarLaterThanTheOthersMovesTheMarginOneStep) {
    WakeLateness lateness;
    lateness.observe(600us);
    lateness.observe(15ms); // a thread the OS put aside
    EXPECT_GT(lateness.margin(), 600us);
    EXPECT_LE(lateness.margin(), 600us + kWakeLatenessStep);
}

TEST(WakeLatenessTest, SettlesWhereOneWakeInAHundredIsLater) {
    // Wakes late by 0 to 999 us, evenly, many times over: the margin settles near 990 us, within a
    // few steps.
    constexpr int kRounds = 300;
    constexpr int kLatenesses = 1000;
    constexpr int kScatter = 383;
    constexpr auto kSettled = 990us;
    constexpr auto kTolerance = 3 * kWakeLatenessStep;
    WakeLateness lateness;
    for (int round = 0; round < kRounds; ++round) {
        for (int late = 0; late < kLatenesses; ++late) {
            // 0, 383, 766, 149, ... visits each lateness once a round, scattered (383 and 1000 share no
            // factor).
            lateness.observe(std::chrono::microseconds{(late * kScatter) % kLatenesses});
        }
    }
    EXPECT_GT(lateness.margin(), kSettled - kTolerance);
    EXPECT_LT(lateness.margin(), kSettled + kTolerance);
}

TEST(WakeLatenessTest, FallsSlowlyOnceTheWakesAreOnTime) {
    WakeLateness lateness;
    lateness.observe(800us);
    lateness.observe(0us);
    EXPECT_LT(lateness.margin(), 800us);
    EXPECT_GT(lateness.margin(), 800us - kWakeLatenessStep) << "a wake on time moves it less than a step";
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
