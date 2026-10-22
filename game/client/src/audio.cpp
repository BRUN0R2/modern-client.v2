// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.assets;
import nx.platform;
import nx.console;
import nx.audio;

namespace cs {

auto ClientAudio::open(const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume)
    -> nx::Result<std::unique_ptr<ClientAudio>, nx::platform::AudioDeviceError> {
    auto device = nx::platform::AudioDevice::open();
    if (!device) {
        return std::unexpected{std::move(device.error())};
    }
    // Made here: its constructor is private.
    std::unique_ptr<ClientAudio> audio{new ClientAudio{vfs, volume, std::move(*device)}};
    audio->update();
    if (auto played = audio->m_device.play(audio->m_mixer); !played) {
        return std::unexpected{std::move(played.error())};
    }
    return audio;
}

ClientAudio::ClientAudio(const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume,
                         nx::platform::AudioDevice device)
    : m_volumeCvar{volume}, m_sounds{vfs}, m_mixer{nx::audio::MixerDesc{.rate = device.rate()}},
      m_device{std::move(device)} {}

auto ClientAudio::playLocal(std::string_view name, float volume) -> nx::Result<void, PlayError> {
    auto sound = m_sounds.load(name);
    if (!sound) {
        return nx::fail(PlayError::NoSound, std::move(sound.error()));
    }
    update();
    // A sound is never louder than it is, as the original bounds its volume.
    const float gain = std::clamp(volume, 0.0F, 1.0F);
    const nx::audio::PlayVoice play{
        .voice = m_nextLocalVoice, .sound = *sound, .gains = {.left = gain, .right = gain}};
    if (auto sent = m_mixer.send(play); !sent) {
        return nx::fail(PlayError::QueueFull, std::move(sent.error()));
    }
    m_nextLocalVoice = (m_nextLocalVoice + 1) % kLocalVoices;
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
