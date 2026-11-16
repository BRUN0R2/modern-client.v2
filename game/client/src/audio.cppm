// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the player hears (docs/11-audio.md): the system's sound output playing the mixer, the
/// game's sounds read by their names, the sounds the player makes on its channels, and those the
/// client plays itself, as the original's `play` and `playvol` do, at full level in both ears; and
/// the sounds the other players make, heard where their makers are as the original's stereo
/// spatialization places them, followed as they and the listener move. Each takes the voice the
/// original's engine would choose (voiceFor).
export module cs.client:audio;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.platform;
import nx.console;
import nx.audio;
import cs.shared;
import :voices;

export namespace cs {

/// Whether everything heard is silenced, the sounds playing on in silence.
enum class Muting : std::uint8_t {
    Off,
    On,
};

/// What can go wrong playing a sound.
enum class PlayError : std::uint8_t {
    NoSound,   ///< The sound could not be read (cause: the SoundError).
    QueueFull, ///< The mixer has not taken the commands sent yet (cause: the MixerError).
    NoVoice,   ///< Every voice plays one of the player's own sounds, which another's never cuts short.
};

/// The step of a sound's volume in an ear the original tells apart: its ears' volumes are whole numbers
/// of 255.
inline constexpr nx::Gain kEarVolumeStep = nx::kFullGain / 255.0F;

/// A sound another player made as it plays: where it is heard from, its volume and how fast it fades
/// with distance, and how loud it was last heard in each ear.
struct PlacedSound {
    nx::Vec3 position{};
    nx::Gain volume{nx::kFullGain};
    nx::Attenuation attenuation{kNormalAttenuation};
    nx::audio::VoiceGains gains{};
};

/// Follows `sound` to where its maker is now, `maker` (none once it is gone, as the original's client
/// keeps a sound where its entity was last told of), as `listener` hears it there
/// (nx::audio::legacyGains): the gains it is heard at now, which it holds from then on, when either ear
/// changed by kEarVolumeStep or more since those it held; none otherwise, nothing heard any differently.
[[nodiscard]] auto followSound(PlacedSound& sound, std::optional<nx::Vec3> maker,
                               const nx::audio::Listener& listener) -> std::optional<nx::audio::VoiceGains>;

/// The sound output and what plays on it. It stays where it is made: the output's thread reads its
/// mixer.
class ClientAudio {
    struct CreateToken {
        explicit CreateToken() = default;
    };

public:
    /// Opens the output named `output`, or the system's default one without a name, and plays the
    /// mixer on it, everything heard at `volume`'s gain; sounds are read from `vfs`, which must outlive
    /// it, as `volume`'s console must. Errors: those of AudioDevice::open and AudioDevice::play.
    [[nodiscard]] static auto open(const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume,
                                   std::optional<std::string_view> output = std::nullopt)
        -> nx::Result<std::unique_ptr<ClientAudio>, nx::platform::AudioDeviceError>;

    /// Use open(). Public only so that open() can use std::make_unique.
    ClientAudio(CreateToken token, const nx::assets::Vfs& vfs, nx::console::Cvar<float> volume,
                nx::platform::AudioDevice device);

    ClientAudio(const ClientAudio&) = delete;
    auto operator=(const ClientAudio&) -> ClientAudio& = delete;
    ClientAudio(ClientAudio&&) = delete;
    auto operator=(ClientAudio&&) -> ClientAudio& = delete;
    ~ClientAudio() = default;

    /// Plays the sound `name` names (nx::audio::SoundLibrary::load) at `volume`, at most nx::kFullGain,
    /// in both ears, as the original's `play` and `playvol` play a sound heard everywhere, `delay` from
    /// now, on the player's SoundChannel::Auto. Errors: NoSound, QueueFull.
    [[nodiscard]] auto playLocal(std::string_view name, nx::Gain volume, nx::Seconds delay = nx::Seconds{})
        -> nx::Result<void, PlayError>;

    /// Plays `sound` as the player makes it, at `volume` (at most nx::kFullGain: the sound's own, or
    /// what the game made it at, cs::SoundMade), `delay` from now: one of its files and a pitch of its
    /// range at random, as the original's client and engine choose them, on its channel of the player,
    /// replacing what that channel plays as it starts. Errors: NoSound, QueueFull.
    [[nodiscard]] auto play(const GameSound& sound, nx::Gain volume, nx::Seconds delay = nx::Seconds{})
        -> nx::Result<void, PlayError>;

