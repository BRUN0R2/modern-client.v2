// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Where an attack meets a body, and what that makes of its damage (docs/10-counter-strike-gameplay.md,
/// section 7.4): data/cstrike/hitGroups.toml, with ReGameDLL_CS's shares (CBaseMonster::TraceAttack,
/// CHostage::GetModifiedDamage, CBasePlayer::TraceAttack; GPL, consulted: docs/20 section 3).
export module cs.shared:hitGroups;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.collision;

export namespace cs {

/// Why the hit groups' data could not be read.
enum class HitGroupError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// What a hitbox's group is to the game (HITGROUP_*): where an attack meets a body. A model's group the
/// game names none of keeps its number, which no enumerator names.
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

/// Whether `group` is a leg's.
[[nodiscard]] constexpr auto isLeg(HitGroup group) noexcept -> bool {
    return group == HitGroup::LeftLeg || group == HitGroup::RightLeg;
}

/// The hit group of the hitbox that stopped `trace`, as the game names the model's group number (the
/// original's trace_t::hitgroup); Generic for what has none.
[[nodiscard]] inline auto hitGroupOf(const nx::collision::Trace& trace) noexcept -> HitGroup {
    return HitGroup{trace.hitGroup};
}

/// The unit of DamageShare: none.
struct DamageShareTag {};

/// The share of an attack's damage a body takes (by the hit group it meets it in, a teammate's), in
/// double precision as the original's damage, a float, times a double literal.
using DamageShare = nx::Quantity<double, DamageShareTag>;

/// `damage` scaled by `share`, in double precision, rounded once to a float.
[[nodiscard]] inline auto scaled(nx::Damage damage, DamageShare share) noexcept -> nx::Damage {
    return nx::Damage{static_cast<float>(static_cast<double>(damage.value()) * share.value())};
}

/// The share of an attack's damage a body takes, by the hit group it meets it in: `other` for a group
/// the game names none of.
struct HitGroupShares {
    DamageShare generic{1.0};
    DamageShare head{1.0};
    DamageShare chest{1.0};
    DamageShare stomach{1.0};
    DamageShare arm{1.0}; ///< Either.
    DamageShare leg{1.0}; ///< Either.
    DamageShare shield{1.0};
    DamageShare other{1.0};

    /// The share of hit group `group`.
    [[nodiscard]] auto of(HitGroup group) const noexcept -> DamageShare;
};

/// data/cstrike/hitGroups.toml: the shares of the monsters' TraceAttack, which a hostage takes an
/// attack by first, and of the hostages' TakeDamage (GetModifiedDamage), then; and of the players'
/// TraceAttack.
struct HitGroupRules {
    HitGroupShares monster{};
    HitGroupShares hostage{};
    HitGroupShares player{};
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
        nx::meta::field<&cs::HitGroupRules::player>("player"),
    };
};
