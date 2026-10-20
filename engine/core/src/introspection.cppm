// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Compile-time names of types and enumerators.
///
/// Until static reflection (P2996) is available on every supported compiler, names are parsed
/// from the signature the compiler gives to a template instantiation. The parsing is isolated
/// here: the rest of the engine only sees typeName, shortTypeName, enumName and enumerators, and
/// the native reflection path can replace this file without touching any caller.
export module nx.core:introspection;

import std;
import :contracts;

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

#if defined(__GNUC__) && !defined(__clang__)
/// GCC ends the argument at the next binding (';') or at the ']' closing the list, outside the
/// brackets of an array type ("int [3]").
[[nodiscard]] consteval auto argumentEnd(std::string_view signature, std::size_t first) noexcept
    -> std::size_t {
    std::size_t depth = 0;
    for (std::size_t index = first; index < signature.size(); ++index) {
        const char character = signature[index];
        if (character == '[') {
            ++depth;
        } else if ((character == ']' && depth == 0) || (character == ';' && depth == 0)) {
            return index;
        } else if (character == ']') {
            --depth;
        }
    }
    return std::string_view::npos;
}
#endif

/// The template argument a compiler signature spells after `prefix`. A signature in a format this
/// file does not know stops the build: names are never silently empty.
[[nodiscard]] consteval auto templateArgument(std::string_view signature, std::string_view prefix) noexcept
    -> std::string_view {
    const auto prefixAt = signature.find(prefix);
    const auto first = prefixAt == std::string_view::npos ? prefixAt : prefixAt + prefix.size();
#if defined(__clang__)
    const auto last = signature.rfind(']');
#elif defined(__GNUC__)
    const auto last = first == std::string_view::npos ? first : argumentEnd(signature, first);
#else
    const auto last = signature.rfind(">(void)");
#endif
    const bool isRecognized =
        first != std::string_view::npos && last != std::string_view::npos && last >= first;
    expects(isRecognized, "nx.core:introspection: this compiler's signature format is not recognized");
    return isRecognized ? signature.substr(first, last - first) : std::string_view{};
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

/// Characters of a module name ("nx.core", a partition "nx.core:text"); "::" ends it.
[[nodiscard]] constexpr auto moduleNameEnd(std::string_view name, std::size_t first) noexcept -> std::size_t {
    std::size_t index = first;
    while (index < name.size() && !name.substr(index).starts_with(kScopeSeparator)) {
        const char character = name[index];
        const bool isLetter =
            (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z');
        const bool isDigit = character >= '0' && character <= '9';
        if (!isLetter && !isDigit && character != '_' && character != '.' && character != ':') {
            break;
        }
        ++index;
    }
    return index;
}

/// `name` without the module GCC writes after every type attached to a named one
/// ("nx::BspError@nx.assets"), so names read the same on every compiler.
[[nodiscard]] constexpr auto withoutModuleAttachment(std::string_view name) -> std::string {
    std::string plain;
    std::size_t index = 0;
    while (index < name.size()) {
        if (name[index] == '@') {
            index = moduleNameEnd(name, index + 1);
        } else {
            plain += name[index++];
        }
    }
    return plain;
}

/// The name of `Type`, stored for the program's lifetime.
template <class Type>
inline constexpr auto kTypeName = [] {
    constexpr std::string_view kSpelled =
        withoutClassKey(templateArgument(typeSignature<Type>(), kTypeArgumentPrefix));
    std::array<char, withoutModuleAttachment(kSpelled).size()> name{};
    std::ranges::copy(withoutModuleAttachment(kSpelled), name.begin());
    return name;
}();

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
    return std::string_view{detail::kTypeName<Type>.data(), detail::kTypeName<Type>.size()};
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

namespace detail {

/// Every index below Count fits in the underlying type, since Count itself does.
template <CountedEnum Enum>
[[nodiscard]] consteval auto enumeratorAt(std::size_t index) noexcept -> Enum {
    return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(index));
}

template <CountedEnum Enum>
[[nodiscard]] consteval auto namedEnumeratorCount() noexcept -> std::size_t {
    std::size_t named = 0;
    for (std::size_t index = 0; index < enumCount<Enum>(); ++index) {
        if (!enumName(enumeratorAt<Enum>(index)).empty()) {
            ++named;
        }
    }
    return named;
}

} // namespace detail

/// The named enumerators of `Enum` before Count, in value order: the values text can name. Values
/// left out between enumerators have no name and are not listed.
template <CountedEnum Enum>
[[nodiscard]] consteval auto enumerators() noexcept
    -> std::array<Enum, detail::namedEnumeratorCount<Enum>()> {
    std::array<Enum, detail::namedEnumeratorCount<Enum>()> values{};
    std::size_t next = 0;
    for (std::size_t index = 0; index < enumCount<Enum>(); ++index) {
        if (const Enum value = detail::enumeratorAt<Enum>(index); !enumName(value).empty()) {
            values[next++] = value;
        }
    }
    return values;
}

} // namespace nx
