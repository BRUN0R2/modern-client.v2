// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a player is hurt and killed (docs/10-counter-strike-gameplay.md, section 7.4):
/// data/cstrike/playerHarm.toml, with ReGameDLL_CS's values and sounds (CBasePlayer::TakeDamage, Pain,
/// DeathSound and PostThink's landing, CHalfLifeMultiplay::FlPlayerFallDamage; GPL, consulted: docs/20
/// section 3): the damage a hard landing deals, the whole points of a damage a player's health loses,
/// and the sounds of its pain, of a landing that kills it and of its death.
///
/// Today: the harm of a player's falls, which its armor does not take; the attacks of other players,
/// their hit groups and the armor's share come with them, and drowning and the harm of slime and lava
/// with its breath (TODO.md).
export module cs.shared:playerHarm;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.game;
import :sounds;
import :player;
import :movement;

export namespace cs {

/// Why the harm's data could not be read.
enum class PlayerHarmError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// How a hard landing hurts a player (FlPlayerFallDamage): falling faster than `safeSpeed`, by
/// `damageAtFatalSpeed` for a fall as much faster as `fatalSpeed` is, in proportion (the original's
/// DAMAGE_FOR_FALL_SPEED), times `scale`, the multiplayer rules'.
struct FallDamage {
    nx::UnitsPerSecond safeSpeed{500.0F};
    nx::UnitsPerSecond fatalSpeed{1100.0F};
    nx::Damage damageAtFatalSpeed{100.0F};
    double scale = 1.25;
};

/// data/cstrike/playerHarm.toml.
struct PlayerHarmRules {
    FallDamage fall{};
    GameSound pain{};  ///< As a harm its armor does not take hurts it (Pain, of no hit group).
    GameSound splat{}; ///< As it lands harder than its health can take.
    GameSound death{}; ///< As it dies (DeathSound).

    /// Every file its sounds play, each once, in the order the file names them.
    [[nodiscard]] auto soundFiles() const -> std::vector<std::string_view>;
};

/// Reads `text`, the file `sourceName`: a fatal speed above the safe one, and each sound with a file.
/// Errors: DataInvalid.
[[nodiscard]] auto readPlayerHarmRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<PlayerHarmRules, PlayerHarmError>;

/// The damage a fall at `fall` deals by `rules` (FlPlayerFallDamage): its speed beyond the safe one
/// times the damage of each unit a second of it, in float, then times the scale in double, rounded to
/// float as the original returns it; none or less at the safe speed or slower.
[[nodiscard]] auto fallDamageOf(const FallDamage& rules, nx::UnitsPerSecond fall) noexcept -> nx::Damage;

/// The damage the landing of a player that a command moved from `before` to `after` deals it by `rules`
/// (CBasePlayer::PostThink): in the air as the command started, its speed down then being its fall
/// (m_flFallVelocity), on the ground after it and not in water, falling faster than the safe speed:
/// fallDamageOf, when it is some; none otherwise.
[[nodiscard]] auto landingDamageOf(const FallDamage& rules, const MovementState& before,
                                   const MovementState& after) noexcept -> std::optional<nx::Damage>;

/// Deals `damage`, finite and not negative, to a player of `status` as CBasePlayer::TakeDamage deals a
/// harm its armor does not take (a fall's): the whole points of it off its health, as the original's
/// processors cut it (int()), so that its health stays whole; with none left, it is killed. A dead
/// player takes none. What it did.
[[nodiscard]] auto harmPlayer(PlayerStatus& status, nx::Damage damage) noexcept -> nx::game::Harm;

} // namespace cs

template <>
struct nx::meta::Description<cs::FallDamage> {
    static constexpr std::string_view kName = "cs::FallDamage";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::FallDamage::safeSpeed>("safeSpeed", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::FallDamage::fatalSpeed>("fatalSpeed", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::FallDamage::damageAtFatalSpeed>("damageAtFatalSpeed",
                                                             nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::FallDamage::scale>("scale", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::PlayerHarmRules> {
    static constexpr std::string_view kName = "cs::PlayerHarmRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::PlayerHarmRules::fall>("fall"),
        nx::meta::field<&cs::PlayerHarmRules::pain>("pain"),
        nx::meta::field<&cs::PlayerHarmRules::splat>("splat"),
        nx::meta::field<&cs::PlayerHarmRules::death>("death"),
    };
};
