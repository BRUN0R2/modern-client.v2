// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the player hears (docs/11-audio.md): the system's sound output playing the mixer, the
/// game's sounds read by their names, the sounds the player makes on its channels, and those the
/// client plays itself, as the original's `play` and `playvol` do. Every sound is heard as the
/// player's own, at full level in both ears, until spatialization comes (TODO.md).
export module cs.client:audio;

import std;
import nx.core;
import nx.assets;
import nx.platform;
import nx.console;
import nx.audio;
import cs.shared;

export namespace cs {

/// What can go wrong playing a sound.
enum class PlayError : std::uint8_t {
    NoSound,   ///< The sound could not be read (cause: the SoundError).
    QueueFull, ///< The mixer has not taken the commands sent yet (cause: the MixerError).
};

/// The sound output and what plays on it. It stays where it is made: the output's thread reads its
/// mixer.
class ClientAudio {
public:
    /// Voices of the mixer the sounds on SoundChannel::Auto take in turn, the client's own among them:
    /// a sound played while all of them play takes the one that started first. The player's other
    /// channels have a voice each, after them.
    static constexpr std::uint32_t kLocalVoices = 8;

    /// Opens the system's default output and plays the mixer on it, everything heard at `volume`'s
    /// gain; sounds are read from `vfs`, which must outlive it, as `volume`'s console must. Errors:
    /// those of AudioDevice::open and AudioDevice::play.
    [[nodiscard]] static auto open(const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume)
        -> nx::Result<std::unique_ptr<ClientAudio>, nx::platform::AudioDeviceError>;

    ClientAudio(const ClientAudio&) = delete;
    auto operator=(const ClientAudio&) -> ClientAudio& = delete;
    ClientAudio(ClientAudio&&) = delete;
    auto operator=(ClientAudio&&) -> ClientAudio& = delete;
    ~ClientAudio() = default;

    /// Plays the sound `name` names (nx::audio::SoundLibrary::load) at `volume`, from 0 to 1 as it is,
    /// in both ears, as the original's `play` and `playvol` play a sound heard everywhere, `delay` from
    /// now. Errors: NoSound, QueueFull.
    [[nodiscard]] auto playLocal(std::string_view name, float volume, nx::Seconds delay = nx::Seconds{})
        -> nx::Result<void, PlayError>;

    /// Plays `sound` as the player makes it, `delay` from now: one of its files and a pitch of its
    /// range at random, as the original's client and engine choose them, on its channel of the player,
    /// replacing what that channel plays as it starts. Errors: NoSound, QueueFull.
    [[nodiscard]] auto play(const GameSound& sound, nx::Seconds delay = nx::Seconds{})
        -> nx::Result<void, PlayError>;

    /// Reads the sound `name` names (nx::audio::SoundLibrary::load) without playing it, so that
    /// playing it reads no file: a file read as a frame plays holds that frame up as long as the disk,
    /// and whatever scans the files opened, take, longer than a frame lasts when it is not in the
    /// system's cache. Errors: NoSound.
    [[nodiscard]] auto preload(std::string_view name) -> nx::Result<void, PlayError>;

    /// Sends the mixer `volume`'s value when it changed since it was last sent: before each sound
    /// played, so that none is heard at another gain, and each frame, so that a change is heard
    /// while nothing new plays. A value the mixer could not take is sent again the next time.
    void update();

    /// The rate the output plays at.
    [[nodiscard]] inline auto rate() const noexcept -> nx::SampleRate { return m_device.rate(); }

private:
    ClientAudio(const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume,
                nx::platform::AudioDevice device);

    /// How a sound plays: at `volume` and `pitch` (1 as recorded), `delay` from now.
    struct Playing {
        float volume = 1.0F;
        float pitch = 1.0F;
        nx::Seconds delay{};
    };

    /// Plays the sound `name` names in `voice` as `playing` says.
    [[nodiscard]] auto playIn(std::uint32_t voice, std::string_view name, const Playing& playing)
        -> nx::Result<void, PlayError>;
    /// Plays the sound `name` names in the next of the kLocalVoices.
    [[nodiscard]] auto playAuto(std::string_view name, const Playing& playing) -> nx::Result<void, PlayError>;

    nx::console::Cvar<float> m_volumeCvar;
    nx::audio::SoundLibrary m_sounds;
    nx::audio::Mixer m_mixer;
    nx::platform::AudioDevice m_device; ///< Last: it stops reading the mixer before the mixer goes.
    std::uint32_t m_nextLocalVoice = 0;
    std::optional<float> m_volume; ///< The gain the mixer was last sent.
    nx::Random m_random;           ///< What chooses the files and the pitches.
};

} // namespace cs
