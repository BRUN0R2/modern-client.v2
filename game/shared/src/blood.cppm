// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The blood a hit sheds (docs/10-counter-strike-gameplay.md, section 7.4): data/cstrike/blood.toml,
/// with ReGameDLL_CS's way of it on the server (CBaseEntity::TraceBleed, UTIL_BloodDecalTrace,
/// UTIL_BloodDrips; GPL, consulted: docs/20 section 3) and Xash3D-FWGS's of the spray on the client
/// (R_BloodSprite; GPL, consulted). Whether a hit sheds blood is the player's armor's to tell
/// (shedsBlood).
export module cs.shared:blood;

import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;

export namespace cs {

/// Why the blood's data could not be read.
enum class BloodError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// How many ways a hit of some damage traces the blood it sheds, each turned off the hit's way by up to
/// `noise` on each axis: those of a damage below `below`, or of any for none.
struct BleedSpread {
    std::optional<nx::Damage> below{};
    std::uint32_t traces = 0;
    float noise = 0.0F;
};

/// How the server tells its clients of the blood a hit sprays where it meets a body (UTIL_BloodDrips,
/// TE_BLOODSPRITE): the palette entry of its color (BLOOD_COLOR_RED), and its size: the damage's whole
/// points times amountPerDamage (the multiplayer rules'), mostAmount at most, over amountPerSize, from
/// smallestSize to largestSize.
struct BloodSprayRules {
    std::uint8_t color = 0;
    std::uint32_t amountPerDamage = 0;
    std::uint32_t mostAmount = 0;
    std::uint32_t amountPerSize = 0;
    std::uint32_t smallestSize = 0;
    std::uint32_t largestSize = 0;
};

/// How a client shows a spray of blood (R_BloodSprite): its sprites, the spray's and its drops', the
/// spray's color the palette entry of the spray's plus 1 to colorShift, the drops' the entry before it,
/// both as opaque as `amount` over 255. The spray, lifted from where the hit met the body by leastLift to
/// mostLift, its scale its size times leastSprayScale to mostSprayScale, plays its frames once in
/// sprayTime. Its drops, as many as its size and extraDropDice dice of extraDropFaces faces, their
/// scales its size times leastDropScale to mostDropScale, fly off across the ground at leastDropSpeed to
/// mostDropSpeed on each axis and up at leastDropRise to mostDropRise, spin at leastDropSpin to
/// mostDropSpin, fall at dropGravity of the world's gravity, stop where they meet the world, and go
/// after leastDropLife to mostDropLife, each on one of its sprite's frames from the second, the last for
/// one beyond mostDropFrame. All drawn at random, each roll a whole turn at most.
struct BloodSprayLook {
    std::string spraySprite;
    std::string dropSprite;
    std::uint8_t colorShift = 0;
    std::uint8_t amount = 0;
    nx::HammerUnits leastLift{};
    nx::HammerUnits mostLift{};
    nx::Scale leastSprayScale{};
    nx::Scale mostSprayScale{};
    nx::Seconds sprayTime{};
    std::uint32_t extraDropDice = 0;
    std::uint32_t extraDropFaces = 0;
    nx::Scale leastDropScale{};
    nx::Scale mostDropScale{};
    nx::UnitsPerSecond leastDropSpeed{};
    nx::UnitsPerSecond mostDropSpeed{};
    nx::UnitsPerSecond leastDropRise{};
    nx::UnitsPerSecond mostDropRise{};
    nx::DegreesPerSecond leastDropSpin{};
    nx::DegreesPerSecond mostDropSpin{};
    nx::Scale dropGravity{};
    nx::Seconds leastDropLife{};
    nx::Seconds mostDropLife{};
    std::uint32_t mostDropFrame = 0;
};

/// data/cstrike/blood.toml: the decals blood leaves (DECAL_BLOOD1 to DECAL_BLOOD6), how far behind a hit
/// it reaches, one in how many of its ways that meet what is solid leave a mark, and its spreads by the
/// hit's damage, the first whose `below` the damage is below, the last of any; how the server tells of
/// the blood a hit sprays, and how a client shows it.
struct BloodRules {
    std::vector<std::string> decals;
    nx::HammerUnits reach{};
    std::uint32_t marksOneIn = 0;
    std::vector<BleedSpread> spreads;
    BloodSprayRules spray{};
    BloodSprayLook look{};
};

/// Reads `text`, the file `sourceName`: decals, a reach and a share of marks, and spreads of traces and
/// noise, each `below` more than the last's, none for the last alone; a spray's sizes from one, its
/// color shifted within the palette, its sprites, its times, scales and dice more than none, and each
/// least no more than its most. Errors: DataInvalid.
[[nodiscard]] auto readBloodRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BloodRules, BloodError>;

/// The ways a hit of `damage` going `direction` traces the blood it sheds behind it (TraceBleed): as
/// many as its spread, each `direction` turned by up to the spread's noise on each axis, drawn from
/// `random`.
[[nodiscard]] auto bleedingWaysOf(const BloodRules& rules, nx::Damage damage, nx::Vec3 direction,
                                  nx::Random& random) -> std::vector<nx::Vec3>;

/// The decal, of BloodRules::decals, that a way of blood meeting what is solid leaves: one in
/// BloodRules::marksOneIn of them, each decal as likely (UTIL_BloodDecalTrace), drawn from `random`;
/// none for the others.
[[nodiscard]] auto bloodDecalOf(const BloodRules& rules, nx::Random& random) -> std::optional<std::uint32_t>;

/// The size of the spray of blood a hit of `damage` makes (UTIL_BloodDrips); none for a hit of no whole
/// point, which sprays none.
[[nodiscard]] auto spraySizeOf(const BloodSprayRules& rules, nx::Damage damage)
    -> std::optional<std::uint32_t>;

} // namespace cs

