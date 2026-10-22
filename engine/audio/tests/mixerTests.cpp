// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The mixer: sounds read at any rate and pitch without coloring or folding them, voices that stop,
// end and loop, the gains, the limiter that keeps the mix under full scale, and the command queue.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.audio;

namespace {

using nx::assets::SampleFrames;
using nx::assets::SampleRate;
using nx::audio::Mixer;
using nx::audio::MixerDesc;
using nx::audio::MixerError;
using nx::audio::PlayVoice;
using nx::audio::SetMasterGain;
using nx::audio::SetVoiceGains;
using nx::audio::SincKernel;
using nx::audio::Sound;
using nx::audio::StopVoice;
using nx::audio::VoiceGains;

constexpr std::uint32_t kMixRate = 48'000;
/// Frames into a play past which the kernel no longer reaches before the sound's start.
constexpr std::size_t kSettled = 64;

/// A mono sound of `frames` frames at `rate`, each `sample(frame)`.
[[nodiscard]] auto soundOf(std::uint32_t rate, std::size_t frames,
                           const std::function<float(std::size_t)>& sample,
                           std::optional<SampleFrames> loopStart = std::nullopt) -> Sound {
    std::vector<float> samples(frames);
    for (std::size_t frame = 0; frame < frames; ++frame) {
        samples[frame] = sample(frame);
    }
    return Sound{std::move(samples), 1, SampleRate{rate}, loopStart};
}

[[nodiscard]] auto sineOf(std::uint32_t rate, double frequency, float amplitude) -> Sound {
    return soundOf(rate, rate, [=](std::size_t frame) {
        return amplitude *
               static_cast<float>(std::sin(2.0 * std::numbers::pi * frequency * static_cast<double>(frame) /
                                           static_cast<double>(rate)));
    });
}

/// The next `frames` frames of `mixer`, left and right interleaved.
[[nodiscard]] auto render(Mixer& mixer, std::size_t frames) -> std::vector<float> {
    std::vector<float> out(frames * 2);
    mixer.render(out);
    return out;
}

void send(Mixer& mixer, nx::audio::MixerCommand command) {
    ASSERT_TRUE(mixer.send(std::move(command)).has_value());
}

/// How far `out`'s left channel is from `expected(frame)` over frames [kSettled, its end), in dB of
/// what was expected over what is off.
[[nodiscard]] auto signalToNoise(std::span<const float> out,
                                 const std::function<double(std::size_t)>& expected) -> double {
    double wantedPower = 0.0;
    double offPower = 0.0;
    for (std::size_t frame = kSettled; frame < out.size() / 2; ++frame) {
        const double wanted = expected(frame);
        const double off = static_cast<double>(out[frame * 2]) - wanted;
        wantedPower += wanted * wanted;
        offPower += off * off;
    }
    return 10.0 * std::log10(wantedPower / offPower);
}

TEST(SincKernelTest, ItIsOneInItsMiddleAndZeroAtEveryOtherSample) {
    const SincKernel kernel;
    EXPECT_NEAR(kernel.at(0.0F), 1.0F, 1e-6F);
    for (int sample = 1; sample <= SincKernel::kZeroCrossings + 2; ++sample) {
        EXPECT_NEAR(kernel.at(static_cast<float>(sample)), 0.0F, 1e-6F) << sample;
        EXPECT_NEAR(kernel.at(static_cast<float>(-sample)), 0.0F, 1e-6F) << -sample;
    }
    EXPECT_FLOAT_EQ(kernel.at(0.37F), kernel.at(-0.37F));
    EXPECT_GT(kernel.at(0.5F), 0.6F);
    EXPECT_LT(kernel.at(1.5F), 0.0F);
}

TEST(MixerTest, ASteadyLevelComesOutAsItIsInEachEarAtItsGain) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 4}};
    const Sound level = soundOf(22'050, 22'050, [](std::size_t) { return 0.5F; });
    send(mixer, PlayVoice{.voice = 2, .sound = &level, .gains = {.left = 1.0F, .right = 0.25F}});
    EXPECT_FALSE(mixer.isPlaying(2));
    const std::vector<float> out = render(mixer, 1024);
    EXPECT_TRUE(mixer.isPlaying(2));
    for (std::size_t frame = kSettled; frame < 1024; ++frame) {
        EXPECT_NEAR(out[frame * 2], 0.5F, 1e-3F) << frame;
        EXPECT_NEAR(out[(frame * 2) + 1], 0.125F, 1e-3F) << frame;
    }
}

