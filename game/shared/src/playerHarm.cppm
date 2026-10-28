// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a player is hurt and killed (docs/10-counter-strike-gameplay.md, section 7.4):
/// data/cstrike/playerHarm.toml, with ReGameDLL_CS's values and sounds (CBasePlayer::TraceAttack,
/// TakeDamage, TakeHealth, Pain, DeathSound, PostThink's landing, WaterMove and CheckTimeBasedDamage,
/// CHalfLifeMultiplay::FlPlayerFallDamage; GPL, consulted: docs/20 section 3): the damage a hard
/// landing, drowning and harmful water deal, what the other players' attacks do (the share of a
/// teammate's, the knockback, the slowing down and the view's punch of a hit), the share of a harm its
/// armor takes, the whole points of a damage a player's health loses, the harms it keeps over time and
/// the drowned health they give back, and the sounds of its pain, by where it was met and its armor, of
/// a landing that kills it and of its death.
///
/// Today: the harm of a player's falls, of drowning, of slime and lava, and of the other players' shots
/// and knives; the grenades' and the bomb's come with them (TODO.md).
export module cs.shared:playerHarm;

import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;
import nx.game;
import :sounds;
import :player;
import :movement;
import :hitGroups;

export namespace cs {

/// Why the harm's data could not be read.
enum class PlayerHarmError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// The kinds of harm a player takes, as the original's damage types tell them apart.
enum class HarmKind : std::uint8_t {
    Melee,  ///< An entity body's melee strike, without a timed harm.
    Fall,   ///< A hard landing (DMG_FALL).
    Drown,  ///< Out of air under water (DMG_DROWN), which the player keeps over time.
    Burn,   ///< In lava (DMG_BURN).
    Acid,   ///< In slime (DMG_ACID), which the player keeps over time.
    Bullet, ///< Another player's weapon: a shot's round or a knife's strike (DMG_BULLET).
    Count,
};

/// A harm dealt to a player: how much, and of what kind.
struct PlayerHarm {
    nx::Damage damage{};
    HarmKind kind{};

    [[nodiscard]] constexpr auto operator==(const PlayerHarm&) const noexcept -> bool = default;
};

/// What a player's armor makes of a harm where it covers the hit group the player was last met in
/// (TakeDamage): the player's health takes `ratio` of it (flRatio: ARMOR_RATIO, times its attacker's
/// weapon's), and the armor loses `bonus` (flBonus, ARMOR_BONUS) for each point of the rest, while it
/// lasts.
struct ArmorShares {
    float ratio = 0.5F;
    float bonus = 0.5F;
};

/// What the other players' attacks do to a player (CBasePlayer::TakeDamage, TakeDamageImpulse): a
/// teammate's deals `teammateShare` of its damage; a hit that makes it flinch small slows it down to
/// `flinchSpeedShare` of its speed (m_flVelocityModifier); one that makes it flinch large, moving
/// slower than `knockbackBelowSpeed`, knocks it back `knockback` units a second away from its
/// attacker and slows it down to `largeFlinchSpeedShare`.
struct PlayerAttackRules {
    DamageShare teammateShare{0.35};
    nx::UnitsPerSecond knockback{170.0F};
    nx::UnitsPerSecond knockbackBelowSpeed{300.0F};
    float flinchSpeedShare = 0.5F;
    float largeFlinchSpeedShare = 0.65F;
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

/// The sparks a hit in the head strikes off a helmet, as the server tells its clients of them
/// (TraceAttack's TE_STREAK_SPLASH): the streak color of their color, how many, how fast they fly off
/// the helmet, and the most each is drawn faster or slower than that, as much on each axis.
struct HelmetSparks {
    std::uint8_t color = 0;
    std::uint32_t count = 0;
    nx::UnitsPerSecond speed{};
    nx::UnitsPerSecond spread{};
};

/// data/cstrike/playerHarm.toml.
struct PlayerHarmRules {
    FallDamage fall{};
    DrowningDamage drowning{};
    HarmfulWater harmfulWater{};
    ArmorShares armor{}; ///< The base of an attack's ratio, its weapon's times it.
    PlayerAttackRules attacks{};
    GameSound pain{};       ///< As a harm hurts it but in the head, its armor taking none (Pain).
    GameSound headPain{};   ///< In the head, without a helmet.
    GameSound helmetPain{}; ///< In the head, with a helmet.
    HelmetSparks helmetSparks{};
    GameSound kevlarPain{}; ///< But in the head and the legs, its armor taking a share.
    GameSound splat{};      ///< As it lands harder than its health can take.
    GameSound death{};      ///< As it dies (DeathSound).

    /// Every file its sounds play, each once, in the order the file names them.
    [[nodiscard]] auto soundFiles() const -> std::vector<std::string_view>;
};

/// Reads `text`, the file `sourceName`: a fatal speed above the safe one, each sound with a file, and a
/// helmet's sparks, some, their speeds no less than none. Errors: DataInvalid.
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

/// Whether armor of `kind` covers hit group `group` (IsArmored): a helmet the head; the vest the chest, the
/// stomach, the arms and a hit of no group; none the legs.
[[nodiscard]] auto isArmored(ArmorKind kind, HitGroup group) noexcept -> bool;

/// What a harm did to a player: the damage its health took, whether its armor took a share of it, and
/// whether it killed it.
struct PlayerHarmed {
    nx::Damage damage{};
    bool isArmored = false;
    bool isKilled = false;

