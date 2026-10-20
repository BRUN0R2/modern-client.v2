// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Explicit numeric conversions and overflow-aware integer arithmetic
/// (docs/02-coding-standards.md, section 4.3). Nothing here converts implicitly or wraps silently.
///
/// The saturating functions are implemented here on every compiler instead of forwarding to
/// std::add_sat and friends: GCC 16 announces them through <version> but its `std` module does
/// not export them, and one tested implementation beats a per-compiler code path.
export module nx.core:conversion;

import std;
import :contracts;
import :failure;
import :text;

export namespace nx {

namespace detail {

template <class Type>
concept CharacterType =
    std::same_as<std::remove_cv_t<Type>, char> || std::same_as<std::remove_cv_t<Type>, wchar_t> ||
    std::same_as<std::remove_cv_t<Type>, char8_t> || std::same_as<std::remove_cv_t<Type>, char16_t> ||
    std::same_as<std::remove_cv_t<Type>, char32_t>;

} // namespace detail

/// Standard integer type. `bool` and character types are not numbers here.
template <class Type>
concept Integer =
    std::integral<Type> && !std::same_as<std::remove_cv_t<Type>, bool> && !detail::CharacterType<Type>;

/// Why a conversion of external data failed.
enum class ConversionError : std::uint8_t {
    OutOfRange, ///< The value does not fit the destination type.
    NotFinite,  ///< A floating-point value that is NaN or infinite.
};

/// Integer conversion that must not lose information; a value that does not fit is a
/// contract violation (a bug in the caller).
template <Integer To, Integer From>
[[nodiscard]] constexpr auto narrow(From value,
                                    std::source_location where = std::source_location::current()) noexcept
    -> To {
    expects(std::in_range<To>(value), "narrow: value does not fit the destination type", where);
    return static_cast<To>(value);
}

namespace detail {

/// The detail of a conversion out of the range of `To`: "300 does not fit in [0, 255]".
template <Integer To, class Value>
[[nodiscard]] auto doesNotFit(Value value) -> std::string {
    return std::format("{} does not fit {}", value,
                       boundsText<To>(std::numeric_limits<To>::min(), std::numeric_limits<To>::max()));
}

} // namespace detail

/// Integer conversion of external data; a value that does not fit is a typed failure.
template <Integer To, Integer From>
[[nodiscard]] auto tryNarrow(From value, std::source_location where = std::source_location::current())
    -> Result<To, ConversionError> {
    if (!std::in_range<To>(value)) {
        return fail(ConversionError::OutOfRange, detail::doesNotFit<To>(value), where);
    }
    return static_cast<To>(value);
}

/// Intentional modular conversion that keeps the low bits of `value`. The caller documents why
/// losing the high bits is correct (wire formats, hashes).
template <Integer To, Integer From>
[[nodiscard]] constexpr auto truncate(From value) noexcept -> To {
    return static_cast<To>(value);
}

namespace detail {

/// Smallest power of two above the range of `To`, as `From` (exactly representable).
template <Integer To, std::floating_point From>
[[nodiscard]] consteval auto upperBoundAs() noexcept -> From {
    const auto radix = static_cast<From>(std::numeric_limits<From>::radix);
    From bound = 1;
    for (int bit = 0; bit < std::numeric_limits<To>::digits; ++bit) {
        bound *= radix;
    }
    return bound;
}

} // namespace detail

/// Rounds to the nearest integer (halfway cases away from zero) and checks the range.
template <Integer To, std::floating_point From>
[[nodiscard]] auto tryRound(From value, std::source_location where = std::source_location::current())
    -> Result<To, ConversionError> {
    if (!std::isfinite(value)) {
        return fail(ConversionError::NotFinite, std::format("{}", value), where);
    }
    const From rounded = std::round(value);
    constexpr auto lowest = static_cast<From>(std::numeric_limits<To>::min());
    constexpr auto upperBound = detail::upperBoundAs<To, From>();
    if (rounded < lowest || rounded >= upperBound) {
        return fail(ConversionError::OutOfRange, detail::doesNotFit<To>(value), where);
    }
    return static_cast<To>(rounded);
}

/// `a + b`, or nullopt when the exact result does not fit in `Type`.
template <Integer Type>
[[nodiscard]] constexpr auto checkedAdd(Type a, Type b) noexcept -> std::optional<Type> {
    constexpr auto max = std::numeric_limits<Type>::max();
    constexpr auto min = std::numeric_limits<Type>::min();
    if constexpr (std::is_signed_v<Type>) {
        if ((b > 0 && a > max - b) || (b < 0 && a < min - b)) {
            return std::nullopt;
        }
    } else {
        if (a > max - b) {
            return std::nullopt;
        }
    }
    return static_cast<Type>(a + b);
}

/// `a - b`, or nullopt when the exact result does not fit in `Type`.
template <Integer Type>
[[nodiscard]] constexpr auto checkedSub(Type a, Type b) noexcept -> std::optional<Type> {
    constexpr auto max = std::numeric_limits<Type>::max();
    constexpr auto min = std::numeric_limits<Type>::min();
    if constexpr (std::is_signed_v<Type>) {
        if ((b < 0 && a > max + b) || (b > 0 && a < min + b)) {
            return std::nullopt;
        }
    } else {
        if (a < b) {
            return std::nullopt;
        }
    }
    return static_cast<Type>(a - b);
}

/// `a * b`, or nullopt when the exact result does not fit in `Type`.
template <Integer Type>
[[nodiscard]] constexpr auto checkedMul(Type a, Type b) noexcept -> std::optional<Type> {
    constexpr auto max = std::numeric_limits<Type>::max();
    constexpr auto min = std::numeric_limits<Type>::min();
    if constexpr (std::is_signed_v<Type>) {
        if (a > 0) {
            if ((b > 0 && a > max / b) || (b <= 0 && b < min / a)) {
                return std::nullopt;
            }
        } else if ((b > 0 && a < min / b) || (b <= 0 && a != 0 && b < max / a)) {
            return std::nullopt;
        }
    } else {
        if (b != 0 && a > max / b) {
            return std::nullopt;
        }
    }
    return static_cast<Type>(a * b);
}

/// `a + b` clamped to the range of `Type` (same results as C++26 std::add_sat).
template <Integer Type>
[[nodiscard]] constexpr auto addSaturated(Type a, Type b) noexcept -> Type {
    if (const auto sum = checkedAdd(a, b)) {
        return *sum;
    }
    return b > 0 ? std::numeric_limits<Type>::max() : std::numeric_limits<Type>::min();
}

/// `a - b` clamped to the range of `Type` (same results as C++26 std::sub_sat).
template <Integer Type>
[[nodiscard]] constexpr auto subSaturated(Type a, Type b) noexcept -> Type {
    if (const auto difference = checkedSub(a, b)) {
        return *difference;
    }
    if constexpr (std::is_signed_v<Type>) {
        return b < 0 ? std::numeric_limits<Type>::max() : std::numeric_limits<Type>::min();
    } else {
        return std::numeric_limits<Type>::min();
    }
}

/// `a * b` clamped to the range of `Type` (same results as C++26 std::mul_sat).
template <Integer Type>
[[nodiscard]] constexpr auto mulSaturated(Type a, Type b) noexcept -> Type {
    if (const auto product = checkedMul(a, b)) {
        return *product;
    }
    if constexpr (std::is_signed_v<Type>) {
        return (a < 0) != (b < 0) ? std::numeric_limits<Type>::min() : std::numeric_limits<Type>::max();
    } else {
        return std::numeric_limits<Type>::max();
    }
}

/// `dividend / divisor` rounded up: how many groups of `divisor` hold `dividend` items. `divisor`
/// must not be 0.
template <std::unsigned_integral Type>
    requires Integer<Type>
[[nodiscard]] constexpr auto divideRoundingUp(Type dividend, Type divisor) noexcept -> Type {
    expects(divisor != 0, "divideRoundingUp: division by zero");
    if (divisor == 0) {
        return 0; // the handler let execution continue ("observe")
    }
    return static_cast<Type>(dividend / divisor + (dividend % divisor != 0 ? 1 : 0));
}

} // namespace nx
