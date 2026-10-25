// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#include <SDL3/SDL.h>

module nx.platform;

import std;
import nx.core;

namespace nx::platform {

namespace {

/// Frames the source renders at most at a time: what the device asks for more is rendered in turns.
constexpr std::size_t kBlockFrames = 1024;
constexpr std::size_t kFrameBytes = AudioDevice::kChannels * sizeof(float);
/// The rate asked for when the default output does not tell its own.
constexpr int kFallbackRate = 48'000;

} // namespace

struct AudioFeed {
    SDL_AudioStream* stream = nullptr;
    /// Set and cleared with the stream locked: SDL calls pull() with it locked.
    AudioSource* source = nullptr;
    std::array<float, kBlockFrames * AudioDevice::kChannels> block{};
    std::atomic<std::uint64_t> lostBlocks{0};
};

namespace {

/// SDL's callback, on the device's thread: renders the bytes the device asks for, in whole frames.
void SDLCALL pull(void* userdata, SDL_AudioStream* stream, int additionalAmount, int /*totalAmount*/) {
    auto& feed = *static_cast<AudioFeed*>(userdata);
    std::size_t framesLeft =
        (static_cast<std::size_t>(std::max(additionalAmount, 0)) + kFrameBytes - 1) / kFrameBytes;
    while (framesLeft > 0) {
        const std::size_t frames = std::min(framesLeft, kBlockFrames);
        const std::span<float> block = std::span{feed.block}.first(frames * AudioDevice::kChannels);
        if (feed.source != nullptr) {
            feed.source->render(block);
        } else {
            std::ranges::fill(block, 0.0F);
        }
        if (!SDL_PutAudioStreamData(stream, block.data(), static_cast<int>(block.size_bytes()))) {
            feed.lostBlocks.fetch_add(1, std::memory_order_relaxed);
        }
        framesLeft -= frames;
    }
}

/// The system's playback outputs, the audio subsystem started.
[[nodiscard]] auto playbackOutputs() -> std::vector<SDL_AudioDeviceID> {
    int count = 0;
    SDL_AudioDeviceID* const outputs = SDL_GetAudioPlaybackDevices(&count);
    if (outputs == nullptr) {
        return {};
    }
    std::vector<SDL_AudioDeviceID> listed(outputs, outputs + std::max(count, 0));
    SDL_free(outputs);
    return listed;
}

/// The name of `output`; none when the system tells none.
[[nodiscard]] auto nameOf(SDL_AudioDeviceID output) -> std::optional<std::string_view> {
    const char* const name = SDL_GetAudioDeviceName(output);
    return name != nullptr ? std::optional<std::string_view>{name} : std::nullopt;
}

} // namespace

auto audioOutputNames() -> Result<std::vector<std::string>, AudioDeviceError> {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return fail(AudioDeviceError::Unavailable,
                    std::format("SDL_InitSubSystem(SDL_INIT_AUDIO): {}", SDL_GetError()));
    }
    std::vector<std::string> names;
    for (const SDL_AudioDeviceID output : playbackOutputs()) {
        if (const std::optional<std::string_view> name = nameOf(output)) {
            names.emplace_back(*name);
        }
    }
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return names;
}

auto AudioDevice::open(std::optional<std::string_view> name) -> Result<AudioDevice, AudioDeviceError> {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        return fail(AudioDeviceError::Unavailable,
                    std::format("SDL_InitSubSystem(SDL_INIT_AUDIO): {}", SDL_GetError()));
    }
    SDL_AudioDeviceID output = SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK;
    if (name) {
        const std::vector<SDL_AudioDeviceID> outputs = playbackOutputs();
        const auto found = std::ranges::find_if(
            outputs, [name](SDL_AudioDeviceID candidate) { return nameOf(candidate) == *name; });
        if (found == outputs.end()) {
            SDL_QuitSubSystem(SDL_INIT_AUDIO);
            return fail(AudioDeviceError::UnknownOutput, std::string{*name});
        }
        output = *found;
    }
    SDL_AudioSpec device{};
    int deviceFrames = 0;
    if (!SDL_GetAudioDeviceFormat(output, &device, &deviceFrames)) {
        std::string error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return fail(AudioDeviceError::NoDevice, std::format("SDL_GetAudioDeviceFormat: {}", error));
    }
    // The device's own rate, so that SDL converts nothing on the way.
    const SDL_AudioSpec spec{.format = SDL_AUDIO_F32,
                             .channels = static_cast<int>(kChannels),
                             .freq = device.freq > 0 ? device.freq : kFallbackRate};
    auto feed = std::make_unique<AudioFeed>();
    feed->stream = SDL_OpenAudioDeviceStream(output, &spec, pull, feed.get());
    if (feed->stream == nullptr) {
        std::string error = SDL_GetError();
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        return fail(AudioDeviceError::NoDevice, std::format("SDL_OpenAudioDeviceStream: {}", error));
    }
    return AudioDevice{std::move(feed), SampleRate{narrow<std::uint32_t>(spec.freq)}};
}

AudioDevice::AudioDevice(std::unique_ptr<AudioFeed> feed, SampleRate rate) noexcept
    : m_feed{std::move(feed)}, m_rate{rate} {}

AudioDevice::AudioDevice(AudioDevice&& other) noexcept
    : m_feed{std::move(other.m_feed)}, m_rate{other.m_rate} {}

auto AudioDevice::operator=(AudioDevice&& other) noexcept -> AudioDevice& {
    if (this != &other) {
        close();
        m_feed = std::move(other.m_feed);
        m_rate = other.m_rate;
    }
    return *this;
}

AudioDevice::~AudioDevice() {
    close();
}

void AudioDevice::close() noexcept {
    if (m_feed == nullptr) {
        return;
    }
    // Closes the device the stream was opened with; SDL calls pull() no more once it returns.
    SDL_DestroyAudioStream(m_feed->stream);
    m_feed.reset();
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
}

auto AudioDevice::play(AudioSource& source) -> Result<void, AudioDeviceError> {
    expects(m_feed != nullptr, "AudioDevice::play: an open device");
    if (!SDL_LockAudioStream(m_feed->stream)) {
        return fail(AudioDeviceError::StartFailed, std::format("SDL_LockAudioStream: {}", SDL_GetError()));
    }
    m_feed->source = &source;
    SDL_UnlockAudioStream(m_feed->stream);
    if (!SDL_ResumeAudioStreamDevice(m_feed->stream)) {
        return fail(AudioDeviceError::StartFailed,
                    std::format("SDL_ResumeAudioStreamDevice: {}", SDL_GetError()));
    }
    return {};
}

auto AudioDevice::pause() -> Result<void, AudioDeviceError> {
    expects(m_feed != nullptr, "AudioDevice::pause: an open device");
    if (!SDL_PauseAudioStreamDevice(m_feed->stream)) {
        return fail(AudioDeviceError::StartFailed,
                    std::format("SDL_PauseAudioStreamDevice: {}", SDL_GetError()));
    }
    // A pull already under way ends before the lock is taken.
    if (!SDL_LockAudioStream(m_feed->stream)) {
        return fail(AudioDeviceError::StartFailed, std::format("SDL_LockAudioStream: {}", SDL_GetError()));
    }
    m_feed->source = nullptr;
    SDL_UnlockAudioStream(m_feed->stream);
    return {};
}

auto AudioDevice::lostBlocks() const noexcept -> std::uint64_t {
    return m_feed != nullptr ? m_feed->lostBlocks.load(std::memory_order_relaxed) : 0;
}

} // namespace nx::platform
