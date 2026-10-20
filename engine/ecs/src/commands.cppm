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

/// What applying a buffer did, counted in recorded commands: a spawn counts once, with its
/// components. Commands that target an entity destroyed in the meantime are skipped (a grenade's
/// owner may die before the grenade spawns) and counted here.
struct CommandStats {
    std::size_t applied = 0;
    std::size_t skipped = 0;
};

/// Records spawn, destroy, add and remove for later. One buffer per system run; a buffer is used
/// from one thread at a time.
///
/// Spawns recorded one after another with the same component types form one command, which
/// apply() turns into one batch of rows in their archetype; destroys recorded one after another
/// form one command too.
class Commands {
public:
    inline explicit Commands(const World& world) noexcept : m_world{&world} {}

    Commands(const Commands&) = delete;
    auto operator=(const Commands&) -> Commands& = delete;
    Commands(Commands&& other) noexcept;
    auto operator=(Commands&& other) noexcept -> Commands&;
    /// Destroys the components of commands never applied.
    ~Commands();

    /// An entity without components; add them with add(pending, ...).
    [[nodiscard]] inline auto spawn() -> PendingEntity { return recordSpawn({}); }

    /// An entity with these components, each type at most once; apply() creates it straight in
    /// its final archetype.
    template <class... Types>
        requires(sizeof...(Types) > 0) && (Component<std::remove_cvref_t<Types>> && ...)
    auto spawn(Types&&... components) -> PendingEntity {
        static_assert(detail::areDistinct<std::remove_cvref_t<Types>...>(),
                      "spawn: each component type at most once");
        std::array<RecordedComponent, sizeof...(Types)> recorded{};
        std::size_t count = 0;
        const auto keep = [this, &recorded, &count]<class Value>(Value&& value) {
            if (const auto type = m_world->requireComponentId<std::remove_cvref_t<Value>>()) {
                recorded[count++] =
                    RecordedComponent{.type = *type, .source = store(std::forward<Value>(value))};
            }
        };
        (keep(std::forward<Types>(components)), ...);
        return recordSpawn(std::span{recorded}.first(count));
    }

    void destroy(Entity entity);

    template <Component Type>
    void add(Entity entity, Type component) {
        recordAdd(Target{entity}, std::move(component));
    }

    template <Component Type>
    void add(PendingEntity entity, Type component) {
        recordAdd(pendingTarget(entity), std::move(component));
    }

