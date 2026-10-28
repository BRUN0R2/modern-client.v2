// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.collision;

namespace cs {

namespace {

/// What a round deals after the first thing it meets, unless what it met says otherwise.
constexpr float kFirstDamageModifier = 0.5F;
/// What is not a brush model, or the last thing a round may meet, lets it go this far in, deals this
/// share after, and leaves it this share of the way left; a brush model leaves it half of it.
constexpr std::int32_t kSoftPower = 42;
constexpr float kSoftDamageModifier = 0.75F;
constexpr float kSoftDistanceModifier = 0.75F;
constexpr float kBrushDistanceModifier = 0.5F;
/// A round's damage is its range modifier's power of the units it went over this.
constexpr float kRangeStep = 500.0F;

/// What a round meeting a texture made of a material keeps of how far it goes into it, and what it
/// deals after, when it says.
struct MaterialEffect {
    double powerKept = 1.0;
    std::optional<float> damageModifier{};
};

/// FireBullets3's materials.
[[nodiscard]] auto effectOf(TextureMaterial material) noexcept -> MaterialEffect {
    switch (material) {
    case TextureMaterial::Metal:
        return {.powerKept = 0.15, .damageModifier = 0.2F};
    case TextureMaterial::Concrete:
        return {.powerKept = 0.25};
    case TextureMaterial::Grate:
        return {.powerKept = 0.5, .damageModifier = 0.4F};
    case TextureMaterial::Vent:
        return {.powerKept = 0.5, .damageModifier = 0.45F};
    case TextureMaterial::Tile:
        return {.powerKept = 0.65, .damageModifier = 0.3F};
    case TextureMaterial::Computer:
        return {.powerKept = 0.4, .damageModifier = 0.45F};
    case TextureMaterial::Wood:
        return {.damageModifier = 0.6F};
    default:
        return {};
    }
}

/// `value`, a round's power or damage, cut to a whole number, as the original's int stores it.
[[nodiscard]] auto cut(double value) noexcept -> std::int32_t {
    return static_cast<std::int32_t>(std::trunc(value));
}

} // namespace

auto followRound(const Shot& shot, const WeaponTracer& tracer, const HitMaterial& materialOf)
    -> std::vector<BulletHit> {
    const ShotRound& round = shot.round;
    std::vector<BulletHit> hits;
    std::uint32_t penetration = round.penetration;
    auto power = static_cast<std::int32_t>(round.penetrationPower);
    std::int32_t damage = nx::truncateAsX86(round.damage.value());
    float damageModifier = kFirstDamageModifier;
    float distance = shot.reach.value();
    const nx::Vec3 direction = shot.direction;
    nx::Vec3 start = shot.origin;
    nx::Vec3 end = start + (direction * distance);
    while (penetration != 0) {
        const WeaponHit met = tracer(nx::collision::Hull::Point, start, end);
        if (const std::optional<TextureMaterial> material = materialOf(met, start, end)) {
            const MaterialEffect effect = effectOf(*material);
            power = cut(static_cast<double>(power) * effect.powerKept);
            damageModifier = effect.damageModifier.value_or(damageModifier);
        }
        if (met.trace.fraction == 1.0F) {
            break;
        }
        --penetration;
        const float travelled = met.trace.fraction * distance;
        const float rangedDamage =
            static_cast<float>(damage) * std::pow(round.rangeModifier, travelled / kRangeStep);
        damage = cut(static_cast<double>(rangedDamage));
        if (travelled > round.penetrationDistance.value()) {
            penetration = 0;
        }
        float distanceModifier = kBrushDistanceModifier;
        if (!met.isBrushModel || penetration == 0) {
            power = kSoftPower;
            damageModifier = kSoftDamageModifier;
            distanceModifier = kSoftDistanceModifier;
        }
        hits.push_back(BulletHit{.entity = met.entity,
                                 .trace = met.trace,
                                 .damage = nx::Damage{static_cast<float>(damage)},
                                 .direction = direction,
                                 .isBrushModel = met.isBrushModel});
        start = met.trace.end + (direction * static_cast<float>(power));
        distance = (distance - travelled) * distanceModifier;
        end = start + (direction * distance);
        damage = cut(static_cast<double>(static_cast<float>(damage) * damageModifier));
    }
    return hits;
}

} // namespace cs
