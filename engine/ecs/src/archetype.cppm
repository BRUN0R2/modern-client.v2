// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Archetypes and their 16 KiB SoA chunks (docs/03-ecs.md, section 5).
export module nx.ecs:archetype;

import std;
import nx.core;
import :component;
import :entity;

export namespace nx::ecs {

/// World change counter. Chunks record, per column, the version of the last write, which is what
/// Changed<T> compares against.
enum class Version : std::uint64_t {};

/// Index of an archetype in its World, in creation order.
enum class ArchetypeId : std::uint32_t {};

/// Size of a chunk. A row larger than this gets chunks of several contiguous blocks.
inline constexpr std::size_t kChunkBytes = 16 * 1024;

/// Where the components of an entity live.
struct EntityLocation {
    ArchetypeId archetype{}; ///< The archetype that stores them.
    std::uint32_t chunk = 0; ///< Index of the chunk in that archetype.
    std::uint32_t row = 0;   ///< Row in that chunk.
};

/// What removeRow() does with the components of the removed row.
enum class RowContents : std::uint8_t {
    Live,     ///< Still constructed: destroy them.
    MovedOut, ///< Already relocated or destroyed by the caller.
};

namespace detail {

struct alignas(kMaxComponentAlignment) ChunkBlock {
    std::array<std::byte, kChunkBytes> bytes;
};

using ChunkStorage = std::unique_ptr<ChunkBlock[]>;

/// Moves a component from `source` to the uninitialized `destination`; the source ends destroyed.
inline void relocateComponent(const ComponentInfo& info, void* destination, void* source) noexcept {
    if (info.relocate != nullptr) {
        info.relocate(destination, source);
    } else {
        std::memcpy(destination, source, info.size);
    }
}

/// Keeps the storage of emptied single-block chunks for the next chunk of any archetype.
class ChunkPool {
public:
    [[nodiscard]] auto acquire(std::size_t blockCount) -> ChunkStorage;
    void release(ChunkStorage storage, std::size_t blockCount);
    [[nodiscard]] inline auto pooledCount() const noexcept -> std::size_t { return m_free.size(); }

private:
    std::vector<ChunkStorage> m_free;
};

} // namespace detail

/// A component array inside every chunk of an archetype.
struct Column {
    ComponentTypeId type{};              ///< The component stored.
    std::size_t offset = 0;              ///< Bytes from the start of the chunk.
    const ComponentInfo* info = nullptr; ///< What the registry knows of the component.
};

/// One block of an archetype's rows: the entity array, then one array per column.
class Chunk {
public:
    /// A chunk without rows in `storage`, with version 0 for each of its `columnCount` columns.
    Chunk(detail::ChunkStorage storage, std::size_t columnCount);

    /// Rows in use, from the first.
    [[nodiscard]] inline auto rowCount() const noexcept -> std::uint32_t { return m_rowCount; }
    /// Start of the chunk: its entity array.
    [[nodiscard]] inline auto data() noexcept -> std::byte* { return m_storage[0].bytes.data(); }
    [[nodiscard]] inline auto data() const noexcept -> const std::byte* { return m_storage[0].bytes.data(); }

    /// The version of the last write to the column.
    [[nodiscard]] inline auto changedVersion(std::size_t column) const noexcept -> Version {
        return m_changed[column];
    }
    /// Raises the column's version to `version` (versions never go back).
    inline void markChanged(std::size_t column, Version version) noexcept {
        m_changed[column] = std::max(m_changed[column], version);
    }

private:
    friend class Archetype;

    void markAllChanged(Version version) noexcept;

    detail::ChunkStorage m_storage;
    std::vector<Version> m_changed; // per column
    std::uint32_t m_rowCount = 0;
};

/// All the entities with exactly one set of component types, stored by chunk. Rows stay packed:
/// every chunk is full except the last, because removal moves the archetype's last row into the
/// hole (swap-and-pop), and an emptied last chunk goes back to the pool.
class Archetype {
public:
    /// `types` must be sorted and unique.
    Archetype(ArchetypeId id, std::vector<ComponentTypeId> types, const ComponentRegistry& registry);

    Archetype(const Archetype&) = delete;
    auto operator=(const Archetype&) -> Archetype& = delete;
    Archetype(Archetype&&) = delete;
    auto operator=(Archetype&&) -> Archetype& = delete;
    ~Archetype();

    /// Its id in its World.
    [[nodiscard]] inline auto id() const noexcept -> ArchetypeId { return m_id; }
    /// Its component types, tags included, sorted.
    [[nodiscard]] inline auto types() const noexcept -> std::span<const ComponentTypeId> { return m_types; }
    /// Whether `type` is one of its component types, tags included.
    [[nodiscard]] auto contains(ComponentTypeId type) const noexcept -> bool;
    /// Column index of a stored component; nullopt for tags and absent components.
    [[nodiscard]] auto columnOf(ComponentTypeId type) const noexcept -> std::optional<std::size_t>;
    /// The stored components (tags have no column), in type order.
    [[nodiscard]] inline auto columns() const noexcept -> std::span<const Column> { return m_columns; }