template <>
struct nx::meta::Description<cs::BleedSpread> {
    static constexpr std::string_view kName = "cs::BleedSpread";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BleedSpread::below>("below"),
        nx::meta::field<&cs::BleedSpread::traces>("traces"),
        nx::meta::field<&cs::BleedSpread::noise>("noise", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::BloodSprayRules> {
    static constexpr std::string_view kName = "cs::BloodSprayRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodSprayRules::color>("color"),
        nx::meta::field<&cs::BloodSprayRules::amountPerDamage>("amountPerDamage"),
        nx::meta::field<&cs::BloodSprayRules::mostAmount>("mostAmount"),
        nx::meta::field<&cs::BloodSprayRules::amountPerSize>("amountPerSize"),
        nx::meta::field<&cs::BloodSprayRules::smallestSize>("smallestSize"),
        nx::meta::field<&cs::BloodSprayRules::largestSize>("largestSize"),
    };
};

template <>
struct nx::meta::Description<cs::BloodSprayLook> {
    static constexpr std::string_view kName = "cs::BloodSprayLook";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodSprayLook::spraySprite>("spraySprite"),
        nx::meta::field<&cs::BloodSprayLook::dropSprite>("dropSprite"),
        nx::meta::field<&cs::BloodSprayLook::colorShift>("colorShift"),
        nx::meta::field<&cs::BloodSprayLook::amount>("amount"),
        nx::meta::field<&cs::BloodSprayLook::leastLift>("leastLift"),
        nx::meta::field<&cs::BloodSprayLook::mostLift>("mostLift"),
        nx::meta::field<&cs::BloodSprayLook::leastSprayScale>("leastSprayScale"),
        nx::meta::field<&cs::BloodSprayLook::mostSprayScale>("mostSprayScale"),
        nx::meta::field<&cs::BloodSprayLook::sprayTime>("sprayTime"),
        nx::meta::field<&cs::BloodSprayLook::extraDropDice>("extraDropDice"),
        nx::meta::field<&cs::BloodSprayLook::extraDropFaces>("extraDropFaces"),
        nx::meta::field<&cs::BloodSprayLook::leastDropScale>("leastDropScale"),
        nx::meta::field<&cs::BloodSprayLook::mostDropScale>("mostDropScale"),
        nx::meta::field<&cs::BloodSprayLook::leastDropSpeed>("leastDropSpeed"),
        nx::meta::field<&cs::BloodSprayLook::mostDropSpeed>("mostDropSpeed"),
        nx::meta::field<&cs::BloodSprayLook::leastDropRise>("leastDropRise"),
        nx::meta::field<&cs::BloodSprayLook::mostDropRise>("mostDropRise"),
        nx::meta::field<&cs::BloodSprayLook::leastDropSpin>("leastDropSpin"),
        nx::meta::field<&cs::BloodSprayLook::mostDropSpin>("mostDropSpin"),
        nx::meta::field<&cs::BloodSprayLook::dropGravity>("dropGravity"),
        nx::meta::field<&cs::BloodSprayLook::leastDropLife>("leastDropLife"),
        nx::meta::field<&cs::BloodSprayLook::mostDropLife>("mostDropLife"),
        nx::meta::field<&cs::BloodSprayLook::mostDropFrame>("mostDropFrame"),
    };
};

template <>
struct nx::meta::Description<cs::BloodRules> {
    static constexpr std::string_view kName = "cs::BloodRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodRules::decals>("decals"),
        nx::meta::field<&cs::BloodRules::reach>("reach"),
        nx::meta::field<&cs::BloodRules::marksOneIn>("marksOneIn"),
        nx::meta::field<&cs::BloodRules::spreads>("spreads"),
        nx::meta::field<&cs::BloodRules::spray>("spray"),
        nx::meta::field<&cs::BloodRules::look>("look"),
    };
};
