// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ecs;

import std;
import nx.core;

namespace nx::ecs {

World::World() {
    // Archetype 0 holds the entities without components.
    std::ignore = archetypeFor({});
}

World::~World() = default;

auto World::spawn() -> Entity {
    return spawnUninitialized({}).entity;
}

auto World::spawnUninitialized(std::span<const ComponentTypeId> sortedTypes) -> SpawnedRow {
    expectNoIteration();
    return appendEntity(archetype(archetypeFor(sortedTypes)));
}

auto World::appendEntity(Archetype& owner) -> SpawnedRow {
    const Entity entity = m_entities.create();
    const EntityLocation location = owner.appendRow(entity, m_chunkPool, m_version);
    setLocation(entity, location);
    return SpawnedRow{.entity = entity, .location = location};
}

void World::setLocation(Entity entity, EntityLocation location) {
    const auto slot = std::size_t{std::to_underlying(entity.index())};
    if (slot >= m_locations.size()) {
        m_locations.resize(slot + 1);
    }
    m_locations[slot] = location;
}

void World::spawnRelocated(std::span<const ComponentTypeId> types, std::span<void* const> sources,
                           std::span<Entity> spawned) {
    const bool isComplete = sources.size() == types.size() * spawned.size();
    expects(isComplete, "World::spawnRelocated: one source per component of each entity");
    m_spawnTypes.assign(types.begin(), types.end());
    std::ranges::sort(m_spawnTypes);
    const bool isDistinct = std::ranges::adjacent_find(m_spawnTypes) == m_spawnTypes.end();
    expects(isDistinct, "World::spawnRelocated: each component type at most once");
    if (!isComplete || !isDistinct || spawned.empty()) {
        return; // an empty batch, or a violation the handler let execution continue past
    }
    expectNoIteration();
    Archetype& owner = archetype(archetypeFor(m_spawnTypes));
    m_spawnColumns.clear();
    for (const ComponentTypeId type : types) {
        m_spawnColumns.push_back(owner.columnOf(type));
    }
    for (Entity& entity : spawned) {
        entity = m_entities.create();
    }
    EntityLocation location = owner.appendRows(spawned, m_chunkPool, m_version);
    for (std::size_t index = 0; index < spawned.size(); ++index, location = owner.nextRow(location)) {
        setLocation(spawned[index], location);
        const auto own = sources.subspan(index * types.size(), types.size());
        for (std::size_t component = 0; component < own.size(); ++component) {
            if (const auto column = m_spawnColumns[component]) {
                detail::relocateComponent(*owner.columns()[*column].info,
                                          owner.componentAt(location, *column), own[component]);
            }
        }
    }
}

void World::destroy(Entity entity) {
    const auto location = requireLocation(entity);
    if (!location) {
        return;
    }
    expectNoIteration();
    if (const auto moved =
            archetype(location->archetype).removeRow(*location, RowContents::Live, m_chunkPool, m_version)) {
        m_locations[std::to_underlying(moved->index())] = *location;
    }
    m_entities.destroy(entity);
}

void World::insertRelocated(Entity entity, ComponentTypeId type, void* source) {
    const ComponentInfo& info = m_components.info(type);
    const auto location = requireLocation(entity);
    if (!location) {
        if (source != nullptr && info.destroy != nullptr) {
            info.destroy(source);
        }
        return;
    }
    Archetype& owner = archetype(location->archetype);
    if (owner.contains(type)) {
        if (const auto column = owner.columnOf(type)) {
            void* const existing = owner.componentAt(*location, *column);
            if (info.destroy != nullptr) {
                info.destroy(existing);
            }
            detail::relocateComponent(info, existing, source);
            markChanged(*location, type);
        }
        return;
    }
    const EntityLocation moved = moveEntity(entity, *location, archetypeWith(location->archetype, type));
    Archetype& target = archetype(moved.archetype);
    if (const auto column = target.columnOf(type)) {
        detail::relocateComponent(info, target.componentAt(moved, *column), source);
    }
}

void World::removeComponent(Entity entity, ComponentTypeId type) {
    const auto location = requireLocation(entity);
    if (location && archetype(location->archetype).contains(type)) {
        std::ignore = moveEntity(entity, *location, archetypeWithout(location->archetype, type));
    }
}

void World::advanceEvents() noexcept {
    for (const EventQueue& queue : m_eventQueues) {
        if (const auto found = m_resources.find(queue.typeKey); found != m_resources.end()) {
            queue.advance(found->second.get());
        }
    }
}

auto World::advanceVersion() noexcept -> Version {
    m_version = Version{std::to_underlying(m_version) + 1};
    return m_version;
}

auto World::findLocation(Entity entity) const noexcept -> std::optional<EntityLocation> {
    if (!m_entities.isAlive(entity)) {
        return std::nullopt;
    }
    return m_locations[std::to_underlying(entity.index())];
}

auto World::requireLocation(Entity entity) const -> std::optional<EntityLocation> {
    const auto location = findLocation(entity);
    expects(location.has_value(), "World: the entity is not alive");
    return location;
}

auto World::stats() const -> WorldStats {
    WorldStats stats{
        .entityCount = m_entities.aliveCount(),
        .archetypeCount = m_archetypes.size(),
        .pooledChunkCount = m_chunkPool.pooledCount(),
        .retiredSlotCount = m_entities.retiredCount(),
    };
    for (const auto& stored : m_archetypes) {
        stats.chunkCount += stored->chunkCount();
    }
    return stats;
}

auto World::archetypeFor(std::span<const ComponentTypeId> sortedTypes) -> ArchetypeId {
    if (const auto found = m_archetypeByTypes.find(sortedTypes); found != m_archetypeByTypes.end()) {
        return found->second;
    }
    const ArchetypeId id{narrow<std::uint32_t>(m_archetypes.size())};
    std::vector<ComponentTypeId> types{sortedTypes.begin(), sortedTypes.end()};
    m_archetypes.push_back(std::make_unique<Archetype>(id, types, m_components));
    m_archetypeByTypes.emplace(std::move(types), id);
    return id;
}

auto World::archetypeWith(ArchetypeId from, ComponentTypeId added) -> ArchetypeId {
    if (const auto cached = archetype(from).findAddEdge(added)) {
        return *cached;
    }
    const auto source = archetype(from).types();
    std::vector<ComponentTypeId> types{source.begin(), source.end()};
    types.insert(std::ranges::upper_bound(types, added), added);
    const ArchetypeId target = archetypeFor(std::move(types));
    archetype(from).setAddEdge(added, target);
    archetype(target).setRemoveEdge(added, from);
    return target;
}

auto World::archetypeWithout(ArchetypeId from, ComponentTypeId removed) -> ArchetypeId {
    if (const auto cached = archetype(from).findRemoveEdge(removed)) {
        return *cached;
    }
    const auto source = archetype(from).types();
    std::vector<ComponentTypeId> types{source.begin(), source.end()};
    std::erase(types, removed);
    const ArchetypeId target = archetypeFor(std::move(types));
    archetype(from).setRemoveEdge(removed, target);
    archetype(target).setAddEdge(removed, from);
    return target;
}

auto World::moveEntity(Entity entity, EntityLocation from, ArchetypeId target) -> EntityLocation {
    expectNoIteration();
    Archetype& source = archetype(from.archetype);
    Archetype& destination = archetype(target);
    const EntityLocation to = destination.appendRow(entity, m_chunkPool, m_version);

    const auto sourceColumns = source.columns();
    for (std::size_t column = 0; column < sourceColumns.size(); ++column) {
        const ComponentInfo& info = *sourceColumns[column].info;
        void* const value = source.componentAt(from, column);
        if (const auto kept = destination.columnOf(sourceColumns[column].type)) {
            detail::relocateComponent(info, destination.componentAt(to, *kept), value);
        } else if (info.destroy != nullptr) {
            info.destroy(value);
        }
    }

    if (const auto moved = source.removeRow(from, RowContents::MovedOut, m_chunkPool, m_version)) {
        m_locations[std::to_underlying(moved->index())] = from;
    }
    m_locations[std::to_underlying(entity.index())] = to;
    return to;
}

void World::expectNoIteration() const noexcept {
    // relaxed: a contract check that orders nothing; the schedule orders the iterations (IterationScope).
    expects(m_activeIterations.load(std::memory_order_relaxed) == 0,
            "World: structural change while a query is iterating (use Commands)");
}

void World::markChanged(EntityLocation location, ComponentTypeId type) noexcept {
    Archetype& owner = archetype(location.archetype);
    if (const auto column = owner.columnOf(type)) {
        owner.chunk(location.chunk).markChanged(*column, m_version);
    }
}

} // namespace nx::ecs
