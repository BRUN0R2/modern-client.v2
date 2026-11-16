// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.platform;
import nx.console;
import nx.audio;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers that chooses which file of a sound plays, and at what pitch.
constexpr std::string_view kSoundStream = "sounds";

/// How fast the mixer plays a sound the game gives `pitch`.
[[nodiscard]] auto mixerPitchOf(SoundPitch pitch) noexcept -> nx::audio::Pitch {
    return nx::audio::kRecordedPitch *
           (static_cast<float>(pitch.value()) / static_cast<float>(kNormalPitch.value()));
}

/// Whether an ear hears a sound at `gain` any differently from at `before`: by kEarVolumeStep or more.
[[nodiscard]] auto isHeardDifferently(nx::Gain gain, nx::Gain before) noexcept -> bool {
    return std::abs((gain - before).value()) >= kEarVolumeStep.value();
}

/// The same gain in both ears: `volume`, at most nx::kFullGain, as the original bounds a sound's volume.
[[nodiscard]] auto bothEarsAt(nx::Gain volume) noexcept -> nx::audio::VoiceGains {
    const nx::Gain gain = std::clamp(volume, nx::Gain{}, nx::kFullGain);
    return {.left = gain, .right = gain};
}

} // namespace

auto followSound(PlacedSound& sound, std::optional<nx::Vec3> maker, const nx::audio::Listener& listener)
    -> std::optional<nx::audio::VoiceGains> {
    if (maker) {
        sound.position = *maker;
    }
    const nx::audio::VoiceGains gains =
        nx::audio::legacyGains(listener, sound.position, sound.volume, sound.attenuation);
    if (!isHeardDifferently(gains.left, sound.gains.left) &&
        !isHeardDifferently(gains.right, sound.gains.right)) {
        return std::nullopt;
    }
    sound.gains = gains;
    return gains;
}

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
      m_device{std::move(device)}, voiceUses(m_mixer.voices()), followedPlays(m_mixer.voices()),
      m_random{0, kSoundStream} {}

auto ClientAudio::playLocal(std::string_view name, nx::Gain volume, nx::Seconds delay)
    -> nx::Result<void, PlayError> {
    return playAs(name, Playing{.gains = bothEarsAt(volume), .delay = delay});
}

auto ClientAudio::play(const GameSound& sound, nx::Gain volume, nx::Seconds delay)
    -> nx::Result<void, PlayError> {
    const Take take = takeOf(sound);
    return playAs(
        take.file,
        Playing{.channel = sound.channel, .gains = bothEarsAt(volume), .pitch = take.pitch, .delay = delay});
}

auto ClientAudio::playAt(const PlayerSound& heard, const nx::audio::Listener& listener, nx::Seconds delay)
    -> nx::Result<void, PlayError> {
    const GameSound& sound = *heard.made.sound;
    PlacedSound placed{.position = heard.position,
                       .volume = std::clamp(heard.made.volume, nx::Gain{}, nx::kFullGain),
                       .attenuation = attenuationOf(sound)};
    placed.gains = nx::audio::legacyGains(listener, placed.position, placed.volume, placed.attenuation);
    if (placed.gains.left == nx::Gain{} && placed.gains.right == nx::Gain{}) {
        return {};
    }
    const Take take = takeOf(sound);
    return playAs(take.file, Playing{.maker = heard.player,
                                     .channel = sound.channel,
                                     .gains = placed.gains,
                                     .pitch = take.pitch,
                                     .delay = delay,
                                     .placed = placed});
}

void ClientAudio::follow(const nx::audio::Listener& listener,
                         std::span<const std::optional<nx::Vec3>> makers) {
    const nx::audio::MixFrames now = m_mixer.framesMixed();
    for (std::size_t voice = 0; voice < followedPlays.size(); ++voice) {
        std::optional<FollowedPlay>& play = followedPlays[voice];
        const VoiceUse& use = voiceUses[voice];
        if (!play || !use.maker) {
            continue;
        }
        if (use.doneAt <= now) {
            play.reset();
            continue;
        }
        const std::size_t maker = use.maker->value;
        PlacedSound followed = play->sound;
        const std::optional<nx::audio::VoiceGains> gains =
            followSound(followed, maker < makers.size() ? makers[maker] : std::nullopt, listener);
        if (!gains) {
            play->sound.position = followed.position;
            continue;
        }
        // Held only once the mixer takes it, so that one it could not is sent again the next time.
        if (m_mixer.send(
                nx::audio::SetVoiceGains{.voice = nx::audio::VoiceId{nx::narrow<std::uint32_t>(voice)},
                                         .tag = play->tag,
                                         .gains = *gains})) {
            play->sound = followed;
        }
    }
}

auto ClientAudio::takeOf(const GameSound& sound) -> Take {
    const std::string& file = sound.files[nx::narrow<std::size_t>(m_random.below(sound.files.size()))];
    const SoundPitch pitch =
        sound.pitch
            ? sound.pitch->least + SoundPitch{nx::narrow<std::uint32_t>(m_random.below(
                                       std::uint64_t{(sound.pitch->most - sound.pitch->least).value()} + 1))}
            : kNormalPitch;
    return Take{.file = file, .pitch = mixerPitchOf(pitch)};
}

auto ClientAudio::preload(std::string_view name) -> nx::Result<void, PlayError> {
    if (auto sound = m_sounds.load(name); !sound) {
        return nx::fail(PlayError::NoSound, std::move(sound.error()));
    }
    return {};
}

auto ClientAudio::playAs(std::string_view name, const Playing& playing) -> nx::Result<void, PlayError> {
    auto sound = m_sounds.load(name);
    if (!sound) {
        return nx::fail(PlayError::NoSound, std::move(sound.error()));
    }
    update();
    // Read before it is sent, so that the mix cannot have started it yet: it is done by then at the
    // soonest, a block of the output later at the latest.
    const nx::audio::MixFrames now = m_mixer.framesMixed();
    const std::optional<std::size_t> voice = voiceFor(voiceUses, playing.maker, playing.channel, now);
    if (!voice) {
        return nx::fail(PlayError::NoVoice, std::format("{} voices play the player's own", voiceUses.size()));
    }
    const nx::audio::PlayTag tag{lastTag.value + 1};
    const nx::audio::PlayVoice play{.voice = nx::audio::VoiceId{nx::narrow<std::uint32_t>(*voice)},
                                    .sound = *sound,
                                    .gains = playing.gains,
                                    .pitch = playing.pitch,
                                    .tag = tag};
    if (auto sent = m_mixer.send(play, playing.delay); !sent) {
        return nx::fail(PlayError::QueueFull, std::move(sent.error()));
    }
    lastTag = tag;
    followedPlays[*voice] =
        playing.placed ? std::optional{FollowedPlay{.sound = *playing.placed, .tag = tag}} : std::nullopt;
    voiceUses[*voice] =
        VoiceUse{.maker = playing.maker,
                 .channel = playing.channel,
                 .doneAt = now + m_mixer.framesIn(playing.delay) + m_mixer.framesOf(**sound, playing.pitch)};
    return {};
}

void ClientAudio::update() {
    const nx::Gain volume = m_muting == Muting::On ? nx::Gain{} : nx::Gain{m_volumeCvar.value()};
    if (sentGain == volume) {
        return;
    }
    if (m_mixer.send(nx::audio::SetMasterGain{.gain = volume})) {
        sentGain = volume;
    }
}

} // namespace cs
