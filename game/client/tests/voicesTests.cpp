// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Which voice a sound takes, as the original's engine chooses a channel for one: a sound on a channel of
// its maker replaces the one its maker plays there, but on Auto; any other takes the voice with the least
// left to play; another's sound never cuts short one of the player's own.

#include <gtest/gtest.h>

import std;
import nx.audio;
import cs.shared;
import cs.client;

namespace {

using cs::PlayerIndex;
using cs::SoundChannel;
using cs::voiceFor;
using cs::VoiceUse;
using nx::audio::MixFrames;

/// The frame of the mix the choices are made at.
constexpr MixFrames kNow{1'000};
constexpr PlayerIndex kOther{3};
constexpr PlayerIndex kAnother{5};

TEST(VoicesTest, ASoundOnAChannelReplacesTheOnePlayedThereThoughOthersAreDone) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{0}},
                            VoiceUse{.channel = SoundChannel::Weapon, .doneAt = MixFrames{1'900}},
                            VoiceUse{.channel = SoundChannel::Body, .doneAt = MixFrames{1'500}}};
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Weapon, kNow), 1U);
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Body, kNow), 2U);
}

TEST(VoicesTest, ASoundOnAutoNeverReplacesOneByItsChannel) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{700}},
                            VoiceUse{.channel = SoundChannel::Weapon, .doneAt = MixFrames{300}},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{400}}};
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Auto, kNow), 1U)
        << "the least left to play, whatever its channel";
}

TEST(VoicesTest, ASoundOnAChannelNothingPlaysTakesTheVoiceWithTheLeastLeft) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Weapon, .doneAt = MixFrames{1'800}},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{1'200}},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{1'600}}};
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Voice, kNow), 1U);
}

TEST(VoicesTest, AmongVoicesDoneTheSameTheFirstIsTaken) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{900}},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{100}},
                            VoiceUse{.channel = SoundChannel::Item, .doneAt = MixFrames{100}}};
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Auto, kNow), 1U);
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Body, kNow), 1U);
}

TEST(VoicesTest, WhileEveryVoicePlaysTheOneDoneSoonestIsTaken) {
    // As the original's few channels did, but with every voice of the mixer to choose from.
    std::vector<VoiceUse> voices;
    for (std::uint64_t voice = 0; voice < 64; ++voice) {
        voices.push_back({.channel = SoundChannel::Auto, .doneAt = MixFrames{10'000 - voice}});
    }
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Auto, kNow), 63U);
}

TEST(VoicesTest, ASoundReplacesOnlyItsOwnMakersChannel) {
    const std::array voices{
        VoiceUse{.maker = kOther, .channel = SoundChannel::Weapon, .doneAt = MixFrames{1'900}},
        VoiceUse{.channel = SoundChannel::Weapon, .doneAt = MixFrames{1'800}},
        VoiceUse{.maker = kAnother, .channel = SoundChannel::Weapon, .doneAt = MixFrames{1'700}},
        VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{200}}};
    EXPECT_EQ(voiceFor(voices, kOther, SoundChannel::Weapon, kNow), 0U);
    EXPECT_EQ(voiceFor(voices, kAnother, SoundChannel::Weapon, kNow), 2U);
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Weapon, kNow), 1U);
    // A player whose weapon nothing plays yet takes the voice with the least left, not another's channel.
    EXPECT_EQ(voiceFor(voices, PlayerIndex{7}, SoundChannel::Weapon, kNow), 3U);
}

TEST(VoicesTest, AnothersSoundNeverCutsThePlayersOwnShort) {
    const std::array voices{
        VoiceUse{.channel = SoundChannel::Weapon, .doneAt = MixFrames{1'100}},
        VoiceUse{.maker = kOther, .channel = SoundChannel::Body, .doneAt = MixFrames{1'900}},
        VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{500}}};
    // The player's weapon plays on: another's sound takes the voice of the player's own that is done.
    EXPECT_EQ(voiceFor(voices, kAnother, SoundChannel::Auto, kNow), 2U);
    const std::array playing{
        VoiceUse{.channel = SoundChannel::Weapon, .doneAt = MixFrames{1'100}},
        VoiceUse{.maker = kOther, .channel = SoundChannel::Body, .doneAt = MixFrames{1'900}}};
    EXPECT_EQ(voiceFor(playing, kAnother, SoundChannel::Auto, kNow), 1U)
        << "the other's, though it has more left";
    // The player's own sound takes the one done soonest, another's or not.
    EXPECT_EQ(voiceFor(playing, std::nullopt, SoundChannel::Auto, kNow), 0U);
}

TEST(VoicesTest, AnothersSoundFindsNoVoiceWhileEveryOneHoldsThePlayersOwn) {
    const std::array voices{VoiceUse{.channel = SoundChannel::Weapon, .doneAt = MixFrames{1'100}},
                            VoiceUse{.channel = SoundChannel::Auto, .doneAt = MixFrames{1'300}}};
    EXPECT_FALSE(voiceFor(voices, kOther, SoundChannel::Auto, kNow).has_value());
    EXPECT_EQ(voiceFor(voices, std::nullopt, SoundChannel::Voice, kNow), 0U);
}

} // namespace
