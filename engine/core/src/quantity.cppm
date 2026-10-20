// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later

/// Strong types for domain quantities (docs/02-padroes-de-codigo.md, section 4.1).
///
/// A Quantity<Rep, Tag> never converts implicitly from or to Rep, nor to a quantity with
/// another tag: HitPoints + ArmorPoints does not compile. Integer quantities never wrap: an
/// overflow is a contract violation and the result saturates.
export module nx.core:quantity;

import std;
import :contracts;
import :conversion;

export namespace nx {

template <class Rep>
concept QuantityRepresentation = Integer<Rep> || std::floating_point<Rep>;

/// Unit tag with a suffix used when formatting, e.g. "64hu".
template <class Tag>
concept UnitWithSuffix = requires {
    { Tag::kSuffix } -> std::convertible_to<std::string_view>;
};

/// Unit tag with a prefix used when formatting, e.g. "$800".
template <class Tag>
concept UnitWithPrefix = requires {
    { Tag::kPrefix } -> std::convertible_to<std::string_view>;
};

namespace detail {

template <QuantityRepresentation Rep>
[[nodiscard]] constexpr auto quantityAdd(Rep a, Rep b) noexcept -> Rep {
    if constexpr (Integer<Rep>) {
        if (const auto sum = checkedAdd(a, b)) {
            return *sum;
        }
        invariant(false, "Quantity: integer overflow in addition");
        return addSaturated(a, b);
    } else {
        return a + b;
    }
}

template <QuantityRepresentation Rep>
[[nodiscard]] constexpr auto quantitySub(Rep a, Rep b) noexcept -> Rep {
    if constexpr (Integer<Rep>) {
        if (const auto difference = checkedSub(a, b)) {
            return *difference;
        }
        invariant(false, "Quantity: integer overflow in subtraction");
        return subSaturated(a, b);
    } else {
        return a - b;
    }
}

template <QuantityRepresentation Rep>
[[nodiscard]] constexpr auto quantityMul(Rep a, Rep b) noexcept -> Rep {
    if constexpr (Integer<Rep>) {
        if (const auto product = checkedMul(a, b)) {
            return *product;
        }
        invariant(false, "Quantity: integer overflow in multiplication");
        return mulSaturated(a, b);
    } else {
        return a * b;
    }
}

template <QuantityRepresentation Rep>
[[nodiscard]] constexpr auto quantityDiv(Rep a, Rep b) noexcept -> Rep {
    if constexpr (Integer<Rep>) {
        if (b == 0) {
            invariant(false, "Quantity: integer division by zero");
            return Rep{};
        }
        if constexpr (std::is_signed_v<Rep>) {
            // min / -1 is the one signed division that overflows.
            if (a == std::numeric_limits<Rep>::min() && b == -1) {
                invariant(false, "Quantity: integer overflow in division");
                return std::numeric_limits<Rep>::max();
            }
        }
        return static_cast<Rep>(a / b);
    } else {
        return a / b;
    }
}

} // namespace detail

template <QuantityRepresentation Rep, class Tag>
class Quantity {
public:
    using Representation = Rep;
    using UnitTag = Tag;

    constexpr Quantity() noexcept = default;
    constexpr explicit Quantity(Rep value) noexcept : m_value{value} {}

    [[nodiscard]] constexpr auto value() const noexcept -> Rep { return m_value; }
    [[nodiscard]] static constexpr auto zero() noexcept -> Quantity { return Quantity{}; }

    [[nodiscard]] constexpr auto operator<=>(const Quantity&) const noexcept = default;

    constexpr auto operator+=(Quantity other) noexcept -> Quantity& {
        m_value = detail::quantityAdd(m_value, other.m_value);
        return *this;
    }

    constexpr auto operator-=(Quantity other) noexcept -> Quantity& {
        m_value = detail::quantitySub(m_value, other.m_value);
        return *this;
    }

    constexpr auto operator*=(Rep factor) noexcept -> Quantity& {
        m_value = detail::quantityMul(m_value, factor);
        return *this;
    }

    constexpr auto operator/=(Rep divisor) noexcept -> Quantity& {
        m_value = detail::quantityDiv(m_value, divisor);
        return *this;
    }

    [[nodiscard]] friend constexpr auto operator+(Quantity a, Quantity b) noexcept -> Quantity {
        return a += b;
    }
    [[nodiscard]] friend constexpr auto operator-(Quantity a, Quantity b) noexcept -> Quantity {
        return a -= b;
    }
    [[nodiscard]] friend constexpr auto operator*(Quantity a, Rep factor) noexcept -> Quantity {
        return a *= factor;
    }
    [[nodiscard]] friend constexpr auto operator*(Rep factor, Quantity a) noexcept -> Quantity {
        return a *= factor;
    }
    [[nodiscard]] friend constexpr auto operator/(Quantity a, Rep divisor) noexcept -> Quantity {
        return a /= divisor;
    }

    /// Dimensionless ratio between two quantities of the same unit.
    [[nodiscard]] friend constexpr auto operator/(Quantity a, Quantity b) noexcept -> Rep {
        return detail::quantityDiv(a.m_value, b.m_value);
    }

    [[nodiscard]] friend constexpr auto operator-(Quantity a) noexcept -> Quantity
        requires std::is_signed_v<Rep>
    {
        return Quantity::zero() - a;
    }

private:
    Rep m_value{};
};

/// Sum clamped to the range of the representation (e.g. Money that must stop at its limit).
template <Integer Rep, class Tag>
[[nodiscard]] constexpr auto addSaturated(Quantity<Rep, Tag> a, Quantity<Rep, Tag> b) noexcept
    -> Quantity<Rep, Tag> {
    return Quantity<Rep, Tag>{addSaturated(a.value(), b.value())};
}

/// Difference clamped to the range of the representation.
template <Integer Rep, class Tag>
[[nodiscard]] constexpr auto subSaturated(Quantity<Rep, Tag> a, Quantity<Rep, Tag> b) noexcept
    -> Quantity<Rep, Tag> {
    return Quantity<Rep, Tag>{subSaturated(a.value(), b.value())};
}

} // namespace nx

/// Formats the value with the format spec of Rep, wrapped in the unit prefix and suffix:
/// std::format("{:.1f}", 1.5_hu) == "1.5hu", std::format("{}", Money{800}) == "$800".
template <nx::QuantityRepresentation Rep, class Tag>
struct std::formatter<nx::Quantity<Rep, Tag>> : std::formatter<Rep> {
    template <class FormatContext>
    auto format(const nx::Quantity<Rep, Tag>& quantity, FormatContext& context) const {
        auto out = context.out();
        if constexpr (nx::UnitWithPrefix<Tag>) {
            out = std::ranges::copy(Tag::kPrefix, out).out;
            context.advance_to(out);
        }
        out = std::formatter<Rep>::format(quantity.value(), context);
        if constexpr (nx::UnitWithSuffix<Tag>) {
            out = std::ranges::copy(Tag::kSuffix, out).out;
        }
        return out;
    }
};
