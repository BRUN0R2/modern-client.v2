// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ecs;

import std;
import nx.core;

namespace nx::ecs {

namespace {

[[nodiscard]] constexpr auto alignUp(std::size_t value, std::size_t alignment) noexcept -> std::size_t {
    return (value + alignment - 1) & ~(alignment - 1);
}

} // namespace

namespace detail {

void relocateComponent(const ComponentInfo& info, void* destination, void* source) noexcept {
    if (info.relocate != nullptr) {
        info.relocate(destination, source);
    } else {
        std::memcpy(destination, source, info.size);
    }
}

auto ChunkPool::acquire(std::size_t blockCount) -> ChunkStorage {
    if (blockCount == 1 && !m_free.empty()) {
        ChunkStorage storage = std::move(m_free.back());
        m_free.pop_back();
        return storage;
    }
    return std::make_unique_for_overwrite<ChunkBlock[]>(blockCount);
}

void ChunkPool::release(ChunkStorage storage, std::size_t blockCount) {
    // Only standard-size storage is shared; oversized chunks belong to rare, huge rows.
    if (blockCount == 1) {
        m_free.push_back(std::move(storage));
    }
}

} // namespace detail

Chunk::Chunk(detail::ChunkStorage storage, std::size_t columnCount)
    : m_storage{std::move(storage)}, m_changed(columnCount, Version{0}) {}

void Chunk::markAllChanged(Version version) noexcept {
    for (std::size_t column = 0; column < m_changed.size(); ++column) {
        markChanged(column, version);
    }
}

Archetype::Archetype(ArchetypeId id, std::vector<ComponentTypeId> types, const ComponentRegistry& registry)
    : m_id{id}, m_types{std::move(types)} {
    expects(std::ranges::is_sorted(m_types) && std::ranges::adjacent_find(m_types) == m_types.end(),
            "Archetype: component types must be sorted and unique");
    for (const ComponentTypeId type : m_types) {
        const ComponentInfo& info = registry.info(type);
        if (!info.isTag()) {
            m_columns.push_back(Column{.type = type, .offset = 0, .info = &info});
        }
    }
    computeLayout();
}

Archetype::~Archetype() {
    for (std::uint32_t chunk = 0; chunk < m_chunks.size(); ++chunk) {
        for (std::uint32_t row = 0; row < m_chunks[chunk].rowCount(); ++row) {
            destroyRow(EntityLocation{.archetype = m_id, .chunk = chunk, .row = row});
        }
    }
}

auto Archetype::contains(ComponentTypeId type) const noexcept -> bool {
    return std::ranges::binary_search(m_types, type);
}

auto Archetype::columnOf(ComponentTypeId type) const noexcept -> std::optional<std::size_t> {
    const auto found = std::ranges::lower_bound(m_columns, type, {}, &Column::type);
    if (found == m_columns.end() || found->type != type) {
        return std::nullopt;
    }
    return static_cast<std::size_t>(found - m_columns.begin());
}

auto Archetype::entityCount() const noexcept -> std::size_t {
    if (m_chunks.empty()) {
        return 0;
    }
    return ((m_chunks.size() - 1) * m_chunkCapacity) + m_chunks.back().rowCount();
}

auto Archetype::entityAt(EntityLocation location) const noexcept -> Entity {
    Entity entity;
    std::memcpy(&entity, m_chunks[location.chunk].data() + (std::size_t{location.row} * sizeof(Entity)),
                sizeof(Entity));
    return entity;
}

auto Archetype::appendRow(Entity entity, detail::ChunkPool& pool, Version now) -> EntityLocation {
    if (m_chunks.empty() || m_chunks.back().rowCount() == m_chunkCapacity) {
        m_chunks.emplace_back(pool.acquire(m_blocksPerChunk), m_columns.size());
    }
    Chunk& chunk = m_chunks.back();
    const EntityLocation location{
        .archetype = m_id,
        .chunk = narrow<std::uint32_t>(m_chunks.size() - 1),
        .row = chunk.m_rowCount,
    };
    std::memcpy(chunk.data() + (std::size_t{location.row} * sizeof(Entity)), &entity, sizeof(Entity));
    ++chunk.m_rowCount;
    chunk.markAllChanged(now);
    return location;
}

auto Archetype::removeRow(EntityLocation location, RowContents contents, detail::ChunkPool& pool, Version now)
    -> std::optional<Entity> {
    if (contents == RowContents::Live) {
        destroyRow(location);
    }
    const EntityLocation last{
        .archetype = m_id,
        .chunk = narrow<std::uint32_t>(m_chunks.size() - 1),
        .row = m_chunks.back().rowCount() - 1,
    };

    std::optional<Entity> moved;
    if (location.chunk != last.chunk || location.row != last.row) {
        for (std::size_t column = 0; column < m_columns.size(); ++column) {
            detail::relocateComponent(*m_columns[column].info, componentAt(location, column),
                                      componentAt(last, column));
        }
        moved = entityAt(last);
        std::memcpy(m_chunks[location.chunk].data() + (std::size_t{location.row} * sizeof(Entity)), &*moved,
                    sizeof(Entity));
        // Another entity's data now sits in this chunk.
        m_chunks[location.chunk].markAllChanged(now);
    }

    Chunk& lastChunk = m_chunks.back();
    --lastChunk.m_rowCount;
    if (lastChunk.m_rowCount == 0) {
        pool.release(std::move(lastChunk.m_storage), m_blocksPerChunk);
        m_chunks.pop_back();
    }
    return moved;
}

auto Archetype::findAddEdge(ComponentTypeId type) const -> std::optional<ArchetypeId> {
    if (const auto found = m_addEdges.find(type); found != m_addEdges.end()) {
        return found->second;
    }
    return std::nullopt;
}

auto Archetype::findRemoveEdge(ComponentTypeId type) const -> std::optional<ArchetypeId> {
    if (const auto found = m_removeEdges.find(type); found != m_removeEdges.end()) {
        return found->second;
    }
    return std::nullopt;
}

void Archetype::computeLayout() {
    std::size_t rowBytes = sizeof(Entity);
    for (const Column& column : m_columns) {
        rowBytes += column.info->size;
    }
    // The most rows that fit once each array is aligned; a row larger than a block gets more blocks.
    for (m_blocksPerChunk = 1;; ++m_blocksPerChunk) {
        const std::size_t chunkBytes = m_blocksPerChunk * kChunkBytes;
        for (std::size_t capacity = chunkBytes / rowBytes; capacity > 0; --capacity) {
            if (layOut(capacity) <= chunkBytes) {
                m_chunkCapacity = narrow<std::uint32_t>(capacity);
                return;
            }
        }
    }
}

auto Archetype::layOut(std::size_t capacity) -> std::size_t {
    // The entity array comes first; Entity's alignment is below the chunk's.
    std::size_t offset = sizeof(Entity) * capacity;
    for (Column& column : m_columns) {
        offset = alignUp(offset, column.info->alignment);
        column.offset = offset;
        offset += column.info->size * capacity;
    }
    return offset;
}

void Archetype::destroyRow(EntityLocation location) noexcept {
    for (std::size_t column = 0; column < m_columns.size(); ++column) {
        if (const auto destroy = m_columns[column].info->destroy; destroy != nullptr) {
            destroy(componentAt(location, column));
        }
    }
}

} // namespace nx::ecs
