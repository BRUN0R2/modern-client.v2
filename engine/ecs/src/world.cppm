// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The world: entities, their components and the archetypes that store them (docs/03-ecs.md).
export module nx.ecs:world;

import std;
import nx.core;
import :archetype;
import :component;
import :entity;
import :event;

export namespace nx::ecs {

/// Numbers for `ecs_stats`.
struct WorldStats {
    std::size_t entityCount = 0;
    std::size_t archetypeCount = 0;
    std::size_t chunkCount = 0;
    std::size_t pooledChunkCount = 0; // emptied chunks kept for reuse
    std::size_t retiredSlotCount = 0; // entity slots whose generations ran out
};

namespace detail {

template <class... Types>
[[nodiscard]] consteval auto areDistinct() noexcept -> bool {
    constexpr std::array keys{typeKeyOf<Types>()...};
    for (std::size_t first = 0; first < keys.size(); ++first) {
        for (std::size_t second = first + 1; second < keys.size(); ++second) {
            if (keys[first] == keys[second]) {
                return false;
            }
        }
    }
    return true;
}

} // namespace detail

/// A resource is a single value of its type per world (`GameRules`, `TickTime`, `MapInfo`).
template <class Type>
concept Resource = std::is_object_v<Type> && !std::is_const_v<Type> && std::is_nothrow_destructible_v<Type> &&
                   std::move_constructible<Type>;

/// Owns entities and components.
///
/// Threads: registration and structural changes (spawn, destroy, add, remove) happen on one thread,
/// outside query iteration; a structural change during an iteration is a contract violation.
/// Queries that do not conflict may iterate from several threads at once (the scheduler's rule).
class World {
public:
    World();
    World(const World&) = delete;
    auto operator=(const World&) -> World& = delete;
    World(World&&) = delete;
    auto operator=(World&&) -> World& = delete;
    ~World();

    // --- Component types ---------------------------------------------------------------------

    /// Registers the C++ component `Type` under its declared, qualified name ("cs::Transform").
    template <Component Type>
    [[nodiscard]] auto registerComponent(std::string_view name) -> Result<ComponentTypeId, ComponentError> {
        return m_components.add<Type>(name);
    }

    /// Registers a component described at run time (Rook components).
    [[nodiscard]] inline auto registerComponent(ComponentInfo info)
        -> Result<ComponentTypeId, ComponentError> {
        return m_components.add(std::move(info));
    }

    [[nodiscard]] inline auto components() const noexcept -> const ComponentRegistry& { return m_components; }

    /// Id of a registered type. An unregistered type is a contract violation and yields nullopt.
    template <Component Type>
    [[nodiscard]] auto requireComponentId() const -> std::optional<ComponentTypeId> {
        const auto id = m_components.find<Type>();
        expects(id.has_value(), "World: the component type is not registered in this world");
        return id;
    }

    // --- Entities ----------------------------------------------------------------------------

    /// An entity without components.
    [[nodiscard]] auto spawn() -> Entity;

    /// An entity with these components, placed straight in its final archetype.
    template <class... Types>
        requires(sizeof...(Types) > 0) && (Component<std::remove_cvref_t<Types>> && ...)
    auto spawn(Types&&... components) -> Entity {
        static_assert(detail::areDistinct<std::remove_cvref_t<Types>...>(),
                      "spawn: each component type at most once");
        const std::array ids{requireComponentId<std::remove_cvref_t<Types>>()...};
        std::vector<ComponentTypeId> types;
        for (const auto& id : ids) {
            if (id.has_value()) {
                types.push_back(*id);
            }
        }
        const auto [entity, location] = spawnUninitialized(std::move(types));
        std::size_t index = 0;
        (constructComponent(location, ids[index++], std::forward<Types>(components)), ...);
        return entity;
    }

    /// `entity` must be alive. Destroys its components and frees its slot.
    void destroy(Entity entity);

    [[nodiscard]] inline auto isAlive(Entity entity) const noexcept -> bool {
        return m_entities.isAlive(entity);
    }
    [[nodiscard]] inline auto entityCount() const noexcept -> std::size_t { return m_entities.aliveCount(); }

    // --- Components of one entity -----------------------------------------------------------

