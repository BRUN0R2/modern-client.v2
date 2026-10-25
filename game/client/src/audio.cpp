// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.assets;
import nx.platform;
import nx.console;
import nx.audio;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers that chooses which file of a sound plays, and at what pitch.
constexpr std::string_view kSoundStream = "sounds";

} // namespace

auto ClientAudio::open(const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume)
    -> nx::Result<std::unique_ptr<ClientAudio>, nx::platform::AudioDeviceError> {
    auto device = nx::platform::AudioDevice::open();
    if (!device) {
        return std::unexpected{std::move(device.error())};
    }
    auto audio = std::make_unique<ClientAudio>(CreateToken{}, vfs, volume, std::move(*device));
    audio->update();
    if (auto played = audio->m_device.play(audio->m_mixer); !played) {
        return std::unexpected{std::move(played.error())};
    }
    return audio;
}

ClientAudio::ClientAudio(CreateToken /*token*/, const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume,
                         nx::platform::AudioDevice device)
    : m_volumeCvar{volume}, m_sounds{vfs}, m_mixer{nx::audio::MixerDesc{.rate = device.rate()}},
      m_device{std::move(device)}, m_random{0, kSoundStream} {}

auto ClientAudio::playLocal(std::string_view name, float volume, nx::Seconds delay)
    -> nx::Result<void, PlayError> {
    return playAuto(name, Playing{.volume = volume, .delay = delay});
}

auto ClientAudio::play(const GameSound& sound, float volume, nx::Seconds delay)
    -> nx::Result<void, PlayError> {
    const std::string& file = sound.files[nx::narrow<std::size_t>(m_random.below(sound.files.size()))];
    const std::uint32_t pitch =
        sound.pitch ? sound.pitch->least + nx::narrow<std::uint32_t>(m_random.below(
                                               std::uint64_t{sound.pitch->most} - sound.pitch->least + 1))
                    : kNormalPitch;
    const Playing playing{.volume = volume,
                          .pitch = static_cast<float>(pitch) / static_cast<float>(kNormalPitch),
                          .delay = delay};
    if (sound.channel == SoundChannel::Auto) {
        return playAuto(file, playing);
    }
    // The player's channels after Auto, a voice each.
    return playIn(nx::audio::VoiceId{kLocalVoices + std::uint32_t{std::to_underlying(sound.channel)} - 1U},
                  file, playing);
}

auto ClientAudio::preload(std::string_view name) -> nx::Result<void, PlayError> {
    if (auto sound = m_sounds.load(name); !sound) {
        return nx::fail(PlayError::NoSound, std::move(sound.error()));
    }
    return {};
}

auto ClientAudio::playAuto(std::string_view name, const Playing& playing) -> nx::Result<void, PlayError> {
    if (auto played = playIn(nx::audio::VoiceId{m_nextLocalVoice}, name, playing); !played) {
        return played;
    }
    m_nextLocalVoice = (m_nextLocalVoice + 1) % kLocalVoices;
    return {};
}

auto ClientAudio::playIn(nx::audio::VoiceId voice, std::string_view name, const Playing& playing)
    -> nx::Result<void, PlayError> {
    auto sound = m_sounds.load(name);
    if (!sound) {
        return nx::fail(PlayError::NoSound, std::move(sound.error()));
    }
    update();
    // A sound is never louder than it is, as the original bounds its volume.
    const float gain = std::clamp(playing.volume, 0.0F, 1.0F);
    const nx::audio::PlayVoice play{
        .voice = voice, .sound = *sound, .gains = {.left = gain, .right = gain}, .pitch = playing.pitch};
    if (auto sent = m_mixer.send(play, playing.delay); !sent) {
        return nx::fail(PlayError::QueueFull, std::move(sent.error()));
    }
    return {};
}

void ClientAudio::update() {
    const float volume = m_volumeCvar.value();
    if (m_volume == volume) {
        return;
    }
    if (m_mixer.send(nx::audio::SetMasterGain{.gain = volume})) {
        m_volume = volume;
    }
}

} // namespace cs
