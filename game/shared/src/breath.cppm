// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a player breathes in water (docs/11-audio.md, section 3): data/cstrike/breath.toml, with
/// ReGameDLL_CS's sounds and volumes (CBasePlayer::WaterMove; GPL, consulted: docs/20 section 3): the
/// air a player holds under water, the gasp it gives as it comes up after a while under, and the
/// bubbles it lets go, the likelier the less air it has left. Drowning, and the harm of slime and
/// lava, come with a player's damage.
export module cs.shared:breath;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.game;
import :sounds;
import :movement;

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

/// A player's breath, as the sounds of `rules`, which must outlive it, play it: the air it holds, kept
/// between the commands that move it as the original's air_finished keeps it.
class Breath {
public:
    explicit Breath(const BreathRules& rules) noexcept;

    /// Its air whole at `time`, as the player spawns.
    void fill(const nx::game::TickTime& time) noexcept;
    /// It breathes at `time`, before a command moves it, in water to `level`, as the last command left
    /// it, while it moves as `moveType` (CBasePlayer::WaterMove, in PreThink): with its head out of
    /// water its air is whole again, and coming up with less than kGaspingAir left of it, it gasps;
    /// with its head under, it bubbles one breath in 32, then as likely as a whole second from 0 to 11,
    /// drawn from `random`, is the whole seconds of its air left or more. A noclipping player holds
    /// its breath as it was. The sound it makes, when it makes one.
    [[nodiscard]] auto breathe(WaterLevel level, MoveType moveType, const nx::game::TickTime& time,
                               nx::Random& random) -> std::optional<SoundMade>;

private:
    const BreathRules* m_rules;
    nx::GameTime m_airRunsOut{}; ///< When its air runs out under water (air_finished).
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
