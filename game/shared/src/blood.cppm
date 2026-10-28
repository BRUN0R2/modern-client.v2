// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The blood a hit sheds on what is behind it (docs/10-counter-strike-gameplay.md, section 7.4):
/// data/cstrike/blood.toml, with ReGameDLL_CS's way of it (CBaseEntity::TraceBleed,
/// UTIL_BloodDecalTrace; GPL, consulted: docs/20 section 3). Whether a hit sheds blood is the player's
/// armor's to tell (shedsBlood).
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

/// data/cstrike/blood.toml: the decals blood leaves (DECAL_BLOOD1 to DECAL_BLOOD6), how far behind a hit
/// it reaches, one in how many of its ways that meet what is solid leave a mark, and its spreads by the
/// hit's damage, the first whose `below` the damage is below, the last of any.
struct BloodRules {
    std::vector<std::string> decals;
    nx::HammerUnits reach{};
    std::uint32_t marksOneIn = 0;
    std::vector<BleedSpread> spreads;
};

/// Reads `text`, the file `sourceName`: decals, a reach and a share of marks, and spreads of traces and
/// noise, each `below` more than the last's, none for the last alone. Errors: DataInvalid.
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
struct nx::meta::Description<cs::BloodRules> {
    static constexpr std::string_view kName = "cs::BloodRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodRules::decals>("decals"),
        nx::meta::field<&cs::BloodRules::reach>("reach"),
        nx::meta::field<&cs::BloodRules::marksOneIn>("marksOneIn"),
        nx::meta::field<&cs::BloodRules::spreads>("spreads"),
    };
};
