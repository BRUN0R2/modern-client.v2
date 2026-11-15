// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Where a sound is heard from, as the original's legacy stereo spatialization makes it: its volume
// falling in a straight line to nothing at its reach, the ear it is on getting more of it.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.math;
import nx.audio;

namespace {

using nx::Attenuation;
using nx::Gain;
using nx::audio::legacyGains;
using nx::audio::Listener;

/// The original's ATTN_NORM: a sound that fades to nothing 1250 units away.
constexpr Attenuation kNormal{0.8F};
constexpr float kTolerance = 1e-5F;

/// At the origin, looking along +x: its right is -y.
constexpr Listener kListener{.position = {}, .right = {.y = -1.0F}};

TEST(SpatialTest, ASoundAheadIsHeardTheSameInBothEarsLessAsItIsFarther) {
    const nx::audio::VoiceGains near = legacyGains(kListener, {.x = 1.0F}, Gain{1.0F}, kNormal);
    EXPECT_NEAR(near.left.value(), 1.0F - (kNormal.value() / 1000.0F), kTolerance);
    EXPECT_NEAR(near.right.value(), near.left.value(), kTolerance);
    // Half of its reach away: half of its volume.
    const nx::audio::VoiceGains half = legacyGains(kListener, {.x = 625.0F}, Gain{0.5F}, kNormal);
    EXPECT_NEAR(half.left.value(), 0.25F, kTolerance);
    EXPECT_NEAR(half.right.value(), 0.25F, kTolerance);
}

TEST(SpatialTest, ASoundToASideIsLouderInThatEarAndFainterInTheOther) {
    // 250 units to the right: 0.8 of it left, all of it on the right and none on the left.
    const nx::audio::VoiceGains right = legacyGains(kListener, {.y = -250.0F}, Gain{0.5F}, kNormal);
    EXPECT_NEAR(right.right.value(), 0.8F, kTolerance);
    EXPECT_EQ(right.left.value(), 0.0F);
    // Ahead and to the left, as far one way as the other: 1 + 0.707 in the left ear.
    const nx::audio::VoiceGains ahead =
        legacyGains(kListener, {.x = 100.0F, .y = 100.0F}, Gain{0.5F}, kNormal);
    const float left = 1.0F - (std::numbers::sqrt2_v<float> * 100.0F * kNormal.value() / 1000.0F);
    EXPECT_NEAR(ahead.left.value(), 0.5F * left * (1.0F + (std::numbers::sqrt2_v<float> / 2.0F)), kTolerance);
    EXPECT_NEAR(ahead.right.value(), 0.5F * left * (1.0F - (std::numbers::sqrt2_v<float> / 2.0F)),
                kTolerance);
}

TEST(SpatialTest, NoEarIsEverLouderThanTheSoundAtFullVolume) {
    const nx::audio::VoiceGains loud = legacyGains(kListener, {.y = -10.0F}, Gain{1.0F}, kNormal);
    EXPECT_EQ(loud.right.value(), 1.0F);
    EXPECT_EQ(loud.left.value(), 0.0F);
}

TEST(SpatialTest, ASoundBeyondItsReachIsNotHeard) {
    const nx::audio::VoiceGains far = legacyGains(kListener, {.x = 1250.0F}, Gain{1.0F}, kNormal);
    EXPECT_EQ(far.left.value(), 0.0F);
    EXPECT_EQ(far.right.value(), 0.0F);
    // The knife's draw, at 2.4, carries 416 units.
    const nx::audio::VoiceGains knife = legacyGains(kListener, {.x = 420.0F}, Gain{0.3F}, Attenuation{2.4F});
    EXPECT_EQ(knife.left.value(), 0.0F);
    EXPECT_EQ(knife.right.value(), 0.0F);
}

TEST(SpatialTest, ASoundThatDoesNotFadeIsHeardEverywhereTheSameInBothEars) {
    const nx::audio::VoiceGains gains = legacyGains(kListener, {.y = -5'000.0F}, Gain{0.7F}, Attenuation{});
    EXPECT_NEAR(gains.left.value(), 0.7F, kTolerance);
    EXPECT_NEAR(gains.right.value(), 0.7F, kTolerance);
}

TEST(SpatialTest, ASoundMadeWhereTheListenerIsIsHeardTheSameInBothEars) {
    const nx::audio::VoiceGains gains = legacyGains(kListener, {}, Gain{0.6F}, kNormal);
    EXPECT_NEAR(gains.left.value(), 0.6F, kTolerance);
    EXPECT_NEAR(gains.right.value(), 0.6F, kTolerance);
}

TEST(SpatialTest, TheListenersTurnDecidesTheEar) {
    // Turned to face -x, its right is +y: a sound at -y is now on its left.
    const Listener turned{.position = {}, .right = nx::angleVectors({.yaw = nx::Degrees{180.0F}}).right};
    const nx::audio::VoiceGains gains = legacyGains(turned, {.y = -250.0F}, Gain{0.5F}, kNormal);
    EXPECT_NEAR(gains.left.value(), 0.8F, kTolerance);
    EXPECT_NEAR(gains.right.value(), 0.0F, kTolerance);
}

} // namespace
