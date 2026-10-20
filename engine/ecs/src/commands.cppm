// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Command buffers: structural changes recorded during system execution and applied at sync
/// points (docs/03-ecs.md, section 8).
export module nx.ecs:commands;

import std;
import nx.core;
import :archetype;
import :component;
import :entity;
import :world;

export namespace nx::ecs {

/// An entity a command buffer will create. The real Entity is allocated when the buffer is
/// applied, in recording order, so the ids do not depend on which thread ran the system.
class PendingEntity {
public:
    [[nodiscard]] constexpr auto index() const noexcept -> std::uint32_t { return m_index; }

private:
    friend class Commands;
    constexpr explicit PendingEntity(std::uint32_t index) noexcept : m_index{index} {}

    std::uint32_t m_index;
};

/// Size of the pages a command buffer stores recorded components in.
inline constexpr std::size_t kCommandPageBytes = kChunkBytes;

/// What applying a buffer did. Commands that target an entity destroyed in the meantime are
/// skipped (a grenade's owner may die before the grenade spawns) and counted here.
struct CommandStats {
    std::size_t applied = 0;
    std::size_t skipped = 0;
};

/// Records spawn, destroy, add and remove for later. One buffer per system run; a buffer is used
/// from one thread at a time.
class Commands {
public:
    inline explicit Commands(const World& world) noexcept : m_world{&world} {}

    Commands(const Commands&) = delete;
    auto operator=(const Commands&) -> Commands& = delete;
    Commands(Commands&& other) noexcept;
    auto operator=(Commands&& other) noexcept -> Commands&;
    /// Destroys the components of commands never applied.
    ~Commands();

    [[nodiscard]] auto spawn() -> PendingEntity;

    template <class... Types>
        requires(sizeof...(Types) > 0) && (Component<std::remove_cvref_t<Types>> && ...)
    auto spawn(Types&&... components) -> PendingEntity {
        const PendingEntity entity = spawn();
        (add(entity, std::remove_cvref_t<Types>{std::forward<Types>(components)}), ...);
        return entity;
    }

    void destroy(Entity entity);

    template <Component Type>
    void add(Entity entity, Type component) {
        record(Target{.entity = entity}, std::move(component));
    }

    template <Component Type>
    void add(PendingEntity entity, Type component) {
        record(pendingTarget(entity), std::move(component));
    }

    template <Component Type>
    void remove(Entity entity) {
        recordRemove(Target{.entity = entity}, m_world->requireComponentId<Type>());
    }

    template <Component Type>
    void remove(PendingEntity entity) {
        recordRemove(pendingTarget(entity), m_world->requireComponentId<Type>());
    }

    [[nodiscard]] inline auto isEmpty() const noexcept -> bool { return m_commands.empty(); }

    /// Applies the commands in recording order and empties the buffer. Must run outside query
    /// iteration (at a sync point).
    auto apply(World& world) -> CommandStats;

    /// The entity a pending entity became in the last apply().
    [[nodiscard]] auto resolve(PendingEntity entity) const noexcept -> std::optional<Entity>;

private:
    enum class Kind : std::uint8_t {
        Spawn,
        Destroy,
        Add,
        Remove,
    };

    /// An existing entity, or the pending entity with index `pending` when `isPending` is set.
    struct Target {
        Entity entity{};
        std::uint32_t pending = 0;
        bool isPending = false;
    };

    struct Command {
        Kind kind = Kind::Spawn;
        Target target;
        ComponentTypeId type{};
        void* payload = nullptr; // Add: the component, constructed in m_storage
    };

    /// Pages never move, so recorded components stay where they were constructed.
    struct alignas(kMaxComponentAlignment) Page {
        std::array<std::byte, kCommandPageBytes> bytes;
    };

    [[nodiscard]] auto pendingTarget(PendingEntity entity) const noexcept -> Target;
    [[nodiscard]] auto allocate(std::size_t size, std::size_t alignment) -> void*;
    void recordRemove(Target target, std::optional<ComponentTypeId> type);
    [[nodiscard]] auto resolveTarget(const Target& target) const noexcept -> Entity;
    void destroyPayloads() noexcept;
    void clear() noexcept;

    template <Component Type>
    void record(Target target, Type&& component) {
        const auto type = m_world->requireComponentId<Type>();
        if (!type) {
            return;
        }
        void* payload = nullptr;
        if constexpr (!TagComponent<Type>) {
            payload = allocate(sizeof(Type), alignof(Type));
            std::construct_at(static_cast<Type*>(payload), std::move(component));
        }
        m_commands.push_back(Command{.kind = Kind::Add, .target = target, .type = *type, .payload = payload});
    }

    const World* m_world;
    std::vector<Command> m_commands;
    std::vector<std::unique_ptr<Page>> m_pages;           // kept across apply()
    std::vector<std::unique_ptr<Page[]>> m_largePayloads; // components larger than a page
    std::size_t m_pageIndex = 0;                          // page being filled
    std::size_t m_pageUsed = 0;                           // bytes used in it
    std::uint32_t m_pendingCount = 0;
    std::vector<Entity> m_resolved; // by pending index, after apply()
};

} // namespace nx::ecs
