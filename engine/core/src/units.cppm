/// Units of the game domain. The Rook counterparts (5000hp, 320ups, 64hu...) are listed in
/// docs/14-linguagem-rook.md, section 6.
export module nx.core:units;

import std;
import :contracts;
import :quantity;

export namespace nx {

struct HammerUnitsTag {
    static constexpr std::string_view kSuffix = "hu";
};
struct UnitsPerSecondTag {
    static constexpr std::string_view kSuffix = "ups";
};
struct HitPointsTag {
    static constexpr std::string_view kSuffix = "hp";
};
struct ArmorPointsTag {
    static constexpr std::string_view kSuffix = "ap";
};
struct DamageTag {
    static constexpr std::string_view kSuffix = "dmg";
};
struct MoneyTag {
    static constexpr std::string_view kPrefix = "$";
};
struct ScaleTag {
    static constexpr std::string_view kSuffix = "x";
};
struct DegreesTag {
    static constexpr std::string_view kSuffix = "deg";
};
struct RadiansTag {
    static constexpr std::string_view kSuffix = "rad";
};

/// Map distance. One hammer unit is the GoldSrc world unit (about one inch).
using HammerUnits = Quantity<float, HammerUnitsTag>;

/// Speed in hammer units per second.
using UnitsPerSecond = Quantity<float, UnitsPerSecondTag>;

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

using Degrees = Quantity<float, DegreesTag>;
using Radians = Quantity<float, RadiansTag>;

/// Simulation durations. Wall-clock time belongs to nx.platform.
using Seconds = std::chrono::duration<float>;
using Milliseconds = std::chrono::duration<float, std::milli>;

/// A half turn: the definition that relates degrees to radians.
inline constexpr Degrees kHalfTurnDegrees{180.0F};
inline constexpr Radians kHalfTurnRadians{std::numbers::pi_v<float>};

[[nodiscard]] constexpr auto operator/(HammerUnits distance, Seconds time) noexcept -> UnitsPerSecond {
    expects(time.count() != 0.0F, "speed: distance divided by a zero duration");
    return UnitsPerSecond{distance.value() / time.count()};
}

[[nodiscard]] constexpr auto operator*(UnitsPerSecond speed, Seconds time) noexcept -> HammerUnits {
    return HammerUnits{speed.value() * time.count()};
}

[[nodiscard]] constexpr auto operator*(Seconds time, UnitsPerSecond speed) noexcept -> HammerUnits {
    return speed * time;
}

[[nodiscard]] constexpr auto operator/(HammerUnits distance, UnitsPerSecond speed) noexcept -> Seconds {
    expects(speed.value() != 0.0F, "travel time: distance divided by a zero speed");
    return Seconds{distance.value() / speed.value()};
}

[[nodiscard]] constexpr auto operator-(HitPoints health, Damage damage) noexcept -> HitPoints {
    return HitPoints{health.value() - damage.value()};
}

[[nodiscard]] constexpr auto operator-(ArmorPoints armor, Damage damage) noexcept -> ArmorPoints {
    return ArmorPoints{armor.value() - damage.value()};
}

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

[[nodiscard]] constexpr auto toRadians(Degrees angle) noexcept -> Radians {
    return Radians{angle.value() * kHalfTurnRadians.value() / kHalfTurnDegrees.value()};
}

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
