// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Which voice a sound takes, as the original's engine chooses a channel for one: a sound on a channel
// replaces the one played there, but on Auto; any other takes the voice with the least left to play.

#include <gtest/gtest.h>

import std;
import cs.shared;
import cs.client;

namespace {

using cs::SoundChannel;
using cs::voiceFor;
using cs::VoiceUse;

TEST(VoicesTest, ASoundOnAChannelReplacesTheOnePlayedThereThoughOthersAreDone) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Auto, .doneAt = 0},
                            VoiceUse{.channel = SoundChannel::Weapon, .doneAt = 900},
                            VoiceUse{.channel = SoundChannel::Body, .doneAt = 500}};
    EXPECT_EQ(voiceFor(voices, SoundChannel::Weapon), 1U);
    EXPECT_EQ(voiceFor(voices, SoundChannel::Body), 2U);
}

TEST(VoicesTest, ASoundOnAutoNeverReplacesOneByItsChannel) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Auto, .doneAt = 700},
                            VoiceUse{.channel = SoundChannel::Weapon, .doneAt = 300},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = 400}};
    EXPECT_EQ(voiceFor(voices, SoundChannel::Auto), 1U) << "the least left to play, whatever its channel";
}

TEST(VoicesTest, ASoundOnAChannelNothingPlaysTakesTheVoiceWithTheLeastLeft) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Weapon, .doneAt = 800},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = 200},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = 600}};
    EXPECT_EQ(voiceFor(voices, SoundChannel::Voice), 1U);
}

TEST(VoicesTest, AmongVoicesDoneTheSameTheFirstIsTaken) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Auto, .doneAt = 900},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = 100},
                            VoiceUse{.channel = SoundChannel::Item, .doneAt = 100}};
    EXPECT_EQ(voiceFor(voices, SoundChannel::Auto), 1U);
    EXPECT_EQ(voiceFor(voices, SoundChannel::Body), 1U);
}

TEST(VoicesTest, WhileEveryVoicePlaysTheOneDoneSoonestIsTaken) {
    // As the original's eight channels did, but with every voice of the mixer to choose from.
    std::vector<VoiceUse> voices;
    for (std::uint64_t voice = 0; voice < 64; ++voice) {
        voices.push_back({.channel = SoundChannel::Auto, .doneAt = 10'000 - voice});
    }
    EXPECT_EQ(voiceFor(voices, SoundChannel::Auto), 63U);
}

} // namespace
