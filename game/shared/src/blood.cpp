// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.data;
import nx.assets;

namespace cs {

namespace {

/// Whether each spread but the last has a `below` more than the one before it, and the last none.
[[nodiscard]] auto areSpreadsInOrder(std::span<const BleedSpread> spreads) -> bool {
    if (spreads.empty() || spreads.back().below) {
        return false;
    }
    nx::Damage least{};
    for (const BleedSpread& spread : spreads.first(spreads.size() - 1)) {
        if (!spread.below || *spread.below <= least) {
            return false;
        }
        least = *spread.below;
    }
    return std::ranges::all_of(spreads, [](const BleedSpread& spread) { return spread.traces > 0; });
}

/// Whether `spray` makes sizes from one, from its smallest to its largest.
[[nodiscard]] auto isValid(const BloodSprayRules& spray) noexcept -> bool {
    return spray.amountPerDamage > 0 && spray.mostAmount > 0 && spray.amountPerSize > 0 &&
           spray.smallestSize > 0 && spray.smallestSize <= spray.largestSize;
}

/// Whether `look` names its sprites, shifts its color within the palette, shows something for a while,
/// draws its drops, and has each least no more than its most.
[[nodiscard]] auto isValid(const BloodSprayLook& look, std::uint8_t color) noexcept -> bool {
    const auto isPositive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };
    const bool isShifted = look.colorShift > 0 &&
                           std::uint32_t{color} + std::uint32_t{look.colorShift} < nx::assets::kPaletteColors;
    return !look.spraySprite.empty() && !look.dropSprite.empty() && isShifted && look.amount > 0 &&
           isPositive(look.sprayTime.count()) && look.extraDropFaces > 0 && look.mostDropFrame > 0 &&
           isPositive(look.leastDropLife.count()) && look.leastLift <= look.mostLift &&
           look.leastSprayScale <= look.mostSprayScale && look.leastDropScale <= look.mostDropScale &&
           look.leastDropSpeed <= look.mostDropSpeed && look.leastDropRise <= look.mostDropRise &&
           look.leastDropSpin <= look.mostDropSpin && look.leastDropLife <= look.mostDropLife &&
           look.dropGravity >= nx::Scale{};
}

/// The spread of a hit of `damage`: the first whose `below` it is below, else the last.
[[nodiscard]] auto spreadOf(const BloodRules& rules, nx::Damage damage) -> const BleedSpread& {
    const auto spread = std::ranges::find_if(
        rules.spreads, [damage](const BleedSpread& each) { return !each.below || damage < *each.below; });
    nx::invariant(spread != rules.spreads.end(), "bleedingWaysOf: a last spread for any damage");
    return *spread;
}

} // namespace

auto readBloodRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BloodRules, BloodError> {
    auto rules = nx::data::parseToml<BloodRules>(text, sourceName);
    if (!rules) {
        return nx::fail(BloodError::DataInvalid, std::string{sourceName}, std::move(rules.error()));
    }
    const bool isValidRules =
        !rules->decals.empty() && std::ranges::none_of(rules->decals, &std::string::empty) &&
        rules->reach > nx::HammerUnits{} && rules->marksOneIn > 0 && areSpreadsInOrder(rules->spreads) &&
        isValid(rules->spray) && isValid(rules->look, rules->spray.color);
    if (!isValidRules) {
        return nx::fail(
            BloodError::DataInvalid,
            std::format("{}: decals, a reach and one in how many ways leave a mark, more than none, "
                        "spreads of traces, each below more than the last's, the last below "
                        "none, a spray's sizes from one, and its look's sprites, color within "
                        "the palette, times and dice more than none, each least no more than its "
                        "most, are required",
                        sourceName));
    }
    return std::move(*rules);
}

auto bleedingWaysOf(const BloodRules& rules, nx::Damage damage, nx::Vec3 direction, nx::Random& random)
    -> std::vector<nx::Vec3> {
    const BleedSpread& spread = spreadOf(rules, damage);
    std::vector<nx::Vec3> ways;
    ways.reserve(spread.traces);
    for (std::uint32_t trace = 0; trace < spread.traces; ++trace) {
        const auto noise = [&random, &spread] {
            return random.between(-spread.noise, spread.noise);
        };
        // Its parts drawn in their order.
        const float x = noise();
        const float y = noise();
        const float z = noise();
        ways.push_back(direction + nx::Vec3{.x = x, .y = y, .z = z});
    }
    return ways;
}

auto spraySizeOf(const BloodSprayRules& rules, nx::Damage damage) -> std::optional<std::uint32_t> {
    // int(flDamage): its whole points, as the original's processors cut it.
    const std::int32_t points = nx::truncateAsX86(damage.value());
    if (points <= 0) {
        return std::nullopt;
    }
    const std::uint64_t amount = std::min<std::uint64_t>(
        std::uint64_t{static_cast<std::uint32_t>(points)} * rules.amountPerDamage, rules.mostAmount);
    return std::clamp(static_cast<std::uint32_t>(amount / rules.amountPerSize), rules.smallestSize,
                      rules.largestSize);
}

auto bloodDecalOf(const BloodRules& rules, nx::Random& random) -> std::optional<std::uint32_t> {
    if (random.below(rules.marksOneIn) != 0) {
        return std::nullopt;
    }
    return nx::narrow<std::uint32_t>(random.below(rules.decals.size()));
}

} // namespace cs