    /// Adds `component`, or replaces the one the entity already has. `entity` must be alive.
    template <Component Type>
    void add(Entity entity, Type component) {
        const auto id = requireComponentId<Type>();
        const auto location = requireLocation(entity);
        if (!id || !location) {
            return;
        }
        if (archetype(location->archetype).contains(*id)) {
            if constexpr (!TagComponent<Type>) {
                Type* const existing = componentPointer<Type>(*location, *id);
                std::destroy_at(existing);
                std::construct_at(existing, std::move(component));
                markChanged(*location, *id);
            }
            return;
        }
        const EntityLocation moved = moveEntity(entity, *location, archetypeWith(location->archetype, *id));
        constructComponent(moved, id, std::move(component));
    }

    /// Removes the component if the entity has it. `entity` must be alive.
    template <Component Type>
    void remove(Entity entity) {
        const auto id = requireComponentId<Type>();
        const auto location = requireLocation(entity);
        if (id && location && archetype(location->archetype).contains(*id)) {
            std::ignore = moveEntity(entity, *location, archetypeWithout(location->archetype, *id));
        }
    }

    /// Untyped add, for command buffers and run-time components: moves the component of type
    /// `type` at `source` (nullptr for tags) into the entity, replacing the one it has. The source
    /// ends destroyed, even when the entity is dead (a contract violation).
    void insertRelocated(Entity entity, ComponentTypeId type, void* source);

    /// Untyped remove: removes the component of type `type` if the entity has it.
    void removeComponent(Entity entity, ComponentTypeId type);

    template <Component Type>
    [[nodiscard]] auto has(Entity entity) const -> bool {
        const auto id = m_components.find<Type>();
        const auto location = findLocation(entity);
        return id && location && archetype(location->archetype).contains(*id);
    }

    /// The component, or nullptr when the entity is dead or lacks it. Mutable access counts as a
    /// write: it raises the chunk's change version for this component.
    template <Component Type>
        requires(!TagComponent<Type>)
    [[nodiscard]] auto get(Entity entity) -> Type* {
        const auto id = m_components.find<Type>();
        const auto location = findLocation(entity);
        if (!id || !location || !archetype(location->archetype).contains(*id)) {
            return nullptr;
        }
        markChanged(*location, *id);
        return componentPointer<Type>(*location, *id);
    }

    template <Component Type>
        requires(!TagComponent<Type>)
    [[nodiscard]] auto get(Entity entity) const -> const Type* {
        const auto id = m_components.find<Type>();
        const auto location = findLocation(entity);
        if (!id || !location || !archetype(location->archetype).contains(*id)) {
            return nullptr;
        }
        const Archetype& owner = archetype(location->archetype);
        return std::launder(static_cast<const Type*>(owner.componentAt(*location, *owner.columnOf(*id))));
    }

    // --- Resources and events ----------------------------------------------------------------

    /// Stores `value` as the world's `Type`, replacing any previous one. Like structural changes,
    /// only outside system execution.
    template <Resource Type>
    void insertResource(Type value) {
        auto owned = std::make_unique<Type>(std::move(value));
        m_resources.insert_or_assign(detail::typeKeyOf<Type>(),
                                     ResourceObject{owned.release(), &destroyResource<Type>});
    }

    /// The world's `Type`, or nullptr when there is none.
    template <Resource Type>
    [[nodiscard]] auto resource() noexcept -> Type* {
        const auto found = m_resources.find(detail::typeKeyOf<Type>());
        return found != m_resources.end() ? static_cast<Type*>(found->second.get()) : nullptr;
    }

    template <Resource Type>
    [[nodiscard]] auto resource() const noexcept -> const Type* {
        const auto found = m_resources.find(detail::typeKeyOf<Type>());
        return found != m_resources.end() ? static_cast<const Type*>(found->second.get()) : nullptr;
    }

    template <Resource Type>
    void removeResource() {
        m_resources.erase(detail::typeKeyOf<Type>());
    }

    /// Creates the Events<Type> resource (if needed) and lets advanceEvents() move it along.
    template <Event Type>
    void addEvents() {
        if (resource<Events<Type>>() != nullptr) {
            return;
        }
        insertResource(Events<Type>{});
        m_eventQueues.push_back(EventQueue{
            .typeKey = detail::typeKeyOf<Events<Type>>(),
            .advance = [](void* events) noexcept { static_cast<Events<Type>*>(events)->advance(); },
        });
    }

    /// The tick boundary for events: called once per tick by the tick loop.
    void advanceEvents() noexcept;

    // --- Change versions ---------------------------------------------------------------------

