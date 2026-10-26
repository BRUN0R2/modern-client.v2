// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a player breathes in water (docs/11-audio.md, section 3): data/cstrike/breath.toml, with
/// ReGameDLL_CS's sounds and volumes (CBasePlayer::WaterMove; GPL, consulted: docs/20 section 3): the
/// air a player holds under water, the gasp it gives as it comes up after a while under, and the
/// bubbles it lets go, the likelier the less air it has left; and, by data/cstrike/playerHarm.toml,
/// its drowning once its air ran out, and the harm of slime and lava
/// (docs/10-counter-strike-gameplay.md, section 7.4).
export module cs.shared:breath;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.game;
import nx.collision;
import :sounds;
import :movement;
import :playerHarm;

export namespace cs {

/// Why the breath's data could not be read.
enum class BreathError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// How long a player's air lasts under water (AIRTIME).
inline constexpr nx::Seconds kAirTime{12.0F};
/// A player coming up for air with less than this left of it gasps (the original's 9 seconds).
inline constexpr nx::Seconds kGaspingAir{9.0F};

/// data/cstrike/breath.toml.
struct BreathRules {
    GameSound shortGasp{}; ///< Coming up with less than kGaspingAir left of the air.
    GameSound longGasp{};  ///< Coming up once the air ran out.
    GameSound bubbles{};   ///< In water, with its head under it, the likelier the less air is left.

    /// Every file its sounds play, each once, in the order the file names them.
    [[nodiscard]] auto soundFiles() const -> std::vector<std::string_view>;
};

/// Reads `text`, the file `sourceName`: each sound with a file. Errors: DataInvalid.
[[nodiscard]] auto readBreathRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BreathRules, BreathError>;

/// What a player's breath did as it breathed (Breath::breathe), in its order: a sound it made, or a
/// harm it took.
using BreathEvent = std::variant<SoundMade, PlayerHarm>;

/// A player's breath, as the sounds of `rules` play it and the drowning and harmful water of `harm`
/// hurt it, both outliving it: the air it holds, kept between the commands that move it as the
/// original's air_finished keeps it, and how its drowning goes.
class Breath {
public:
    Breath(const BreathRules& rules, const PlayerHarmRules& harm) noexcept;

    /// As the player spawns at `time` (Spawn): its air whole, its drowning's harm back to its start,
    /// and out of water.
    void fill(const nx::game::TickTime& time) noexcept;
    /// It breathes at `time`, before a command moves it, in water of `waterType` to `level`, as the last
    /// command left it, while it moves as `moveType`, the harms it keeps over time in `timed`
    /// (CBasePlayer::WaterMove, in PreThink): with its head out of water its air is whole again, its
    /// drowning's harm back to its start and what drowning took given back (TimedHarms::headOutOfWater),
    /// and coming up with less than kGaspingAir left of it, it gasps; with its head under, nothing is
    /// given back, and once its air ran out it drowns, its drowning's interval apart, each harm a step
    /// more than the last up to the most (TimedHarms::drowned). In water it bubbles one breath in 32,
    /// then as likely as a whole second from 0 to 11, drawn from `random`, is the whole seconds of its
    /// air left or more; in slime it is hurt each command, and in lava too, but within a second after
    /// slime hurt it without its leaving the water (the original's dmgtime, which slime sets, lava reads
    /// and going into water puts back), each times how deep it is. A noclipping player holds its breath
    /// as it was. What it did, in its order.
    [[nodiscard]] auto breathe(WaterLevel level, nx::collision::Contents waterType, MoveType moveType,
                               TimedHarms& timed, const nx::game::TickTime& time, nx::Random& random)
        -> std::vector<BreathEvent>;

private:
    const BreathRules* m_rules;
    const PlayerHarmRules* m_harm;
    nx::GameTime m_airRunsOut{};       ///< When its air runs out under water (air_finished).
    nx::Damage m_drowning{};           ///< Its last drowning's harm (dmg), the next one a step more.
    nx::GameTime m_nextDrowning{};     ///< When it may drown again (pain_finished).
    bool m_isInWater = false;          ///< In water at the end of its last breath (FL_INWATER).
    nx::GameTime m_harmfulWaterTime{}; ///< Before it, lava does not hurt it (dmgtime).
};

} // namespace cs

template <>
struct nx::meta::Description<cs::BreathRules> {
    static constexpr std::string_view kName = "cs::BreathRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BreathRules::shortGasp>("shortGasp"),
        nx::meta::field<&cs::BreathRules::longGasp>("longGasp"),
        nx::meta::field<&cs::BreathRules::bubbles>("bubbles"),
    };
};
