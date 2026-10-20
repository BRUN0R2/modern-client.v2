// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The facade the engine reflects through: names, annotations and fields of described types.
export module nx.meta:reflection;

import std;
import :description;
import :native;

export namespace nx::meta {

/// The declared, namespace-qualified name of `Type` ("cs::Transform").
template <Described Type>
[[nodiscard]] consteval auto qualifiedName() noexcept -> std::string_view {
    static_assert(native::verify<Type>());
    return Description<Type>::kName;
}

/// Number of fields of `Type`.
template <Described Type>
[[nodiscard]] consteval auto fieldCount() noexcept -> std::size_t {
    static_assert(native::verify<Type>());
    return std::tuple_size_v<std::remove_cvref_t<decltype(Description<Type>::kFields)>>;
}

/// The first annotation of type `Wanted` on `Type`.
template <Annotation Wanted, Described Type>
[[nodiscard]] consteval auto typeAnnotation() noexcept -> std::optional<Wanted> {
    static_assert(native::verify<Type>());
    return detail::firstOfType<Wanted>(detail::typeAnnotations<Type>());
}

/// Field `Index` of `Type`, as a type: forEachField hands one to its visitor.
template <Described Type, std::size_t Index>
class Field {
    static constexpr const auto& kDesc = std::get<Index>(Description<Type>::kFields);
    static constexpr auto kMember = detail::FieldDescTraits<std::remove_cvref_t<decltype(kDesc)>>::kMember;
    using Traits = detail::MemberTraits<std::remove_cvref_t<decltype(kMember)>>;

    static_assert(std::same_as<typename Traits::OwnerType, Type>,
                  "nx::meta: a field of the description names a member of another type");
    static_assert(native::verify<Type>());

public:
    using ValueType = typename Traits::ValueType;

    static constexpr std::size_t kIndex = Index;
    static constexpr std::string_view kName = kDesc.name;

    [[nodiscard]] static constexpr auto get(Type& object) noexcept -> ValueType& { return object.*kMember; }
    [[nodiscard]] static constexpr auto get(const Type& object) noexcept -> const ValueType& {
        return object.*kMember;
    }

    /// The first annotation of type `Wanted` on this field.
    template <Annotation Wanted>
    [[nodiscard]] static consteval auto annotation() noexcept -> std::optional<Wanted> {
        return detail::firstOfType<Wanted>(kDesc.annotations);
    }
};

/// Calls `visitor` with a Field<Type, Index> for every field of `Type`, in declaration order:
///
///     nx::meta::forEachField<cs::Ammo>([&]<class Field>(Field) { write(Field::kName, Field::get(ammo)); });
template <Described Type, class Visitor>
constexpr void forEachField(Visitor&& visitor) {
    [&visitor]<std::size_t... Index>(std::index_sequence<Index...> /*fields*/) {
        (std::invoke(visitor, Field<Type, Index>{}), ...);
    }(std::make_index_sequence<fieldCount<Type>()>{});
}

} // namespace nx::meta