    /// Plays the sound another player made, `heard`, `delay` from now, as `listener` hears it where it was
    /// made: one of its files and a pitch at random as play chooses them, on its channel of its maker,
    /// replacing what that channel plays as it starts, in each ear as the original's legacy stereo
    /// spatialization makes it (nx::audio::legacyGains, by its attenuation), followed from then on
    /// (follow); not at all when neither ear hears it, as the original drops a sound out of reach as it
    /// starts. Errors: NoSound, QueueFull, NoVoice.
    [[nodiscard]] auto playAt(const PlayerSound& heard, const nx::audio::Listener& listener,
                              nx::Seconds delay = nx::Seconds{}) -> nx::Result<void, PlayError>;

    /// Each frame: the sounds the other players made that still play heard as `listener` hears them from
    /// where their makers are now, `makers`, by PlayerIndex (ClientGame::writePlayerOrigins), as the
    /// original's client places them again each frame (followSound); a maker it has none of left where it
    /// was last. A change the mixer could not take is sent again the next time.
    void follow(const nx::audio::Listener& listener, std::span<const std::optional<nx::Vec3>> makers);

    /// Reads the sound `name` names (nx::audio::SoundLibrary::load) without playing it, so that
    /// playing it reads no file: a file read as a frame plays holds that frame up as long as the disk,
    /// and whatever scans the files opened, take, longer than a frame lasts when it is not in the
    /// system's cache. Errors: NoSound.
    [[nodiscard]] auto preload(std::string_view name) -> nx::Result<void, PlayError>;

    /// Sends the mixer the gain of everything heard when it changed since it was last sent: `volume`'s
    /// value, or none while muted (setMuting); before each sound played, so that none is heard at
    /// another gain, and each frame, so that a change is heard while nothing new plays. A value the
    /// mixer could not take is sent again the next time.
    void update();

    /// Silences everything heard while `muting` is On, the sounds playing on in silence, from the next
    /// update.
    inline void setMuting(Muting muting) noexcept { m_muting = muting; }

    /// The rate the output plays at.
    [[nodiscard]] inline auto rate() const noexcept -> nx::SampleRate { return m_device.rate(); }

private:
    /// One of a sound's files, and the pitch it plays at.
    struct Take {
        std::string_view file{};
        nx::audio::Pitch pitch{nx::audio::kRecordedPitch};
    };

    /// Who makes a sound and how it plays: its maker, none for the player itself, and the channel of
    /// its maker it plays on; how loud in each ear, at what pitch, `delay` from now; and, another's,
    /// where it is heard from, none for one heard as the player's own.
    struct Playing {
        std::optional<PlayerIndex> maker{};
        SoundChannel channel = SoundChannel::Auto;
        nx::audio::VoiceGains gains{};
        nx::audio::Pitch pitch{nx::audio::kRecordedPitch};
        nx::Seconds delay{};
        std::optional<PlacedSound> placed{};
    };

    /// A play of a voice that follows its maker: where it is heard from, and what the play is known by.
    struct FollowedPlay {
        PlacedSound sound{};
        nx::audio::PlayTag tag{};
    };

    /// One of `sound`'s files and a pitch of its range, at random, as the original's client and engine
    /// choose them.
    [[nodiscard]] auto takeOf(const GameSound& sound) -> Take;
    /// Plays the sound `name` names as `playing` says, in the voice voiceFor chooses.
    [[nodiscard]] auto playAs(std::string_view name, const Playing& playing) -> nx::Result<void, PlayError>;

    nx::console::Cvar<float> m_volumeCvar;
    nx::audio::SoundLibrary m_sounds;
    nx::audio::Mixer m_mixer;
    nx::platform::AudioDevice m_device; ///< Last: it stops reading the mixer before the mixer goes.
    std::vector<VoiceUse> voiceUses;    ///< What each voice of the mixer was last given, by its VoiceId.
    /// What each voice plays that follows its maker, by its VoiceId; none for any other.
    std::vector<std::optional<FollowedPlay>> followedPlays;
    nx::audio::PlayTag lastTag{};     ///< Of the last play: each one's is the next.
    std::optional<nx::Gain> sentGain; ///< The gain the mixer was last sent.
    Muting m_muting = Muting::Off;
    nx::Random m_random; ///< What chooses the files and the pitches.
};

} // namespace cs
