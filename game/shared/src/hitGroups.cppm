// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where an attack meets a body, and what that makes of its damage (docs/10-counter-strike-gameplay.md,
/// section 7.4): data/cstrike/hitGroups.toml, with ReGameDLL_CS's shares (CBaseMonster::TraceAttack,
/// CHostage::GetModifiedDamage; GPL, consulted: docs/20 section 3).
export module cs.shared:hitGroups;

import std;
import nx.core;
import nx.meta;
import nx.data;

export namespace cs {

/// Why the hit groups' data could not be read.
enum class HitGroupError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// What a hitbox's group is to the game (HITGROUP_*): where an attack meets a body.
enum class HitGroup : std::uint32_t {
    Generic,
    Head,
    Chest,
    Stomach,
    LeftArm,
    RightArm,
    LeftLeg,
    RightLeg,
    Shield,
};

/// The share of an attack's damage a body takes, by the hit group it meets it in: `other` for a group
/// the game names none of.
struct HitGroupShares {
    double generic = 1.0;
    double head = 1.0;
    double chest = 1.0;
    double stomach = 1.0;
    double arm = 1.0; ///< Either.
    double leg = 1.0; ///< Either.
    double shield = 1.0;
    double other = 1.0;

    /// The share of hit group `group`, a hitbox's.
    [[nodiscard]] auto of(std::uint32_t group) const noexcept -> double;
};

/// data/cstrike/hitGroups.toml: the shares of the monsters' TraceAttack, which a hostage takes an
/// attack by first, and of the hostages' TakeDamage (GetModifiedDamage), then.
struct HitGroupRules {
    HitGroupShares monster{};
    HitGroupShares hostage{};
};

/// Reads `text`, the file `sourceName`. Errors: DataInvalid.
[[nodiscard]] auto readHitGroupRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<HitGroupRules, HitGroupError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::HitGroupShares> {
    static constexpr std::string_view kName = "cs::HitGroupShares";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::HitGroupShares::generic>("generic", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HitGroupShares::head>("head", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HitGroupShares::chest>("chest", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HitGroupShares::stomach>("stomach", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HitGroupShares::arm>("arm", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HitGroupShares::leg>("leg", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HitGroupShares::shield>("shield", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::HitGroupShares::other>("other", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::HitGroupRules> {
    static constexpr std::string_view kName = "cs::HitGroupRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::HitGroupRules::monster>("monster"),
        nx::meta::field<&cs::HitGroupRules::hostage>("hostage"),
    };
};
