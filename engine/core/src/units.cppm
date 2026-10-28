// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Units of the game domain. The Rook counterparts (5000hp, 320ups, 64hu...) are listed in
/// docs/14-rook-language.md, section 6.
export module nx.core:units;

import std;
import :contracts;
import :quantity;

export namespace nx {

// Unit tags: what each quantity below measures, with the suffix or prefix it prints with.

/// The unit of HammerUnits.
struct HammerUnitsTag {
    static constexpr std::string_view kSuffix = "hu";
};
/// The unit of UnitsPerSecond.
struct UnitsPerSecondTag {
    static constexpr std::string_view kSuffix = "ups";
};
/// The unit of UnitsPerSecondSquared.
struct UnitsPerSecondSquaredTag {
    static constexpr std::string_view kSuffix = "ups/s";
};
/// The unit of PerSecond.
struct PerSecondTag {
    static constexpr std::string_view kSuffix = "/s";
};
/// The unit of PerSecondSquared.
struct PerSecondSquaredTag {
    static constexpr std::string_view kSuffix = "/s/s";
};
/// The unit of DegreesPerSecond.
struct DegreesPerSecondTag {
    static constexpr std::string_view kSuffix = "deg/s";
};
/// The unit of DegreesPerSecondSquared.
struct DegreesPerSecondSquaredTag {
    static constexpr std::string_view kSuffix = "deg/s/s";
};
/// The unit of HitPoints.
struct HitPointsTag {
    static constexpr std::string_view kSuffix = "hp";
};
/// The unit of ArmorPoints.
struct ArmorPointsTag {
    static constexpr std::string_view kSuffix = "ap";
};
/// The unit of Damage.
struct DamageTag {
    static constexpr std::string_view kSuffix = "dmg";
};
/// The unit of Money.
struct MoneyTag {
    static constexpr std::string_view kPrefix = "$";
};
/// The unit of Scale.
struct ScaleTag {
    static constexpr std::string_view kSuffix = "x";
};
/// The unit of Degrees.
struct DegreesTag {
    static constexpr std::string_view kSuffix = "deg";
};
/// The unit of Radians.
struct RadiansTag {
    static constexpr std::string_view kSuffix = "rad";
};
/// The unit of SampleRate.
struct HertzTag {
    static constexpr std::string_view kSuffix = "Hz";
};

/// Map distance. One hammer unit is the GoldSrc world unit (about one inch).
using HammerUnits = Quantity<float, HammerUnitsTag>;

/// Speed in hammer units per second.
using UnitsPerSecond = Quantity<float, UnitsPerSecondTag>;

/// Acceleration in hammer units per second, per second (gravity).
using UnitsPerSecondSquared = Quantity<float, UnitsPerSecondSquaredTag>;

/// A rate: how much of something is gained or lost each second (friction, the acceleration
/// factors of player movement).
using PerSecond = Quantity<float, PerSecondTag>;

/// A rate of a rate: how hard a spring pulls back by how far it is stretched (its stiffness).
using PerSecondSquared = Quantity<float, PerSecondSquaredTag>;

/// How fast something turns (a rotating door), in degrees a second.
using DegreesPerSecond = Quantity<float, DegreesPerSecondTag>;

/// How fast a turn speeds up (a bot's view), in degrees a second, per second.
using DegreesPerSecondSquared = Quantity<float, DegreesPerSecondSquaredTag>;

/// Health. Floating point like the original game: fractional damage accumulates, and the HUD
/// shows the integer value derived by the original rule, without any upper limit.
using HitPoints = Quantity<float, HitPointsTag>;

/// Armor. Floating point like the original game.
using ArmorPoints = Quantity<float, ArmorPointsTag>;

/// Raw damage, before armor and hit group multipliers.
using Damage = Quantity<float, DamageTag>;

/// In-game money. Overflow is a contract violation; use addSaturated to clamp on purpose.
using Money = Quantity<std::int32_t, MoneyTag>;

/// Dimensionless multiplier (gravity, damage and speed scales).
using Scale = Quantity<float, ScaleTag>;

/// Angles. Degrees and radians never convert implicitly: toRadians and toDegrees do it.
using Degrees = Quantity<float, DegreesTag>;
using Radians = Quantity<float, RadiansTag>;

/// Sample frames a second: what a sound is made at and what a sound device plays at.
using SampleRate = Quantity<std::uint32_t, HertzTag>;

/// Simulation durations. Wall-clock time belongs to nx.platform.
using Seconds = std::chrono::duration<float>;
using Milliseconds = std::chrono::duration<float, std::milli>;

/// A half turn: the definition that relates degrees to radians.
inline constexpr Degrees kHalfTurnDegrees{180.0F};
inline constexpr Radians kHalfTurnRadians{std::numbers::pi_v<float>};

// Arithmetic across units: only the combinations with a physical meaning compile.

/// Speed. The duration must not be zero.
[[nodiscard]] constexpr auto operator/(HammerUnits distance, Seconds time) noexcept -> UnitsPerSecond {
    expects(time.count() != 0.0F, "speed: distance divided by a zero duration");
    return UnitsPerSecond{distance.value() / time.count()};
}

/// Distance covered.
[[nodiscard]] constexpr auto operator*(UnitsPerSecond speed, Seconds time) noexcept -> HammerUnits {
    return HammerUnits{speed.value() * time.count()};
}

[[nodiscard]] constexpr auto operator*(Seconds time, UnitsPerSecond speed) noexcept -> HammerUnits {
    return speed * time;
}

/// Travel time. The speed must not be zero.
[[nodiscard]] constexpr auto operator/(HammerUnits distance, UnitsPerSecond speed) noexcept -> Seconds {
    expects(speed.value() != 0.0F, "travel time: distance divided by a zero speed");
    return Seconds{distance.value() / speed.value()};
}

/// Health or armor after taking damage, without a floor: the game rules decide what reaching zero means.
[[nodiscard]] constexpr auto operator-(HitPoints health, Damage damage) noexcept -> HitPoints {
    return HitPoints{health.value() - damage.value()};
}

[[nodiscard]] constexpr auto operator-(ArmorPoints armor, Damage damage) noexcept -> ArmorPoints {
    return ArmorPoints{armor.value() - damage.value()};
}

/// Scaled damage and speed (hit group multipliers, speed modifiers).
[[nodiscard]] constexpr auto operator*(Damage damage, Scale scale) noexcept -> Damage {
    return Damage{damage.value() * scale.value()};
}

[[nodiscard]] constexpr auto operator*(Scale scale, Damage damage) noexcept -> Damage {
    return damage * scale;
}

[[nodiscard]] constexpr auto operator*(UnitsPerSecond speed, Scale scale) noexcept -> UnitsPerSecond {
    return UnitsPerSecond{speed.value() * scale.value()};
}

[[nodiscard]] constexpr auto operator*(Scale scale, UnitsPerSecond speed) noexcept -> UnitsPerSecond {
    return speed * scale;
}

/// The same angle in radians.
[[nodiscard]] constexpr auto toRadians(Degrees angle) noexcept -> Radians {
    return Radians{angle.value() * kHalfTurnRadians.value() / kHalfTurnDegrees.value()};
}

/// The same angle in degrees.
[[nodiscard]] constexpr auto toDegrees(Radians angle) noexcept -> Degrees {
    return Degrees{angle.value() * kHalfTurnDegrees.value() / kHalfTurnRadians.value()};
}

namespace detail {

/// Integer unit literals must be exactly representable in the float representation.
[[nodiscard]] consteval auto literalToFloat(unsigned long long value) noexcept -> float {
    const auto converted = static_cast<float>(value);
    invariant(static_cast<unsigned long long>(converted) == value,
              "unit literal is not exactly representable");
    return converted;
}

[[nodiscard]] consteval auto literalToFloat(long double value) noexcept -> float {
    return static_cast<float>(value);
}

} // namespace detail

/// Unit literals: `using namespace nx::literals;` then 64_hu, 250_ups, 100_hp, 0.75_x, 90_deg.
/// Money has no literal on purpose (Money{800}), matching the Rook API.
namespace literals {

[[nodiscard]] consteval auto operator""_hu(unsigned long long value) noexcept -> HammerUnits {
    return HammerUnits{detail::literalToFloat(value)};
}
[[nodiscard]] consteval auto operator""_hu(long double value) noexcept -> HammerUnits {
    return HammerUnits{detail::literalToFloat(value)};
}

[[nodiscard]] consteval auto operator""_ups(unsigned long long value) noexcept -> UnitsPerSecond {
    return UnitsPerSecond{detail::literalToFloat(value)};
}
[[nodiscard]] consteval auto operator""_ups(long double value) noexcept -> UnitsPerSecond {
    return UnitsPerSecond{detail::literalToFloat(value)};
}

[[nodiscard]] consteval auto operator""_hp(unsigned long long value) noexcept -> HitPoints {
    return HitPoints{detail::literalToFloat(value)};
}
[[nodiscard]] consteval auto operator""_hp(long double value) noexcept -> HitPoints {
    return HitPoints{detail::literalToFloat(value)};
}

[[nodiscard]] consteval auto operator""_ap(unsigned long long value) noexcept -> ArmorPoints {
    return ArmorPoints{detail::literalToFloat(value)};
}
[[nodiscard]] consteval auto operator""_ap(long double value) noexcept -> ArmorPoints {
    return ArmorPoints{detail::literalToFloat(value)};
}

[[nodiscard]] consteval auto operator""_dmg(unsigned long long value) noexcept -> Damage {
    return Damage{detail::literalToFloat(value)};
}
[[nodiscard]] consteval auto operator""_dmg(long double value) noexcept -> Damage {
    return Damage{detail::literalToFloat(value)};
}

[[nodiscard]] consteval auto operator""_x(unsigned long long value) noexcept -> Scale {
    return Scale{detail::literalToFloat(value)};
}
[[nodiscard]] consteval auto operator""_x(long double value) noexcept -> Scale {
    return Scale{detail::literalToFloat(value)};
}

[[nodiscard]] consteval auto operator""_deg(unsigned long long value) noexcept -> Degrees {
    return Degrees{detail::literalToFloat(value)};
}
[[nodiscard]] consteval auto operator""_deg(long double value) noexcept -> Degrees {
    return Degrees{detail::literalToFloat(value)};
}

[[nodiscard]] consteval auto operator""_rad(unsigned long long value) noexcept -> Radians {
    return Radians{detail::literalToFloat(value)};
}
[[nodiscard]] consteval auto operator""_rad(long double value) noexcept -> Radians {
    return Radians{detail::literalToFloat(value)};
}

} // namespace literals

} // namespace nx