TEST(MixerTest, AStereoSoundKeepsItsTwoChannels) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    std::vector<float> samples;
    for (int frame = 0; frame < 4800; ++frame) {
        samples.insert(samples.end(), {0.5F, -0.25F});
    }
    const Sound stereo{std::move(samples), 2, SampleRate{kMixRate}};
    send(mixer, PlayVoice{.voice = 0, .sound = &stereo, .gains = {.left = 1.0F, .right = 2.0F}});
    const std::vector<float> out = render(mixer, 256);
    EXPECT_NEAR(out[kSettled * 2], 0.5F, 1e-3F);
    EXPECT_NEAR(out[(kSettled * 2) + 1], -0.5F, 1e-3F);
}

TEST(MixerTest, ASoundAtAnotherRateIsReadWithoutColoringIt) {
    // 1 kHz at 22 050 Hz, mixed at 48 kHz: the sine it was, at the mix's rate.
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    const Sound sine = sineOf(22'050, 1'000.0, 0.5F);
    send(mixer, PlayVoice{.voice = 0, .sound = &sine});
    const std::vector<float> out = render(mixer, 4'096);
    const double snr = signalToNoise(out, [](std::size_t frame) {
        return 0.5 * std::sin(2.0 * std::numbers::pi * 1'000.0 * static_cast<double>(frame) / kMixRate);
    });
    EXPECT_GT(snr, 70.0);
}

TEST(MixerTest, APitchOfTwoPlaysItAnOctaveHigher) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    const Sound sine = sineOf(kMixRate, 1'000.0, 0.5F);
    send(mixer, PlayVoice{.voice = 0, .sound = &sine, .pitch = 2.0F});
    const std::vector<float> out = render(mixer, 4'096);
    const double snr = signalToNoise(out, [](std::size_t frame) {
        return 0.5 * std::sin(2.0 * std::numbers::pi * 2'000.0 * static_cast<double>(frame) / kMixRate);
    });
    EXPECT_GT(snr, 70.0);
}

TEST(MixerTest, ASoundSpedUpPastTheMixsBandIsFilteredNotFoldedBack) {
    // 18 kHz played twice as fast would be 36 kHz, past the 24 kHz the mix holds: it would come
    // back as 12 kHz if it were not filtered out first.
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    const Sound sine = sineOf(kMixRate, 18'000.0, 0.5F);
    send(mixer, PlayVoice{.voice = 0, .sound = &sine, .pitch = 2.0F});
    const std::vector<float> out = render(mixer, 4'096);
    float loudest = 0.0F;
    for (std::size_t frame = kSettled; frame < 4'096; ++frame) {
        loudest = std::max(loudest, std::abs(out[frame * 2]));
    }
    EXPECT_LT(loudest, 0.5F * 1e-3F); // under -60 dB
}

TEST(MixerTest, ASoundThatDoesNotLoopEndsAndItsVoiceIsFree) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    const Sound click = soundOf(kMixRate, 100, [](std::size_t) { return 0.5F; });
    send(mixer, PlayVoice{.voice = 0, .sound = &click});
    const std::vector<float> out = render(mixer, 256);
    EXPECT_FALSE(mixer.isPlaying(0));
    EXPECT_NEAR(out[50 * 2], 0.5F, 1e-3F);
    for (std::size_t frame = 100; frame < 256; ++frame) {
        EXPECT_EQ(out[frame * 2], 0.0F) << frame;
    }
}

TEST(MixerTest, ALoopingSoundGoesBackToItsLoopStart) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    const auto ramp = [](std::size_t frame) {
        return static_cast<float>(frame) / 200.0F;
    };
    const Sound loop = soundOf(kMixRate, 100, ramp, SampleFrames{50});
    send(mixer, PlayVoice{.voice = 0, .sound = &loop, .loops = true});
    const std::vector<float> out = render(mixer, 400);
    EXPECT_TRUE(mixer.isPlaying(0));
    // At the mix's own rate the voice reads its sound sample by sample: 100 and on are 50 and on.
    for (const std::size_t frame : {std::size_t{70}, std::size_t{120}, std::size_t{199}, std::size_t{230}}) {
        const std::size_t inSound = frame < 100 ? frame : 50 + ((frame - 100) % 50);
        EXPECT_NEAR(out[frame * 2], ramp(inSound), 1e-5F) << frame;
    }
}

TEST(MixerTest, AStoppedVoiceIsSilent) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    const Sound level = soundOf(kMixRate, kMixRate, [](std::size_t) { return 0.5F; });
    send(mixer, PlayVoice{.voice = 0, .sound = &level});
    std::ignore = render(mixer, 128);
    send(mixer, StopVoice{.voice = 0});
    const std::vector<float> out = render(mixer, 128);
    EXPECT_FALSE(mixer.isPlaying(0));
    EXPECT_TRUE(std::ranges::all_of(out, [](float sample) { return sample == 0.0F; }));
}

TEST(MixerTest, GainsChangeWhileItPlays) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    const Sound level = soundOf(kMixRate, kMixRate, [](std::size_t) { return 0.5F; });
    send(mixer, PlayVoice{.voice = 0, .sound = &level});
    std::ignore = render(mixer, 128);
    send(mixer, SetVoiceGains{.voice = 0, .gains = {.left = 0.5F, .right = 0.0F}});
    send(mixer, SetMasterGain{.gain = 0.5F});
    const std::vector<float> out = render(mixer, 128);
    EXPECT_NEAR(out[0], 0.125F, 1e-3F);
    EXPECT_EQ(out[1], 0.0F);
}

TEST(MixerTest, TheLimiterKeepsTheMixUnderFullScaleAndLetsGo) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 2}};
    const Sound loud = sineOf(kMixRate, 440.0, 1.0F);
    const Sound quiet = soundOf(kMixRate, kMixRate, [](std::size_t) { return 0.25F; });
    send(mixer, PlayVoice{.voice = 0, .sound = &loud});
    send(mixer, PlayVoice{.voice = 1, .sound = &loud});
    const std::vector<float> limited = render(mixer, 4'800);
    // Within a rounding of the ceiling: the gain that brings a peak down to it is a float too.
    constexpr float kRounding = 1e-6F;
    EXPECT_TRUE(std::ranges::all_of(
        limited, [](float sample) { return std::abs(sample) <= Mixer::kCeiling * (1.0F + kRounding); }));
    EXPECT_GT(std::ranges::max(limited), 0.9F);

    send(mixer, StopVoice{.voice = 0});
    send(mixer, PlayVoice{.voice = 1, .sound = &quiet});
    const std::vector<float> released = render(mixer, kMixRate);
    EXPECT_NEAR(released.back(), 0.25F, 1e-3F); // a second later: ten of the release's time constants
}

