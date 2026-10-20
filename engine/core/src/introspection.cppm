// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Compile-time names of types and enumerators.
///
/// Until static reflection (P2996) is available on every supported compiler, names are parsed
/// from the signature the compiler gives to a template instantiation. The parsing is isolated
/// here: the rest of the engine only sees typeName, shortTypeName and enumName, and the native
/// reflection path can replace this file without touching any caller.
export module nx.core:introspection;

import std;

export namespace nx {

/// Scoped enumeration (`enum class`).
template <class Type>
concept ScopedEnum = std::is_scoped_enum_v<Type>;

/// Scoped enumeration whose last enumerator is the `Count` sentinel (docs/02, section 4.2).
template <class Type>
concept CountedEnum = ScopedEnum<Type> && requires { Type::Count; };

/// Number of enumerators before the `Count` sentinel.
template <CountedEnum Enum>
[[nodiscard]] consteval auto enumCount() noexcept -> std::size_t {
    return static_cast<std::size_t>(std::to_underlying(Enum::Count));
}

/// Underlying values scanned for names when an enum has no `Count` sentinel.
inline constexpr std::size_t kEnumNameScanLimit = 256;

namespace detail {

inline constexpr std::string_view kScopeSeparator = "::";

template <class Type>
[[nodiscard]] consteval auto typeSignature() noexcept -> std::string_view {
#if defined(__clang__) || defined(__GNUC__)
    return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    return __FUNCSIG__;
#else
#error "nx.core:introspection: add the function signature format of this compiler"
#endif
}

template <auto Value>
[[nodiscard]] consteval auto valueSignature() noexcept -> std::string_view {
#if defined(__clang__) || defined(__GNUC__)
    return __PRETTY_FUNCTION__;
#elif defined(_MSC_VER)
    return __FUNCSIG__;
#else
#error "nx.core:introspection: add the function signature format of this compiler"
#endif
}

// Where each compiler writes the template argument in the signatures above:
//   Clang: "... typeSignature() [Type = ns::Name]"
//   GCC:   "... typeSignature() [with Type = ns::Name; std::string_view = ...]"
//   MSVC:  "... __cdecl nx::detail::typeSignature<enum ns::Name>(void) noexcept"
#if defined(__clang__)
inline constexpr std::string_view kTypeArgumentPrefix = "[Type = ";
inline constexpr std::string_view kValueArgumentPrefix = "[Value = ";
#elif defined(__GNUC__)
inline constexpr std::string_view kTypeArgumentPrefix = "[with Type = ";
inline constexpr std::string_view kValueArgumentPrefix = "[with auto Value = ";
#elif defined(_MSC_VER)
inline constexpr std::string_view kTypeArgumentPrefix = "typeSignature<";
inline constexpr std::string_view kValueArgumentPrefix = "valueSignature<";
#endif

[[nodiscard]] consteval auto templateArgument(std::string_view signature, std::string_view prefix) noexcept
    -> std::string_view {
    const auto prefixAt = signature.find(prefix);
    if (prefixAt == std::string_view::npos) {
        return {};
    }
    const auto first = prefixAt + prefix.size();
#if defined(__clang__)
    const auto last = signature.rfind(']');
#elif defined(__GNUC__)
    const auto last = signature.find_first_of(";]", first);
#else
    const auto last = signature.rfind(">(void)");
#endif
    if (last == std::string_view::npos || last < first) {
        return {};
    }
    return signature.substr(first, last - first);
}

/// MSVC spells type arguments with their class-key ("enum ns::Name").
[[nodiscard]] consteval auto withoutClassKey(std::string_view name) noexcept -> std::string_view {
    for (const std::string_view key : {"enum ", "class ", "struct ", "union "}) {
        if (name.starts_with(key)) {
            return name.substr(key.size());
        }
    }
    return name;
}

/// Last component of a qualified name, ignoring "::" inside template argument lists.
[[nodiscard]] consteval auto unqualified(std::string_view name) noexcept -> std::string_view {
    std::size_t depth = 0;
    std::size_t start = 0;
    std::size_t index = 0;
    while (index < name.size()) {
        if (depth == 0 && name.substr(index).starts_with(kScopeSeparator)) {
            index += kScopeSeparator.size();
            start = index;
            continue;
        }
        if (name[index] == '<') {
            ++depth;
        } else if (name[index] == '>' && depth > 0) {
            --depth;
        }
        ++index;
    }
    return name.substr(start);
}

[[nodiscard]] consteval auto isDecimalDigit(char character) noexcept -> bool {
    return character >= '0' && character <= '9';
}

template <auto Value>
[[nodiscard]] consteval auto enumeratorName() noexcept -> std::string_view {
    const auto argument = templateArgument(valueSignature<Value>(), kValueArgumentPrefix);
    // A value without an enumerator is printed as a cast: "(enum E)0x7" (MSVC), "(E)7" (GCC, Clang).
    if (argument.empty() || argument.front() == '(' || argument.front() == '-' ||
        isDecimalDigit(argument.front())) {
        return {};
    }
    return unqualified(argument);
}

template <ScopedEnum Enum>
[[nodiscard]] consteval auto enumScanCount() noexcept -> std::size_t {
    if constexpr (CountedEnum<Enum>) {
        return enumCount<Enum>() + 1; // also names the sentinel itself
    } else {
        constexpr auto largest =
            static_cast<std::uintmax_t>(std::numeric_limits<std::underlying_type_t<Enum>>::max());
        if constexpr (largest < kEnumNameScanLimit) {
            return static_cast<std::size_t>(largest) + 1;
        } else {
            return kEnumNameScanLimit;
        }
    }
}

template <ScopedEnum Enum, std::size_t... Index>
[[nodiscard]] consteval auto makeEnumNameTable(std::index_sequence<Index...> /*indices*/) noexcept
    -> std::array<std::string_view, sizeof...(Index)> {
    return {enumeratorName<static_cast<Enum>(Index)>()...};
}

template <ScopedEnum Enum>
inline constexpr auto kEnumNameTable =
    makeEnumNameTable<Enum>(std::make_index_sequence<enumScanCount<Enum>()>{});

} // namespace detail

/// Fully qualified name of `Type`, e.g. "nx::assets::BspError".
template <class Type>
[[nodiscard]] consteval auto typeName() noexcept -> std::string_view {
    return detail::withoutClassKey(
        detail::templateArgument(detail::typeSignature<Type>(), detail::kTypeArgumentPrefix));
}

/// Unqualified name of `Type`, e.g. "BspError".
template <class Type>
[[nodiscard]] consteval auto shortTypeName() noexcept -> std::string_view {
    return detail::unqualified(typeName<Type>());
}

/// Name of the enumerator equal to `value`; empty when no enumerator has that value or the value
/// lies outside the scanned range ([0, Count] or [0, kEnumNameScanLimit)).
template <ScopedEnum Enum>
[[nodiscard]] constexpr auto enumName(Enum value) noexcept -> std::string_view {
    constexpr const auto& names = detail::kEnumNameTable<Enum>;
    const auto raw = std::to_underlying(value);
    if (std::cmp_less(raw, 0) || std::cmp_greater_equal(raw, names.size())) {
        return {};
    }
    return names[static_cast<std::size_t>(raw)];
}

} // namespace nx
