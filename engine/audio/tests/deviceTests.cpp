// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The mixer played by a sound device, on SDL's dummy output (SDL_AUDIO_DRIVER=dummy, set by
// CMakeLists.txt): its thread pulls the mixer at a real device's pace and plays it nowhere.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;
import nx.audio;

namespace {

using nx::SampleRate;
using nx::audio::Mixer;
using nx::audio::MixerDesc;
using nx::audio::PlayVoice;
using nx::audio::Sound;
using nx::audio::StopVoice;
using nx::platform::AudioDevice;

/// Far longer than the dummy output needs, so that only a device that stopped pulling fails.
constexpr std::chrono::seconds kPatience{10};
constexpr std::chrono::milliseconds kPollInterval{1};

/// Whether `isDone` holds within kPatience.
[[nodiscard]] auto waitUntil(const std::function<bool()>& isDone) -> bool {
    const auto deadline = std::chrono::steady_clock::now() + kPatience;
    while (!isDone()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return true;
}

TEST(MixerOnADeviceTest, TheDevicesThreadTakesTheMixersCommandsAndMixes) {
    auto device = AudioDevice::open();
    ASSERT_TRUE(device.has_value()) << nx::describe(device.error());
    Mixer mixer{MixerDesc{.rate = device->rate(), .voices = 4}};
    // A tenth of a second at another rate than the device's, looping until it is stopped.
    const Sound hum{std::vector<float>(2'205, 0.25F), 1, SampleRate{22'050}};
    ASSERT_TRUE(mixer.send(PlayVoice{.voice = 3, .sound = &hum, .loops = true}).has_value());
    ASSERT_TRUE(device->play(mixer).has_value());
    EXPECT_TRUE(waitUntil([&mixer] { return mixer.isPlaying(3); }));

    ASSERT_TRUE(mixer.send(StopVoice{.voice = 3}).has_value());
    EXPECT_TRUE(waitUntil([&mixer] { return !mixer.isPlaying(3); }));
    ASSERT_TRUE(device->pause().has_value());
    EXPECT_EQ(device->lostBlocks(), 0U);
}

} // namespace