TEST(MixerTest, CommandsWaitInTheQueueUntilItIsFull) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 1}};
    for (std::size_t command = 0; command < Mixer::kCommandCapacity; ++command) {
        ASSERT_TRUE(mixer.send(SetMasterGain{.gain = 1.0F}).has_value()) << command;
    }
    const auto refused = mixer.send(SetMasterGain{.gain = 1.0F});
    ASSERT_FALSE(refused.has_value());
    EXPECT_EQ(refused.error().code, MixerError::QueueFull);
    std::ignore = render(mixer, 16);
    EXPECT_TRUE(mixer.send(SetMasterGain{.gain = 1.0F}).has_value());
}

TEST(MixerTest, CommandsOfAVoiceItDoesNotHaveDoNothing) {
    Mixer mixer{MixerDesc{.rate = SampleRate{kMixRate}, .voices = 2}};
    const Sound level = soundOf(kMixRate, kMixRate, [](std::size_t) { return 0.5F; });
    send(mixer, PlayVoice{.voice = 2, .sound = &level});
    send(mixer, StopVoice{.voice = 7});
    send(mixer, SetVoiceGains{.voice = 9});
    send(mixer, PlayVoice{.voice = 0, .sound = &level, .pitch = 0.0F});
    const std::vector<float> out = render(mixer, 64);
    EXPECT_EQ(mixer.voices(), 2U);
    EXPECT_FALSE(mixer.isPlaying(0));
    EXPECT_FALSE(mixer.isPlaying(2));
    EXPECT_TRUE(std::ranges::all_of(out, [](float sample) { return sample == 0.0F; }));
}

} // namespace
