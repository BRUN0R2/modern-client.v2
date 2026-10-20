// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Component types and their registry (docs/03-ecs.md, section 4).
///
/// Until the reflection facade (nx.meta) exists, components are registered explicitly with their
/// declared name, the documented fallback path (docs/02-coding-standards.md, section 1.2). The
/// registry works from a ComponentInfo, so components defined at run time (Rook) use the same path.
export module nx.ecs:component;

import std;
import nx.core;

export namespace nx::ecs {

/// Dense index of a component type within one world, used in archetypes and queries.
enum class ComponentTypeId : std::uint32_t {};

/// A component is plain data: an object type that moves and dies without failing.
template <class Type>
concept Component =
    std::is_object_v<Type> && !std::is_const_v<Type> && !std::is_volatile_v<Type> && !std::is_array_v<Type> &&
    std::is_nothrow_move_constructible_v<Type> && std::is_nothrow_destructible_v<Type>;

/// A component without data (`Player`, `OnGround`): it only places the entity in an archetype.
template <class Type>
concept TagComponent = Component<Type> && std::is_empty_v<Type>;

/// Strictest alignment a component may need: the alignment of a chunk.
inline constexpr std::size_t kMaxComponentAlignment = kCacheLineSize;

/// What the storage needs to know about a component type, defined in C++ or at run time.
struct ComponentInfo {
    /// Declared, qualified name ("cs::Transform"); never a compiler-generated name, which differs
    /// between compilers.
    std::string name;
    /// Hash of `name`: the identity used by the network, saves, replays and mods.
    StableId stableId;
    /// 0 for tags, which have no storage.
    std::size_t size = 0;
    std::size_t alignment = 1;
    /// Moves the object at `source` into the uninitialized `destination` and destroys the source.
    /// nullptr when a byte copy does that (trivially copyable types).
    void (*relocate)(void* destination, void* source) noexcept = nullptr;
    /// nullptr when the type is trivially destructible.
    void (*destroy)(void* object) noexcept = nullptr;

    [[nodiscard]] inline auto isTag() const noexcept -> bool { return size == 0; }
};

/// ComponentInfo for the C++ type `Type`, registered under the declared `name`.
template <Component Type>
[[nodiscard]] auto describeComponent(std::string_view name) -> ComponentInfo {
    ComponentInfo info{
        .name = std::string{name},
        .stableId = StableId::fromName(name),
        .size = TagComponent<Type> ? 0 : sizeof(Type),
        .alignment = alignof(Type),
    };
    if constexpr (!std::is_trivially_copyable_v<Type>) {
        info.relocate = [](void* destination, void* source) noexcept {
            Type* const from = std::launder(static_cast<Type*>(source));
            std::construct_at(static_cast<Type*>(destination), std::move(*from));
            std::destroy_at(from);
        };
    }
    if constexpr (!std::is_trivially_destructible_v<Type>) {
        info.destroy = [](void* object) noexcept {
            std::destroy_at(std::launder(static_cast<Type*>(object)));
        };
    }
    return info;
}

enum class ComponentError : std::uint8_t {
    TypeAlreadyRegistered,
    NameAlreadyRegistered,
    StableIdCollision, // two different names hash to the same StableId
    InvalidAlignment,  // not a power of two, or beyond kMaxComponentAlignment
    InvalidName,
};

namespace detail {

/// One object per type in the whole program: its address identifies the C++ type without RTTI.
template <class Type>
inline constexpr char kTypeKey = 0;

template <class Type>
[[nodiscard]] constexpr auto typeKeyOf() noexcept -> const void* {
    return &kTypeKey<Type>;
}

/// Hash of a type key, for the maps every typed operation looks up: one multiplication by 2^64/phi
/// (Fibonacci hashing), folded so the low bits mix as well as the high ones. std::hash of a pointer
/// is not built for this: MSVC runs FNV-1a over its bytes, several times slower.
struct TypeKeyHash {
    [[nodiscard]] inline auto operator()(const void* typeKey) const noexcept -> std::size_t {
        constexpr std::uint64_t kFibonacciMultiplier = 0x9E37'79B9'7F4A'7C15ULL;
        constexpr unsigned kFoldShift = 32;
        const std::uint64_t mixed =
            std::uint64_t{std::bit_cast<std::uintptr_t>(typeKey)} * kFibonacciMultiplier;
        return static_cast<std::size_t>(mixed ^ (mixed >> kFoldShift));
    }
};

} // namespace detail

/// Component types known to one world, in registration order.
class ComponentRegistry {
public:
    /// Registers a C++ component type under its declared name.
    template <Component Type>
    [[nodiscard]] auto add(std::string_view name) -> Result<ComponentTypeId, ComponentError> {
        return add(describeComponent<Type>(name), detail::typeKeyOf<Type>());
    }

    /// Registers a component described at run time. `typeKey` identifies the C++ type, if any.
    [[nodiscard]] auto add(ComponentInfo info, const void* typeKey = nullptr)
        -> Result<ComponentTypeId, ComponentError>;

    template <Component Type>
    [[nodiscard]] auto find() const -> std::optional<ComponentTypeId> {
        return findByTypeKey(detail::typeKeyOf<Type>());
    }

    [[nodiscard]] auto findByTypeKey(const void* typeKey) const -> std::optional<ComponentTypeId>;
    [[nodiscard]] auto findByStableId(StableId stableId) const -> std::optional<ComponentTypeId>;

    /// `id` must come from this registry.
    [[nodiscard]] auto info(ComponentTypeId id) const noexcept -> const ComponentInfo&;
    [[nodiscard]] inline auto count() const noexcept -> std::size_t { return m_infos.size(); }

private:
    std::deque<ComponentInfo> m_infos; // a deque keeps references stable as it grows
    std::unordered_map<const void*, ComponentTypeId, detail::TypeKeyHash> m_byTypeKey;
    std::unordered_map<StableId, ComponentTypeId> m_byStableId;
};

} // namespace nx::ecs
