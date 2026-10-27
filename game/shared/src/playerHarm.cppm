// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a player is hurt and killed (docs/10-counter-strike-gameplay.md, section 7.4):
/// data/cstrike/playerHarm.toml, with ReGameDLL_CS's values and sounds (CBasePlayer::TakeDamage,
/// TakeHealth, Pain, DeathSound, PostThink's landing, WaterMove and CheckTimeBasedDamage,
/// CHalfLifeMultiplay::FlPlayerFallDamage; GPL, consulted: docs/20 section 3): the damage a hard
/// landing, drowning and harmful water deal, the whole points of a damage a player's health loses, the
/// harms it keeps over time and the drowned health they give back, and the sounds of its pain, of a
/// landing that kills it and of its death.
///
/// Today: the harm of a player's falls, of drowning, and of slime and lava; no player has armor yet,
/// so none takes a share of a harm. The attacks of other players, their hit groups and the armor's
/// share come with them (TODO.md).
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

/// The kinds of harm a player takes, as the original's damage types tell them apart.
enum class HarmKind : std::uint8_t {
    Melee, ///< An entity body's melee strike, without a timed harm.
    Fall,  ///< A hard landing (DMG_FALL).
    Drown, ///< Out of air under water (DMG_DROWN), which the player keeps over time.
    Burn,  ///< In lava (DMG_BURN).
    Acid,  ///< In slime (DMG_ACID), which the player keeps over time.
    Count,
};

/// A harm dealt to a player: how much, and of what kind.
struct PlayerHarm {
    nx::Damage damage{};
    HarmKind kind{};

    [[nodiscard]] constexpr auto operator==(const PlayerHarm&) const noexcept -> bool = default;
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

/// How drowning hurts a player whose air ran out under water, `interval` apart (WaterMove's
/// pain_finished), each harm `step` more than the last, from `start` as its air is whole, `most` at
/// most; and how much of what it took is given back each time its kept harms are looked at once it
/// breathes again (CheckTimeBasedDamage's drowning recovery).
struct DrowningDamage {
    nx::Seconds interval{1.0F};
    nx::Damage start{2.0F};
    nx::Damage step{1.0F};
    nx::Damage most{5.0F};
    nx::HitPoints givenBack{10.0F};
};

/// How harmful water hurts a player in it, each command, times how deep it is in it (WaterMove).
struct HarmfulWater {
    nx::Damage slime{4.0F};
    nx::Damage lava{10.0F};
};

/// data/cstrike/playerHarm.toml.
struct PlayerHarmRules {
    FallDamage fall{};
    DrowningDamage drowning{};
    HarmfulWater harmfulWater{};
    GameSound pain{};  ///< As a harm hurts it (Pain, without armor, of no hit group).
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

/// How often, at least, a player's kept harms are looked at (CheckTimeBasedDamage's 2 seconds).
inline constexpr nx::Seconds kKeptHarmsInterval{2.0F};

/// The harms a player keeps over time, as the original's time-based damage keeps them (its damage
/// types above DMG_ALWAYSGIB in m_bitsDamageType, m_rgbTimeBasedDamage and CheckTimeBasedDamage):
/// drowning, the drowned health given back once it breathes again, and acid, looked at
/// kKeptHarmsInterval apart at least while it keeps any.
class TimedHarms {
public:
    /// A harm of `kind` was dealt to it (TakeDamage): drowning and acid are kept, acid's looks counted
    /// anew.
    void took(HarmKind kind) noexcept;
    /// It drowned `damage` more (m_idrowndmg), its whole points to be given back.
    void drowned(nx::Damage damage) noexcept;
    /// Its head is out of water (WaterMove): while what drowning took is not all given back, it is
    /// given back, its looks counted anew, and drowning is no longer kept.
    void headOutOfWater() noexcept;
    /// Its head is under water (WaterMove): nothing is given back meanwhile; with air left
    /// (`hasAir`), drowning is no longer kept either.
    void headUnderWater(bool hasAir) noexcept;
    /// It spawns (Spawn): drowning, what it took and what was given back are forgotten; acid, and when
    /// the kept harms were last looked at, stay.
    void spawned() noexcept;
    /// Looks at the kept harms at `now`, the game running at `rate` (CheckTimeBasedDamage), when it
    /// keeps any and kKeptHarmsInterval has passed since it last did: of what drowning took and was not
    /// given back, `mostGivenBack` at most is given back, while it is kept; each harm kept with a count
    /// of looks is kept that many more looks, its count started as it is first looked at (4 for what
    /// is given back, 2 for acid). Returns the health given back.
    [[nodiscard]] auto check(nx::HitPoints mostGivenBack, nx::GameTime now, nx::TickRate rate) noexcept
        -> nx::HitPoints;

private:
    /// A harm kept, in the order the original looks at them, drowning first, which it looks at none of.
    enum class Kept : std::uint8_t {
        Drowning,
        GivingBack,
        Acid,
        Count,
    };

    nx::Flags<Kept> m_kept{};
    nx::EnumArray<Kept, std::uint8_t> m_looksLeft{}; ///< Of the kept with a count (m_rgbTimeBasedDamage).
    nx::GameTime m_lastLook{};                       ///< m_tbdPrev.
    std::int32_t m_drowned = 0;                      ///< Whole points drowning took (m_idrowndmg).
    std::int32_t m_givenBack = 0;                    ///< Whole points of them given back (m_idrownrestored).
};

/// Deals `harm`, its damage finite and not negative, to a player of `status` as CBasePlayer::TakeDamage
/// deals it to one without armor, as no player has yet (a fall's and drowning's armor never takes): the
/// whole points of it off its health, as the original's processors cut it (int()), so that its health
/// stays whole, and a harm kept over time kept (`timed`); with no health left, it is killed. A dead
/// player takes none, and keeps none. What it did.
[[nodiscard]] auto harmPlayer(PlayerStatus& status, TimedHarms& timed, const PlayerHarm& harm) noexcept
    -> nx::game::Harm;

/// Gives a player of `status` back `health`, not negative, as CBasePlayer::TakeHealth does: alive and
/// below the health it spawns with, up to it.
void healPlayer(PlayerStatus& status, nx::HitPoints health) noexcept;

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
struct nx::meta::Description<cs::DrowningDamage> {
    static constexpr std::string_view kName = "cs::DrowningDamage";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::DrowningDamage::interval>("interval", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DrowningDamage::start>("start", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DrowningDamage::step>("step", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DrowningDamage::most>("most", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DrowningDamage::givenBack>("givenBack", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::HarmfulWater> {
    static constexpr std::string_view kName = "cs::HarmfulWater";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::HarmfulWater::slime>("slime", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HarmfulWater::lava>("lava", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::PlayerHarmRules> {
    static constexpr std::string_view kName = "cs::PlayerHarmRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::PlayerHarmRules::fall>("fall"),
        nx::meta::field<&cs::PlayerHarmRules::drowning>("drowning"),
        nx::meta::field<&cs::PlayerHarmRules::harmfulWater>("harmfulWater"),
        nx::meta::field<&cs::PlayerHarmRules::pain>("pain"),
        nx::meta::field<&cs::PlayerHarmRules::splat>("splat"),
        nx::meta::field<&cs::PlayerHarmRules::death>("death"),
    };
};
