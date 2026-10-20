// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// C++26 reflection (P2996) is detected by the compiler's feature-test macro; <meta> is included
// here because the std module does not have to export it.
#if defined(__cpp_impl_reflection)
#include <meta>
#endif

/// The native path of nx.meta: where the compiler implements C++26 reflection (P2996, GCC 16 with
/// -freflection), every Description is compared with the type it describes at compile time.
export module nx.meta:native;

import std;
import :description;

export namespace nx::meta::native {

#if defined(__cpp_impl_reflection)

inline constexpr bool kAvailable = true;

namespace detail {

/// "cs::Transform": the identifiers of the enclosing namespaces and classes, outermost first.
/// Unnamed scopes add nothing.
[[nodiscard]] consteval auto qualifiedNameOf(std::meta::info entity) -> std::string {
    std::string name{std::meta::identifier_of(entity)};
    for (auto scope = std::meta::parent_of(entity); scope != ^^::; scope = std::meta::parent_of(scope)) {
        if (std::meta::has_identifier(scope)) {
            name.insert(0, std::string{std::meta::identifier_of(scope)} + "::");
        }
    }
    return name;
}

template <class Type>
inline constexpr auto kMembers = std::define_static_array(
    std::meta::nonstatic_data_members_of(^^Type, std::meta::access_context::unchecked()));

template <class Value>
[[nodiscard]] consteval auto hasAnnotation(std::meta::info entity, const Value& value) -> bool {
    for (const auto annotation : std::meta::annotations_of_with_type(entity, ^^Value)) {
        if (std::meta::extract<Value>(annotation) == value) {
            return true;
        }
    }
    return false;
}

/// Whether `entity` carries exactly the `declared` annotations. An entity without annotations
/// matches any declaration: annotations live in the descriptions until every supported compiler
/// accepts the `[[=...]]` syntax (docs/02-coding-standards.md, section 1.2).
template <class... Values>
[[nodiscard]] consteval auto matchesAnnotations(std::meta::info entity, const std::tuple<Values...>& declared)
    -> bool {
    const std::size_t count = std::meta::annotations_of(entity).size();
    if (count == 0) {
        return true;
    }
    return count == sizeof...(Values) &&
           std::apply([entity](const auto&... value) { return (hasAnnotation(entity, value) && ...); },
                      declared);
}

template <Described Type, std::size_t Index>
[[nodiscard]] consteval auto fieldMismatch() -> std::string_view {
    constexpr const auto& declared = std::get<Index>(Description<Type>::kFields);
    constexpr auto declaredMember =
        meta::detail::FieldDescTraits<std::remove_cvref_t<decltype(declared)>>::kMember;
    constexpr auto member = kMembers<Type>[Index];
    const std::string actual{std::meta::identifier_of(member)};
    const std::string name{declared.name};
    if (actual != name) {
        return std::define_static_string("field \"" + name + "\" is \"" + actual +
                                         "\" in the type; fields are listed in declaration order");
    }
    if constexpr (!std::same_as<decltype(&[:member:]), std::remove_const_t<decltype(declaredMember)>>) {
        return std::define_static_string("field \"" + name + "\" names a member of another type");
    } else if (&[:member:] != declaredMember) {
        return std::define_static_string("field \"" + name + "\" names another member of the type");
    }
    if (!matchesAnnotations(member, declared.annotations)) {
        return std::define_static_string("field \"" + name + "\" has other annotations in the type");
    }
    return {};
}

template <Described Type, std::size_t... Index>
[[nodiscard]] consteval auto firstFieldMismatch(std::index_sequence<Index...> /*fields*/)
    -> std::string_view {
    std::string_view found;
    const auto keepFirst = [&found](std::string_view problem) {
        if (found.empty()) {
            found = problem;
        }
    };
    (keepFirst(fieldMismatch<Type, Index>()), ...);
    return found;
}

} // namespace detail

/// What makes Description<Type> differ from `Type` itself, or an empty view when they match:
/// another qualified name, a missing, extra, renamed or reordered field, or annotations of the type
/// or of a field that differ from the declared ones.
template <Described Type>
[[nodiscard]] consteval auto mismatch() -> std::string_view {
    const std::string actual = detail::qualifiedNameOf(^^Type);
    const std::string declared{Description<Type>::kName};
    if (actual != declared) {
        return std::define_static_string("the description is named \"" + declared + "\", the type \"" +
                                         actual + "\"");
    }
    if (!detail::matchesAnnotations(^^Type, meta::detail::typeAnnotations<Type>())) {
        return std::define_static_string("\"" + declared + "\" has other annotations in the type");
    }
    constexpr std::size_t kDeclaredFields =
        std::tuple_size_v<std::remove_cvref_t<decltype(Description<Type>::kFields)>>;
    if constexpr (kDeclaredFields != detail::kMembers<Type>.size()) {
        return std::define_static_string("the description of \"" + declared +
                                         "\" lists a different number of fields than the type declares");
    } else {
        return detail::firstFieldMismatch<Type>(std::make_index_sequence<kDeclaredFields>{});
    }
}

/// The compile-time check behind every facade call: a description that does not match its type
/// stops the build, with the mismatch as the message.
template <Described Type>
[[nodiscard]] consteval auto verify() -> bool {
    static_assert(mismatch<Type>().empty(), mismatch<Type>());
    return true;
}

#else

inline constexpr bool kAvailable = false;

/// Without C++26 reflection, descriptions are taken as written.
template <Described Type>
[[nodiscard]] consteval auto verify() -> bool {
    return true;
}

#endif

} // namespace nx::meta::native
