// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Sounds made of WAV files: every layout made floats at the same scale, two channels kept of more,
// and where a sound loops from.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.assets;
import nx.audio;

namespace {

using nx::assets::ChannelCount;
using nx::assets::SampleFormat;
using nx::assets::SampleFrames;
using nx::assets::SampleRate;
using nx::assets::WavSound;
using nx::audio::Sound;

constexpr float kExact = 1e-7F;

/// The bytes of `values`, each `width` bytes little-endian.
[[nodiscard]] auto littleEndian(std::initializer_list<std::uint64_t> values, std::size_t width)
    -> std::vector<std::byte> {
    std::vector<std::byte> bytes;
    for (const std::uint64_t value : values) {
        for (std::size_t index = 0; index < width; ++index) {
            bytes.push_back(static_cast<std::byte>((value >> (index * 8)) & 0xFFU));
        }
    }
    return bytes;
}

[[nodiscard]] auto wavOf(SampleFormat format, std::span<const std::byte> bytes, std::uint16_t channels,
                         std::uint32_t frames) -> WavSound {
    return WavSound{.format = format,
                    .channels = ChannelCount{channels},
                    .rate = SampleRate{22'050},
                    .length = SampleFrames{frames},
                    .samples = bytes,
                    .cuePoints = {}};
}

void expectSamples(const Sound& sound, std::initializer_list<float> expected) {
    ASSERT_EQ(sound.samples().size(), expected.size());
    for (std::size_t index = 0; const float value : expected) {
        EXPECT_NEAR(sound.samples()[index], value, kExact) << "sample " << index;
        ++index;
    }
}

TEST(SoundTest, EveryLayoutIsMadeFloatsAtTheSameScale) {
    {
        const auto bytes = littleEndian({0, 128, 192}, 1);
        expectSamples(Sound::of(wavOf(SampleFormat::Unsigned8, bytes, 1, 3)), {-1.0F, 0.0F, 0.5F});
    }
    {
        const auto bytes = littleEndian({0x8000, 0, 0x4000}, 2);
        expectSamples(Sound::of(wavOf(SampleFormat::Signed16, bytes, 1, 3)), {-1.0F, 0.0F, 0.5F});
    }
    {
        const auto bytes = littleEndian({0x80'0000, 0xC0'0000, 0x40'0000}, 3);
        expectSamples(Sound::of(wavOf(SampleFormat::Signed24, bytes, 1, 3)), {-1.0F, -0.5F, 0.5F});
    }
    {
        const auto bytes = littleEndian({0x8000'0000, 0, 0x4000'0000}, 4);
        expectSamples(Sound::of(wavOf(SampleFormat::Signed32, bytes, 1, 3)), {-1.0F, 0.0F, 0.5F});
    }
    {
        const auto bytes =
            littleEndian({std::bit_cast<std::uint32_t>(0.25F), std::bit_cast<std::uint32_t>(-1.0F)}, 4);
        expectSamples(Sound::of(wavOf(SampleFormat::Float32, bytes, 1, 2)), {0.25F, -1.0F});
    }
    {
        const auto bytes = littleEndian({std::bit_cast<std::uint64_t>(-0.75)}, 8);
        expectSamples(Sound::of(wavOf(SampleFormat::Float64, bytes, 1, 1)), {-0.75F});
    }
}

TEST(SoundTest, ASoundKeepsItsRateAndTwoOfMoreChannels) {
    // Two frames of three channels: the third of each is left out.
    const auto bytes = littleEndian({0x4000, 0xC000, 0x7FFF, 0, 0x2000, 0x8000}, 2);
    const Sound sound = Sound::of(wavOf(SampleFormat::Signed16, bytes, 3, 2));
    EXPECT_EQ(sound.channels(), 2);
    EXPECT_EQ(sound.frames(), 2U);
    EXPECT_EQ(sound.rate(), SampleRate{22'050});
    expectSamples(sound, {0.5F, -0.5F, 0.0F, 0.25F});
}

TEST(SoundTest, ItLoopsFromItsFirstCuePoint) {
    const auto bytes = littleEndian({128, 128, 128, 128}, 1);
    WavSound wav = wavOf(SampleFormat::Unsigned8, bytes, 1, 4);
    EXPECT_EQ(Sound::of(wav).loopStart(), std::nullopt);
    wav.cuePoints = {SampleFrames{2}, SampleFrames{1}};
    EXPECT_EQ(Sound::of(wav).loopStart(), SampleFrames{2});
}

TEST(SoundTest, ALoopStartPastItsEndLoopsFromItsStart) {
    const Sound sound{{0.0F, 0.0F}, 1, SampleRate{11'025}, SampleFrames{2}};
    EXPECT_EQ(sound.frames(), 2U);
    EXPECT_EQ(sound.loopStart(), std::nullopt);
}

} // namespace