    template <Component Type>
    void remove(Entity entity) {
        recordRemove(Target{entity}, m_world->requireComponentId<Type>());
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
    /// What a command acts on: an existing entity, or one spawned earlier in this buffer.
    using Target = std::variant<Entity, PendingEntity>;

    /// `count` entities with the same component types: the pending entities from `first` on, in
    /// order. Their types, in recording order, are m_spawnTypes[firstType, +typeCount); their
    /// components, entity after entity and in that order, are m_spawnSources[firstSource,
    /// +count * typeCount).
    struct SpawnCommand {
        PendingEntity first;
        std::uint32_t count = 0;
        std::uint32_t firstType = 0;
        std::uint32_t typeCount = 0;
        std::uint32_t firstSource = 0;
    };

    /// Destroys the entities m_destroyed[first, +count), recorded one after another.
    struct DestroyCommand {
        std::uint32_t first = 0;
        std::uint32_t count = 0;
    };

    struct AddCommand {
        Target target;
        ComponentTypeId type{};
        void* payload = nullptr; // in the pages; nullptr for tags and once moved into the world
    };

    struct RemoveCommand {
        Target target;
        ComponentTypeId type{};
    };

    using Command = std::variant<SpawnCommand, DestroyCommand, AddCommand, RemoveCommand>;

    /// A component of the spawn being recorded.
    struct RecordedComponent {
        ComponentTypeId type{};
        void* source = nullptr; // in the pages; nullptr for tags
    };

    /// Pages never move, so recorded components stay where they were constructed.
    struct alignas(kMaxComponentAlignment) Page {
        std::array<std::byte, kCommandPageBytes> bytes;
    };

    /// Records a spawn of `components`, extending the previous spawn command when it has the same
    /// types in the same order.
    [[nodiscard]] auto recordSpawn(std::span<const RecordedComponent> components) -> PendingEntity;
    void recordRemove(Target target, std::optional<ComponentTypeId> type);
    [[nodiscard]] auto pendingTarget(PendingEntity entity) const noexcept -> Target;

    /// Room for `size` bytes aligned to `alignment` in the pages: inline for the common case of a
    /// component that fits in the page being filled.
    [[nodiscard]] inline auto allocate(std::size_t size, std::size_t alignment) -> void* {
        const std::size_t offset = alignUp(m_pageUsed, alignment);
        if (m_pageIndex < m_pages.size() && offset + size <= kCommandPageBytes) {
            m_pageUsed = offset + size;
            return m_pages[m_pageIndex]->bytes.data() + offset;
        }
        return allocateBeyondPage(size, alignment);
    }
    /// allocate() when the page being filled has no room: a new page, or a block of its own for a
    /// component larger than a page.
    [[nodiscard]] auto allocateBeyondPage(std::size_t size, std::size_t alignment) -> void*;

    [[nodiscard]] auto typesOf(const SpawnCommand& command) const noexcept
        -> std::span<const ComponentTypeId>;
    [[nodiscard]] auto sourcesOf(const SpawnCommand& command) noexcept -> std::span<void*>;
    [[nodiscard]] auto entitiesOf(const DestroyCommand& command) const noexcept -> std::span<const Entity>;
    [[nodiscard]] auto resolveTarget(const Target& target) const noexcept -> Entity;

    void applyCommand(World& world, const SpawnCommand& command, CommandStats& stats);
    void applyCommand(World& world, const DestroyCommand& command, CommandStats& stats) const;
    void applyCommand(World& world, AddCommand& command, CommandStats& stats) const;
    void applyCommand(World& world, const RemoveCommand& command, CommandStats& stats) const;

    /// Exchanges every member: what both move operations are made of.
    void swap(Commands& other) noexcept;
    void destroyComponent(ComponentTypeId type, void* component) const noexcept;
    /// Destroys the components of the commands not applied: what a buffer dropped unapplied owns.
    void destroyPayloads() noexcept;
    /// Empties the buffer after apply(), which moved or destroyed every component.
    void clear() noexcept;

    /// The last recorded command when it is a `Kind`, whose run a new command may extend.
    template <class Kind>
    [[nodiscard]] auto lastCommandAs() noexcept -> Kind* {
        return m_commands.empty() ? nullptr : std::get_if<Kind>(&m_commands.back());
    }

    /// Moves `value` into the pages; nullptr for tags, which have no data.
    template <class Value>
    [[nodiscard]] auto store(Value&& value) -> void* {
        using Type = std::remove_cvref_t<Value>;
        if constexpr (TagComponent<Type>) {
            return nullptr;
        } else {
            return std::construct_at(static_cast<Type*>(allocate(sizeof(Type), alignof(Type))),
                                     std::forward<Value>(value));
        }
    }

    template <Component Type>
    void recordAdd(Target target, Type&& component) {
        if (const auto type = m_world->requireComponentId<Type>()) {
            m_commands.emplace_back(
                AddCommand{.target = target, .type = *type, .payload = store(std::move(component))});
        }
    }

    const World* m_world;
    std::vector<Command> m_commands;
    std::vector<ComponentTypeId> m_spawnTypes;  // of the spawn commands
    std::vector<void*> m_spawnSources;          // of the spawn commands; nullptr once moved into the world
    std::vector<Entity> m_destroyed;            // of the destroy commands
    std::vector<std::unique_ptr<Page>> m_pages; // kept across apply()
    std::vector<std::unique_ptr<Page[]>> m_largePayloads; // components larger than a page
    std::size_t m_pageIndex = 0;                          // page being filled
    std::size_t m_pageUsed = 0;                           // bytes used in it
    std::uint32_t m_pendingCount = 0;
    std::vector<Entity> m_resolved; // by pending index, after apply()
};

} // namespace nx::ecs
