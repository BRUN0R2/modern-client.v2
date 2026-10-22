// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The sound output, over SDL3 (docs/11-audio.md, section 2): the system's default device, followed
/// when it changes (headphones plugged in or out), pulling blocks from a source on its own real-time
/// thread, whatever the game's frame rate, its loading or its window's focus. SDL stays in the
/// implementation: nothing of it crosses this interface.
export module nx.platform:audio;

import std;
import nx.core;

namespace nx::platform {

/// What the device's thread reads: its source and its scratch block (audio.cpp).
struct AudioFeed;

} // namespace nx::platform

export namespace nx::platform {

/// What can go wrong with the sound output. The detail has SDL's message.
enum class AudioDeviceError : std::uint8_t {
    Unavailable, ///< The audio subsystem did not start (no driver).
    NoDevice,    ///< The system's default output could not be opened.
    StartFailed, ///< The device did not start or stop playing.
};

/// What a sound device plays: blocks of frames of two channels, left and right interleaved, in
/// 32-bit floats at the device's rate.
class AudioSource {
public:
    AudioSource() = default;
    AudioSource(const AudioSource&) = delete;
    AudioSource(AudioSource&&) = delete;
    auto operator=(const AudioSource&) -> AudioSource& = delete;
    auto operator=(AudioSource&&) -> AudioSource& = delete;
    virtual ~AudioSource() = default;

    /// Fills `out` with the next `out.size() / 2` frames. Called on the device's real-time thread,
    /// so it neither allocates, locks nor waits.
    virtual void render(std::span<float> out) noexcept = 0;
};

/// The system's default sound output, opened at its own rate in two channels of 32-bit floats.
/// When the default changes, SDL moves the output to the new one, converting if its rate differs.
class AudioDevice {
public:
    /// Channels of each frame a source renders: left and right.
    static constexpr std::size_t kChannels = 2;

    /// Opens the system's default output, paused. Errors: Unavailable, NoDevice.
    [[nodiscard]] static auto open() -> Result<AudioDevice, AudioDeviceError>;

    AudioDevice(const AudioDevice&) = delete;
    auto operator=(const AudioDevice&) -> AudioDevice& = delete;
    AudioDevice(AudioDevice&& other) noexcept;
    auto operator=(AudioDevice&& other) noexcept -> AudioDevice&;
    ~AudioDevice();

    /// Plays `source` from now on: it must outlive the play, until pause(), another play() or the
    /// device's end. Errors: StartFailed.
    [[nodiscard]] auto play(AudioSource& source) -> Result<void, AudioDeviceError>;

    /// Stops playing; the device stays open, and its source is no longer read once this returns.
    /// Errors: StartFailed.
    [[nodiscard]] auto pause() -> Result<void, AudioDeviceError>;

    /// The rate the device plays at: what its source renders at.
    [[nodiscard]] inline auto rate() const noexcept -> SampleRate { return m_rate; }

    /// Blocks rendered that the device could not take (SDL out of memory), lost as silence; from any
    /// thread.
    [[nodiscard]] auto lostBlocks() const noexcept -> std::uint64_t;

private:
    AudioDevice(std::unique_ptr<AudioFeed> feed, SampleRate rate) noexcept;
    void close() noexcept;

    std::unique_ptr<AudioFeed> m_feed; ///< On the heap: the device's thread holds its address.
    SampleRate m_rate;
};

} // namespace nx::platform