    /// Starts at 1, so the version 0 of "never ran" is older than every write.
    [[nodiscard]] inline auto version() const noexcept -> Version { return m_version; }
    /// Moves to the next version and returns it (the scheduler does this before each system).
    auto advanceVersion() noexcept -> Version;

    // --- Storage, for queries and diagnostics ------------------------------------------------

    [[nodiscard]] inline auto archetypeCount() const noexcept -> std::size_t { return m_archetypes.size(); }
    [[nodiscard]] inline auto archetype(ArchetypeId id) noexcept -> Archetype& {
        return *m_archetypes[std::to_underlying(id)];
    }
    [[nodiscard]] inline auto archetype(ArchetypeId id) const noexcept -> const Archetype& {
        return *m_archetypes[std::to_underlying(id)];
    }
    [[nodiscard]] auto findLocation(Entity entity) const noexcept -> std::optional<EntityLocation>;
    [[nodiscard]] auto stats() const -> WorldStats;

    /// Held while a query iterates; structural changes check that none is active.
    class IterationScope {
    public:
        inline explicit IterationScope(const World& world) noexcept : m_world{&world} {
            m_world->m_activeIterations.fetch_add(1, std::memory_order_relaxed);
        }
        IterationScope(const IterationScope&) = delete;
        auto operator=(const IterationScope&) -> IterationScope& = delete;
        IterationScope(IterationScope&&) = delete;
        auto operator=(IterationScope&&) -> IterationScope& = delete;
        inline ~IterationScope() { m_world->m_activeIterations.fetch_sub(1, std::memory_order_relaxed); }

    private:
        const World* m_world;
    };

private:
    struct SpawnedRow {
        Entity entity;
        EntityLocation location;
    };

    using ResourceObject = std::unique_ptr<void, void (*)(void*) noexcept>;

    struct EventQueue {
        const void* typeKey = nullptr; // of the Events<T> resource
        void (*advance)(void* events) noexcept = nullptr;
    };

    template <class Type>
    static void destroyResource(void* object) noexcept {
        const std::unique_ptr<Type> owned{static_cast<Type*>(object)};
    }

    [[nodiscard]] auto spawnUninitialized(std::vector<ComponentTypeId> types) -> SpawnedRow;
    [[nodiscard]] auto requireLocation(Entity entity) const -> std::optional<EntityLocation>;
    [[nodiscard]] auto archetypeFor(std::vector<ComponentTypeId> sortedTypes) -> ArchetypeId;
    [[nodiscard]] auto archetypeWith(ArchetypeId from, ComponentTypeId added) -> ArchetypeId;
    [[nodiscard]] auto archetypeWithout(ArchetypeId from, ComponentTypeId removed) -> ArchetypeId;
    /// Moves the entity's row to `target`, keeping the components both archetypes have and
    /// destroying the others. Components only `target` has are left for the caller to construct.
    [[nodiscard]] auto moveEntity(Entity entity, EntityLocation from, ArchetypeId target) -> EntityLocation;
    void expectNoIteration() const noexcept;
    void markChanged(EntityLocation location, ComponentTypeId type) noexcept;

    /// The archetype at `location` must store `id`.
    template <Component Type>
    [[nodiscard]] auto componentPointer(EntityLocation location, ComponentTypeId id) noexcept -> Type* {
        Archetype& owner = archetype(location.archetype);
        return std::launder(static_cast<Type*>(owner.componentAt(location, *owner.columnOf(id))));
    }

    template <class Value>
    void constructComponent(EntityLocation location, std::optional<ComponentTypeId> id, Value&& value) {
        using Type = std::remove_cvref_t<Value>;
        if constexpr (!TagComponent<Type>) {
            if (id.has_value()) {
                Archetype& owner = archetype(location.archetype);
                std::construct_at(static_cast<Type*>(owner.componentAt(location, *owner.columnOf(*id))),
                                  std::forward<Value>(value));
            }
        }
    }

    // Declaration order matters: archetypes destroy their components through the registry's infos.
    ComponentRegistry m_components;
    EntityTable m_entities;
    std::vector<EntityLocation> m_locations; // by entity slot
    detail::ChunkPool m_chunkPool;
    std::vector<std::unique_ptr<Archetype>> m_archetypes;
    std::map<std::vector<ComponentTypeId>, ArchetypeId> m_archetypeByTypes;
    std::unordered_map<const void*, ResourceObject> m_resources;
    std::vector<EventQueue> m_eventQueues;
    Version m_version{1};
    mutable std::atomic<int> m_activeIterations{0};
};

} // namespace nx::ecs
