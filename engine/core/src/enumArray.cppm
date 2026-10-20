// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Fixed-size array indexed by the enumerators of a counted enum
/// (docs/02-padroes-de-codigo.md, section 4.2): no raw indices, no hand-sized arrays.
export module nx.core:enumArray;

import std;
import :contracts;
import :introspection;

export namespace nx {

template <CountedEnum Enum, class Value>
class EnumArray {
public:
    static_assert(enumCount<Enum>() > 0, "EnumArray needs at least one enumerator before Count");

    using Storage = std::array<Value, enumCount<Enum>()>;

    constexpr EnumArray() = default;
    constexpr explicit EnumArray(const Value& fill) { m_values.fill(fill); }
    constexpr explicit EnumArray(Storage values) : m_values{std::move(values)} {}

    [[nodiscard]] constexpr auto operator[](Enum key) noexcept -> Value& { return m_values[indexOf(key)]; }
    [[nodiscard]] constexpr auto operator[](Enum key) const noexcept -> const Value& {
        return m_values[indexOf(key)];
    }

    [[nodiscard]] static constexpr auto size() noexcept -> std::size_t { return enumCount<Enum>(); }

    /// Every enumerator before Count, in declaration order.
    [[nodiscard]] static constexpr auto keys() noexcept {
        return std::views::iota(std::size_t{0}, size()) |
               std::views::transform([](std::size_t index) { return static_cast<Enum>(index); });
    }

    [[nodiscard]] constexpr auto begin() noexcept { return m_values.begin(); }
    [[nodiscard]] constexpr auto end() noexcept { return m_values.end(); }
    [[nodiscard]] constexpr auto begin() const noexcept { return m_values.begin(); }
    [[nodiscard]] constexpr auto end() const noexcept { return m_values.end(); }

    [[nodiscard]] friend constexpr auto operator==(const EnumArray&, const EnumArray&) -> bool = default;

private:
    /// A key outside [0, Count) is a contract violation. If the handler lets execution continue
    /// ("observe"), the index is clamped so the access stays inside the array.
    [[nodiscard]] static constexpr auto indexOf(Enum key) noexcept -> std::size_t {
        const auto raw = std::to_underlying(key);
        if (std::cmp_less(raw, 0) || std::cmp_greater_equal(raw, size())) {
            expects(false, "EnumArray: key outside [0, Count)");
            return size() - 1;
        }
        return static_cast<std::size_t>(raw);
    }

    Storage m_values{};
};

} // namespace nx