    /// Rows a chunk holds.
    [[nodiscard]] inline auto chunkCapacity() const noexcept -> std::uint32_t { return m_chunkCapacity; }
    /// Chunks in use; every one is full except the last.
    [[nodiscard]] inline auto chunkCount() const noexcept -> std::size_t { return m_chunks.size(); }
    /// The chunk at `index`, which must be below chunkCount().
    [[nodiscard]] inline auto chunk(std::size_t index) noexcept -> Chunk& { return m_chunks[index]; }
    [[nodiscard]] inline auto chunk(std::size_t index) const noexcept -> const Chunk& {
        return m_chunks[index];
    }
    /// Entities stored, in every chunk.
    [[nodiscard]] auto entityCount() const noexcept -> std::size_t;

    /// Start of the entity array of a chunk.
    [[nodiscard]] inline auto entities(std::size_t chunk) noexcept -> std::byte* {
        return m_chunks[chunk].data();
    }
    /// Start of a column's array in a chunk.
    [[nodiscard]] inline auto columnData(std::size_t chunk, std::size_t column) noexcept -> std::byte* {
        return m_chunks[chunk].data() + m_columns[column].offset;
    }
    [[nodiscard]] inline auto columnData(std::size_t chunk, std::size_t column) const noexcept
        -> const std::byte* {
        return m_chunks[chunk].data() + m_columns[column].offset;
    }
    /// One component of one row.
    [[nodiscard]] inline auto componentAt(EntityLocation location, std::size_t column) noexcept -> void* {
        return columnData(location.chunk, column) +
               (std::size_t{location.row} * m_columns[column].info->size);
    }
    [[nodiscard]] inline auto componentAt(EntityLocation location, std::size_t column) const noexcept -> const
        void* {
        return columnData(location.chunk, column) +
               (std::size_t{location.row} * m_columns[column].info->size);
    }
    /// The entity of one row.
    [[nodiscard]] auto entityAt(EntityLocation location) const noexcept -> Entity;

    /// Appends a row for `entity`. Its components are left uninitialized: the caller constructs
    /// every column. The chunk's versions become `now`, since its contents changed.
    [[nodiscard]] auto appendRow(Entity entity, detail::ChunkPool& pool, Version now) -> EntityLocation;

    /// appendRow for each of `entities` (at least one), in order and a chunk at a time. Returns the
    /// first row; each next one is nextRow() of the one before.
    [[nodiscard]] auto appendRows(std::span<const Entity> entities, detail::ChunkPool& pool, Version now)
        -> EntityLocation;

    /// The row after `location` in this archetype: the next in its chunk, or the first of the next.
    [[nodiscard]] inline auto nextRow(EntityLocation location) const noexcept -> EntityLocation {
        if (location.row + 1 < m_chunkCapacity) {
            return EntityLocation{.archetype = m_id, .chunk = location.chunk, .row = location.row + 1};
        }
        return EntityLocation{.archetype = m_id, .chunk = location.chunk + 1, .row = 0};
    }

    /// Removes a row, moving the archetype's last row into the hole. Returns the entity that moved,
    /// whose location the caller updates.
    [[nodiscard]] auto removeRow(EntityLocation location, RowContents contents, detail::ChunkPool& pool,
                                 Version now) -> std::optional<Entity>;

    /// Archetype graph: the archetype reached by adding or removing one component type.
    [[nodiscard]] auto findAddEdge(ComponentTypeId type) const -> std::optional<ArchetypeId>;
    [[nodiscard]] auto findRemoveEdge(ComponentTypeId type) const -> std::optional<ArchetypeId>;
    /// Records the archetype reached by adding or removing `type`, for the find functions above.
    inline void setAddEdge(ComponentTypeId type, ArchetypeId target) {
        m_addEdges.insert_or_assign(type, target);
    }
    inline void setRemoveEdge(ComponentTypeId type, ArchetypeId target) {
        m_removeEdges.insert_or_assign(type, target);
    }

private:
    void computeLayout();
    [[nodiscard]] auto layOut(std::size_t capacity) -> std::size_t;
    void destroyRow(EntityLocation location) noexcept;

    ArchetypeId m_id;
    std::vector<ComponentTypeId> m_types;
    std::vector<Column> m_columns;                // stored components, in type order
    std::vector<std::size_t> m_destructedColumns; // the columns whose type has a destructor to run
    std::vector<Chunk> m_chunks;
    std::uint32_t m_chunkCapacity = 0;
    std::size_t m_blocksPerChunk = 1;
    std::unordered_map<ComponentTypeId, ArchetypeId> m_addEdges;
    std::unordered_map<ComponentTypeId, ArchetypeId> m_removeEdges;
};

} // namespace nx::ecs
