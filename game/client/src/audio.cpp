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

auto ClientAudio::open(const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume,
                       std::optional<std::string_view> output)
    -> nx::Result<std::unique_ptr<ClientAudio>, nx::platform::AudioDeviceError> {
    auto device = nx::platform::AudioDevice::open(output);
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
      m_device{std::move(device)}, voiceUses(m_mixer.voices()), m_random{0, kSoundStream} {}

auto ClientAudio::playLocal(std::string_view name, float volume, nx::Seconds delay)
    -> nx::Result<void, PlayError> {
    return playOn(SoundChannel::Auto, name, Playing{.volume = volume, .delay = delay});
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
    return playOn(sound.channel, file, playing);
}

auto ClientAudio::preload(std::string_view name) -> nx::Result<void, PlayError> {
    if (auto sound = m_sounds.load(name); !sound) {
        return nx::fail(PlayError::NoSound, std::move(sound.error()));
    }
    return {};
}

auto ClientAudio::playOn(SoundChannel channel, std::string_view name, const Playing& playing)
    -> nx::Result<void, PlayError> {
    auto sound = m_sounds.load(name);
    if (!sound) {
        return nx::fail(PlayError::NoSound, std::move(sound.error()));
    }
    update();
    const std::size_t voice = voiceFor(voiceUses, channel);
    // A sound is never louder than it is, as the original bounds its volume.
    const float gain = std::clamp(playing.volume, 0.0F, 1.0F);
    const nx::audio::PlayVoice play{.voice = nx::audio::VoiceId{nx::narrow<std::uint32_t>(voice)},
                                    .sound = *sound,
                                    .gains = {.left = gain, .right = gain},
                                    .pitch = playing.pitch};
    // Read before it is sent, so that the mix cannot have started it yet: it is done by then at the
    // soonest, a block of the output later at the latest.
    const std::uint64_t now = m_mixer.framesMixed();
    if (auto sent = m_mixer.send(play, playing.delay); !sent) {
        return nx::fail(PlayError::QueueFull, std::move(sent.error()));
    }
    voiceUses[voice] =
        VoiceUse{.channel = channel,
                 .doneAt = now + m_mixer.framesIn(playing.delay) + m_mixer.framesOf(**sound, playing.pitch)};
    return {};
}

void ClientAudio::update() {
    const float volume = m_muting == Muting::On ? 0.0F : m_volumeCvar.value();
    if (m_volume == volume) {
        return;
    }
    if (m_mixer.send(nx::audio::SetMasterGain{.gain = volume})) {
        m_volume = volume;
    }
}

} // namespace cs