    [[nodiscard]] constexpr auto operator==(const PlayerHarmed&) const noexcept -> bool = default;
};

/// Deals `harm`, its damage finite and not negative, to a player of `status` last met in hit group
/// `group` (m_LastHitGroup), as CBasePlayer::TakeDamage deals it: with armor that covers that group
/// (isArmored), but to a fall and drowning, its health takes `armor`'s ratio of it, the armor losing
/// its bonus of each point of the rest, in float; and when that is more armor than it has, it loses it
/// all, and the health takes what the armor's points, over its bonus, leave; its kind none once its
/// armor is none. Then the whole points of what is left off its health, as the original's processors
/// cut it (int()), so that its health stays whole, and a harm kept over time kept (`timed`); with no
/// health left, it is killed. A dead player takes none, and keeps none. What it did.
[[nodiscard]] auto harmPlayer(PlayerStatus& status, TimedHarms& timed, const PlayerHarm& harm, HitGroup group,
                              const ArmorShares& armor) noexcept -> PlayerHarmed;

/// What a player of `status` is heard saying as a harm that met it last in hit group `group` hurts it
/// (Pain), `harmed` its armor taking a share or none: in the head, its helmet's, or a headshot's
/// without one; elsewhere, its armor's but in the legs, or its flesh's.
[[nodiscard]] auto painOf(const PlayerHarmRules& rules, const PlayerStatus& status, HitGroup group,
                          bool isArmored) noexcept -> const GameSound&;

/// Whether a player of armor `kind` met in hit group `group` by another's attack sheds blood
/// (TraceAttack's bShouldBleed): in the head without a helmet; in the chest, the stomach and the arms
/// without armor; in the legs and in no group always; never on a shield.
[[nodiscard]] auto shedsBlood(HitGroup group, ArmorKind kind) noexcept -> bool;

/// Whether a player of armor `kind` met in hit group `group` by another's attack, a teammate's that
/// spares it too, strikes sparks off its helmet (TraceAttack's bShouldSpark): in the head with a helmet.
[[nodiscard]] auto sparksOffHelmet(HitGroup group, ArmorKind kind) noexcept -> bool;

/// How an attack's `damage`, its hit group's share taken, punches the view of a player of armor `kind`
/// met in hit group `group` (TraceAttack), its blood shed (shedsBlood): in the head, its pitch half
/// the damage up, 12 degrees at most, and its roll as much as the damage, drawn from `random` either
/// way, 9 degrees at most; in the chest or the stomach, without armor, its pitch a tenth of the damage
/// up, 4 at most; elsewhere none, and the punch as it was.
void punchByHit(nx::EulerAngles& punch, HitGroup group, nx::Damage damage, ArmorKind kind,
                nx::Random& random) noexcept;

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
struct nx::meta::Description<cs::ArmorShares> {
    static constexpr std::string_view kName = "cs::ArmorShares";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::ArmorShares::ratio>("ratio", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::ArmorShares::bonus>("bonus", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::PlayerAttackRules> {
    static constexpr std::string_view kName = "cs::PlayerAttackRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::PlayerAttackRules::teammateShare>("teammateShare",
                                                               nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::PlayerAttackRules::knockback>("knockback", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::PlayerAttackRules::knockbackBelowSpeed>("knockbackBelowSpeed",
                                                                     nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::PlayerAttackRules::flinchSpeedShare>(
            "flinchSpeedShare", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::PlayerAttackRules::largeFlinchSpeedShare>(
            "largeFlinchSpeedShare", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::HelmetSparks> {
    static constexpr std::string_view kName = "cs::HelmetSparks";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::HelmetSparks::color>("color"),
        nx::meta::field<&cs::HelmetSparks::count>("count"),
        nx::meta::field<&cs::HelmetSparks::speed>("speed"),
        nx::meta::field<&cs::HelmetSparks::spread>("spread"),
    };
};

template <>
struct nx::meta::Description<cs::PlayerHarmRules> {
    static constexpr std::string_view kName = "cs::PlayerHarmRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::PlayerHarmRules::fall>("fall"),
        nx::meta::field<&cs::PlayerHarmRules::drowning>("drowning"),
        nx::meta::field<&cs::PlayerHarmRules::harmfulWater>("harmfulWater"),
        nx::meta::field<&cs::PlayerHarmRules::armor>("armor"),
        nx::meta::field<&cs::PlayerHarmRules::attacks>("attacks"),
        nx::meta::field<&cs::PlayerHarmRules::pain>("pain"),
        nx::meta::field<&cs::PlayerHarmRules::headPain>("headPain"),
        nx::meta::field<&cs::PlayerHarmRules::helmetPain>("helmetPain"),
        nx::meta::field<&cs::PlayerHarmRules::helmetSparks>("helmetSparks"),
        nx::meta::field<&cs::PlayerHarmRules::kevlarPain>("kevlarPain"),
        nx::meta::field<&cs::PlayerHarmRules::splat>("splat"),
        nx::meta::field<&cs::PlayerHarmRules::death>("death"),
    };
};
