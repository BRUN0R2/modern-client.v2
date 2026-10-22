// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// These run on SDL's dummy audio output (SDL_AUDIO_DRIVER=dummy, set by CMakeLists.txt): a device
// thread that takes blocks at a real device's pace and plays them nowhere.

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.platform;

namespace {

using nx::platform::AudioDevice;
using nx::platform::AudioSource;

/// Far longer than the dummy output needs, so that only a device that stopped pulling fails.
constexpr std::chrono::seconds kPatience{10};

/// A source of silence that counts the frames it renders and says when it has rendered `wanted`.
class CountingSource final : public AudioSource {
public:
    explicit CountingSource(std::size_t wanted) : m_wanted{wanted} {}

    void render(std::span<float> out) noexcept override {
        std::ranges::fill(out, 0.0F);
        if (out.size() % AudioDevice::kChannels != 0) {
            m_partialFrames.fetch_add(1, std::memory_order_relaxed);
        }
        const std::size_t frames = out.size() / AudioDevice::kChannels;
        if (m_frames.fetch_add(frames, std::memory_order_relaxed) + frames >= m_wanted &&
            !m_hasSignaled.exchange(true, std::memory_order_relaxed)) {
            m_enough.release();
        }
    }

    /// Whether the device rendered the frames wanted within kPatience.
    [[nodiscard]] auto waitForEnough() -> bool { return m_enough.try_acquire_for(kPatience); }
    [[nodiscard]] auto frames() const noexcept -> std::size_t {
        return m_frames.load(std::memory_order_relaxed);
    }
    [[nodiscard]] auto partialFrames() const noexcept -> std::size_t {
        return m_partialFrames.load(std::memory_order_relaxed);
    }

private:
    std::size_t m_wanted;
    std::atomic<std::size_t> m_frames{0};
    std::atomic<std::size_t> m_partialFrames{0};
    std::atomic<bool> m_hasSignaled{false};
    std::binary_semaphore m_enough{0};
};

[[nodiscard]] auto openDevice() -> std::optional<AudioDevice> {
    auto device = AudioDevice::open();
    EXPECT_TRUE(device.has_value()) << nx::describe(device.error());
    return device ? std::optional<AudioDevice>{std::move(*device)} : std::nullopt;
}

/// A tenth of a second of `device`'s frames.
[[nodiscard]] auto tenthOfASecond(const AudioDevice& device) -> std::size_t {
    return device.rate().value() / 10;
}

TEST(AudioDeviceTest, ItOpensTheDefaultOutputAndPullsItsSourceInWholeFrames) {
    auto device = openDevice();
    ASSERT_TRUE(device.has_value());
    EXPECT_GT(device->rate().value(), 0U);
    CountingSource source{tenthOfASecond(*device)};
    ASSERT_TRUE(device->play(source).has_value());
    EXPECT_TRUE(source.waitForEnough());
    EXPECT_EQ(source.partialFrames(), 0U);
    EXPECT_EQ(device->lostBlocks(), 0U);
}

TEST(AudioDeviceTest, OncePausedItReadsItsSourceNoMoreAndPlaysAnotherAfter) {
    auto device = openDevice();
    ASSERT_TRUE(device.has_value());
    CountingSource first{tenthOfASecond(*device)};
    ASSERT_TRUE(device->play(first).has_value());
    ASSERT_TRUE(first.waitForEnough());
    ASSERT_TRUE(device->pause().has_value());
    const std::size_t framesWhenPaused = first.frames();

    CountingSource second{tenthOfASecond(*device)};
    ASSERT_TRUE(device->play(second).has_value());
    EXPECT_TRUE(second.waitForEnough());
    EXPECT_EQ(first.frames(), framesWhenPaused);
}

TEST(AudioDeviceTest, AMovedDeviceKeepsPlaying) {
    auto opened = openDevice();
    ASSERT_TRUE(opened.has_value());
    CountingSource source{tenthOfASecond(*opened)};
    ASSERT_TRUE(opened->play(source).has_value());
    AudioDevice moved = std::move(*opened);
    opened.reset();
    EXPECT_TRUE(source.waitForEnough());
    EXPECT_GT(moved.rate().value(), 0U);
}

} // namespace
