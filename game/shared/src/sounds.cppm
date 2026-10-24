// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The sounds the game makes, as data (docs/11-audio.md, section 3): which files, on which channel of
/// the entity that makes them, how loud and at what pitch. The game says which sound it makes; who
/// hears it chooses the file and the pitch, as the original's client and engine choose them.
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

/// A sound's pitch as recorded, in hundredths of its speed (the original's PITCH_NORM).
inline constexpr std::uint32_t kNormalPitch = 100;
/// The highest pitch: the original's is a byte.
inline constexpr std::uint32_t kMostPitch = 255;

/// A pitch from `least` to `most`, at random, in hundredths of the recorded speed.
struct PitchRange {
    std::uint32_t least{kNormalPitch};
    std::uint32_t most{kNormalPitch};
};

/// A sound the game makes: one of its files at random (paths under `sound/`), on a channel of the
/// entity that makes it, at `volume`, from 0 to 1, and a pitch of its range, as recorded without one.
struct GameSound {
    std::vector<std::string> files{};
    SoundChannel channel{};
    float volume{1.0F};
    std::optional<PitchRange> pitch{};
};

/// A sound the game made, and the moment of the tick it made it at: what lets it be heard as it was
/// made, though a frame of the client runs many ticks at once.
struct SoundMade {
    const GameSound* sound{};
    nx::GameTime at{};
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
            "least", nx::data::Range{.minimum = 1.0, .maximum = double{cs::kMostPitch}}),
        nx::meta::field<&cs::PitchRange::most>(
            "most", nx::data::Range{.minimum = 1.0, .maximum = double{cs::kMostPitch}}),
    };
};

template <>
struct nx::meta::Description<cs::GameSound> {
    static constexpr std::string_view kName = "cs::GameSound";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::GameSound::files>("files"),
        nx::meta::field<&cs::GameSound::channel>("channel"),
        nx::meta::field<&cs::GameSound::volume>("volume", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::GameSound::pitch>("pitch"),
    };
};
