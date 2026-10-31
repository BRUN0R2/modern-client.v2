// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How the attacks that hurt an entity's body knock it back, apart from what makes and moves the body: a
/// type's rules, what a body keeps of its last hit, and what they make of its move.
export module cs.shared:entityKnockback;

import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;

export namespace cs {

/// How the attacks that hurt a body of a type and leave it alive knock it back, as a zombie mod's
/// knockback holds its zombies back under fire, and as the original's players are slowed down by theirs
/// (m_flVelocityModifier). A hit pushes the body along the attack's way across at `pushPerDamage` for each
/// point of damage it dealt, to no faster across than `mostPushSpeed`, nor slower than it went already;
/// holds its own walking back for `staggerTime`, while the push carries it; then lets it walk at
/// `slowShare` of its speed, back to the whole of it over `slowRecovery`.
struct EntityKnockback {
    nx::UnitsPerSecond pushPerDamage{};
    nx::UnitsPerSecond mostPushSpeed{};
    nx::Seconds staggerTime{};
    float slowShare = 1.0F;
    nx::Seconds slowRecovery{};
};

/// Whether `rules` are finite, none of them less than none, `mostPushSpeed` more than none and
/// `slowShare` more than none and at most 1.
[[nodiscard]] auto isValid(const EntityKnockback& rules) noexcept -> bool;

/// What a body keeps of the hits that knocked it back: when the last one met it, none before the first.
struct KnockedBack {
    std::optional<nx::GameTime> hitAt{};
};

/// `velocity` once a hit that dealt `dealt` along `direction` pushed it as `rules` say: across alone, its
/// fall kept; as it was for a hit straight up or down, or that dealt nothing.
[[nodiscard]] auto pushedBy(const EntityKnockback& rules, nx::Vec3 velocity, nx::Vec3 direction,
                            nx::Damage dealt) noexcept -> nx::Vec3;

/// The share of its speed a body `knocked` back may walk at, at `now`, by `rules`: none while staggered,
/// then from `slowShare` back to 1, and 1 once it recovered or before any hit.
[[nodiscard]] auto walkShareOf(const EntityKnockback& rules, const KnockedBack& knocked, nx::GameTime now,
                               nx::TickRate rate) noexcept -> float;

} // namespace cs

template <>
struct nx::meta::Description<cs::EntityKnockback> {
    static constexpr std::string_view kName = "cs::EntityKnockback";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::EntityKnockback::pushPerDamage>("pushPerDamage",
                                                             nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::EntityKnockback::mostPushSpeed>("mostPushSpeed",
                                                             nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::EntityKnockback::staggerTime>("staggerTime", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::EntityKnockback::slowShare>("slowShare",
                                                         nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::EntityKnockback::slowRecovery>("slowRecovery", nx::data::Range{.minimum = 0.0}),
    };
};
