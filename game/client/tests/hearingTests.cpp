// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// When a frame plays what the game made: each sound as long after the frame as it came after what the
// last frame showed, so that a frame that ran many ticks plays their sounds spread as they were made;
// and the moment of a play each client event of a view model comes at.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import cs.client;

namespace {

using nx::GameTime;
using nx::Seconds;

constexpr float kTolerance = 1e-5F;

TEST(HearingTest, ASoundIsHeardAsLongAfterTheFrameAsItCameAfterWhatTheLastFrameShowed) {
    // The last frame showed tick 100 and four tenths; this one ran ticks 101 to 121, a shot each ten.
    const cs::Hearing hearing{.from = GameTime::atTick(100, 0.4F), .rate = nx::kDefaultTickRate};
    EXPECT_NEAR(hearing.delayOf(GameTime::atTick(101)).count(), 0.006F, kTolerance);
    EXPECT_NEAR(hearing.delayOf(GameTime::atTick(111)).count(), 0.106F, kTolerance);
    EXPECT_NEAR(hearing.delayOf(GameTime::atTick(121)).count(), 0.206F, kTolerance)
        << "ten ticks apart as made, not all at once";
}

TEST(HearingTest, WhatCameBeforeWhatTheLastFrameShowedIsHeardAtOnce) {
    const cs::Hearing hearing{.from = GameTime::atTick(100, 0.4F), .rate = nx::kDefaultTickRate};
    EXPECT_EQ(hearing.delayOf(GameTime::atTick(100)), Seconds{});
    EXPECT_EQ(hearing.delayOf(GameTime::atTick(90)), Seconds{});
}

[[nodiscard]] auto sequenceOf(float fps, bool isLooping) -> nx::assets::StudioSequence {
    nx::assets::StudioSequence made;
    made.fps = fps;
    made.isLooping = isLooping;
    made.frameCount = 31;
    return made;
}

TEST(HearingTest, AnEventOfAPlayComesAsItsFrameDoes) {
    // At 30 frames a second, frame 12 comes 0.4 seconds into the play, however far the frame shown is.
    const nx::assets::StudioSequence shot = sequenceOf(30.0F, false);
    EXPECT_NEAR(cs::intoPlayOf(shot, 12.0F, 15.0F, Seconds{0.5F}).count(), 0.4F, kTolerance);
    EXPECT_NEAR(cs::intoPlayOf(shot, 0.0F, 1.5F, Seconds{0.05F}).count(), 0.0F, kTolerance);
}

TEST(HearingTest, AnEventOfALoopComesInTheTurnShown) {
    // Thirty frames a turn at 30 a second: 2.5 seconds in, the third turn shows frame 15; its frame 12
    // came 0.1 seconds before.
    const nx::assets::StudioSequence idle = sequenceOf(30.0F, true);
    EXPECT_NEAR(cs::intoPlayOf(idle, 12.0F, 15.0F, Seconds{2.5F}).count(), 2.4F, kTolerance);
}

TEST(HearingTest, AnEventOfASequenceOfNoRateComesAtItsStart) {
    EXPECT_EQ(cs::intoPlayOf(sequenceOf(0.0F, false), 0.0F, 0.0F, Seconds{1.0F}), Seconds{});
}

} // namespace
