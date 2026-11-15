// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The sounds the game makes, as data (docs/11-audio.md, section 3): which files, on which channel of
/// the entity that makes them, how loud, how fast they fade with distance and at what pitch. The game
/// says which sound it makes; who hears it chooses the file and the pitch, as the original's client and
/// engine choose them.
export module cs.shared:sounds;

import std;
import nx.core;
import nx.meta;
import nx.data;

export namespace cs {

/// The channels of an entity a sound plays on: a sound replaces the one its entity plays on its
/// channel, but on Auto, where each takes a channel of its own (the original's CHAN_AUTO,
/// CHAN_WEAPON, CHAN_VOICE, CHAN_ITEM and CHAN_BODY).
enum class SoundChannel : std::uint8_t {
    Auto,
    Weapon,
    Voice,
    Item,
    Body,
    Count,
};

/// The unit of SoundPitch: none.
struct SoundPitchTag {};

/// How fast a sound plays against its recording, in hundredths of its speed, as the original's game
/// gives a sound's pitch: kNormalPitch as recorded, twice that an octave higher in half the time.
using SoundPitch = nx::Quantity<std::uint32_t, SoundPitchTag>;

/// A sound's pitch as recorded (the original's PITCH_NORM).
inline constexpr SoundPitch kNormalPitch{100};
/// The highest pitch: the original's is a byte.
inline constexpr SoundPitch kMostPitch{255};

/// How fast most sounds fade with distance (the original's ATTN_NORM): to nothing 1250 units away
/// (docs/11-audio.md, section 3).
inline constexpr nx::Attenuation kNormalAttenuation{0.8F};
/// The most a sound may be attenuated: the original's server refuses a sound attenuated more.
inline constexpr nx::Attenuation kMostAttenuation{4.0F};

/// A pitch from `least` to `most`, at random.
struct PitchRange {
    SoundPitch least{kNormalPitch};
    SoundPitch most{kNormalPitch};
};

/// What an agent that hears a sound takes it for, which tells how far it carries (the game events
/// Condition Zero's bot hears, IsGameEventAudible): a step, a hard landing, a shot, a quiet one (a
/// knife's, a silenced weapon's), and a dry fire.
enum class NoiseKind : std::uint8_t {
    Footstep,  ///< EVENT_PLAYER_FOOTSTEP.
    Landing,   ///< EVENT_PLAYER_LANDED_FROM_HEIGHT.
    Shot,      ///< EVENT_WEAPON_FIRED, of a weapon of its normal range.
    QuietShot, ///< EVENT_WEAPON_FIRED, of a knife or a silenced weapon (its short range).
    EmptyShot, ///< EVENT_WEAPON_FIRED_ON_EMPTY.
    Count,
};

/// A sound the game makes: one of its files at random (paths under `sound/`), on a channel of the
/// entity that makes it, at `volume`, at most nx::kFullGain, fading with distance by its attenuation, as
/// most of the original's sounds do without one (attenuationOf), a pitch of its range, as recorded
/// without one, and the noise an agent that hears it takes it for, none for a sound agents do not heed.
struct GameSound {
    std::vector<std::string> files{};
    SoundChannel channel{};
    nx::Gain volume{nx::kFullGain};
    std::optional<nx::Attenuation> attenuation{};
    std::optional<PitchRange> pitch{};
    std::optional<NoiseKind> noise{};
};

/// How fast `sound` fades with distance: its attenuation, kNormalAttenuation without one; none for a
/// sound heard everywhere at its volume.
[[nodiscard]] inline auto attenuationOf(const GameSound& sound) noexcept -> nx::Attenuation {
    return sound.attenuation.value_or(kNormalAttenuation);
}

/// A sound the game made, the moment of the tick it made it at, and how loud: what lets it be heard
/// as it was made, though a frame of the client runs many ticks at once.
struct SoundMade {
    const GameSound* sound{};
    nx::GameTime at{};
    nx::Gain volume{nx::kFullGain}; ///< The sound's own, or less, as a ducked player's steps.
};

/// What is wrong with `sound`, when anything is: no file, or a pitch range whose least is above its
/// most.
[[nodiscard]] auto soundProblem(const GameSound& sound) -> std::optional<std::string_view>;

} // namespace cs

template <>
struct nx::meta::Description<cs::PitchRange> {
    static constexpr std::string_view kName = "cs::PitchRange";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::PitchRange::least>(
            "least", nx::data::Range{.minimum = 1.0, .maximum = double{cs::kMostPitch.value()}}),
        nx::meta::field<&cs::PitchRange::most>(
            "most", nx::data::Range{.minimum = 1.0, .maximum = double{cs::kMostPitch.value()}}),
    };
};

template <>
struct nx::meta::Description<cs::GameSound> {
    static constexpr std::string_view kName = "cs::GameSound";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::GameSound::files>("files"),
        nx::meta::field<&cs::GameSound::channel>("channel"),
        nx::meta::field<&cs::GameSound::volume>(
            "volume", nx::data::Range{.minimum = 0.0, .maximum = double{nx::kFullGain.value()}}),
        nx::meta::field<&cs::GameSound::attenuation>(
            "attenuation", nx::data::Range{.minimum = 0.0, .maximum = double{cs::kMostAttenuation.value()}}),
        nx::meta::field<&cs::GameSound::pitch>("pitch"),
        nx::meta::field<&cs::GameSound::noise>("noise"),
    };
};
