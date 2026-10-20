// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Explicit descriptions of reflected types: the metadata source of nx.meta on every compiler.
export module nx.meta:description;

import std;

export namespace nx::meta {

/// A value attached to a type or a field (`nx::net::Replicated{}`, `nx::data::Range{...}`): a
/// copyable class compared with `==`, as the C++26 annotations (P3394) it stands for.
template <class Type>
concept Annotation =
    std::is_class_v<Type> && std::is_trivially_copyable_v<Type> && std::equality_comparable<Type>;

/// One field of a description: the data member it names, its name and its annotations.
template <auto Member, Annotation... Annotations>
    requires std::is_member_object_pointer_v<decltype(Member)>
struct FieldDesc {
    std::string_view name;
    std::tuple<Annotations...> annotations;
};

/// `nx::meta::field<&Ammo::clip>("clip", nx::data::Range{...})`.
template <auto Member, Annotation... Annotations>
    requires std::is_member_object_pointer_v<decltype(Member)>
[[nodiscard]] consteval auto field(std::string_view name, Annotations... annotations)
    -> FieldDesc<Member, Annotations...> {
    return FieldDesc<Member, Annotations...>{.name = name, .annotations = std::tuple{annotations...}};
}

/// The description of `Type`, specialized next to the type's definition. Fields are listed in
/// declaration order; kAnnotations is optional:
///
///     template <>
///     struct nx::meta::Description<cs::Ammo> {
///         static constexpr std::string_view kName = "cs::Ammo";
///         static constexpr std::tuple kAnnotations{nx::net::Replicated{}};
///         static constexpr std::tuple kFields{
///             nx::meta::field<&cs::Ammo::clip>("clip"),
///             nx::meta::field<&cs::Ammo::reserve>("reserve"),
///         };
///     };
///
/// kName is the namespace-qualified name of the type as declared ("cs::Ammo"): stable identities
/// (nx::StableId) derive from it, never from a compiler-generated name.
template <class Type>
struct Description;

namespace detail {

template <class Entry>
struct FieldDescTraits {
    static constexpr bool kIsField = false;
};

template <auto Member, class... Annotations>
struct FieldDescTraits<FieldDesc<Member, Annotations...>> {
    static constexpr bool kIsField = true;
    static constexpr auto kMember = Member;
};

template <class Tuple>
inline constexpr bool kIsFieldTuple = false;

template <class... Entries>
inline constexpr bool kIsFieldTuple<std::tuple<Entries...>> = (FieldDescTraits<Entries>::kIsField && ...);

template <class Tuple>
inline constexpr bool kIsAnnotationTuple = false;

template <class... Values>
inline constexpr bool kIsAnnotationTuple<std::tuple<Values...>> = (Annotation<Values> && ...);

template <class MemberPointer>
struct MemberTraits;

template <class Value, class Owner>
struct MemberTraits<Value Owner::*> {
    using OwnerType = Owner;
    using ValueType = Value;
};

} // namespace detail

/// A class with a Description specialization whose name and fields are well-formed.
template <class Type>
concept Described = std::is_class_v<Type> && requires {
    { Description<Type>::kName } -> std::convertible_to<std::string_view>;
    requires detail::kIsFieldTuple<std::remove_cvref_t<decltype(Description<Type>::kFields)>>;
};

namespace detail {

/// The type's annotations; a description without kAnnotations has none.
template <Described Type>
[[nodiscard]] constexpr auto typeAnnotations() noexcept {
    if constexpr (requires { Description<Type>::kAnnotations; }) {
        static_assert(kIsAnnotationTuple<std::remove_cvref_t<decltype(Description<Type>::kAnnotations)>>,
                      "nx::meta: kAnnotations must be a std::tuple of annotations");
        return Description<Type>::kAnnotations;
    } else {
        return std::tuple<>{};
    }
}

/// The first element of `values` whose type is `Wanted`.
template <class Wanted, class... Values>
[[nodiscard]] constexpr auto firstOfType(const std::tuple<Values...>& values) noexcept
    -> std::optional<Wanted> {
    std::optional<Wanted> found;
    std::apply(
        [&found](const auto&... value) {
            const auto consider = [&found](const auto& candidate) {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(candidate)>, Wanted>) {
                    if (!found) {
                        found = candidate;
                    }
                }
            };
            (consider(value), ...);
        },
        values);
    return found;
}

} // namespace detail

} // namespace nx::meta
