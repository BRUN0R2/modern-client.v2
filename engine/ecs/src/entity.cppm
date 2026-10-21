// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Entity handles and the table that issues them (docs/03-ecs.md, section 3).
export module nx.ecs:entity;

import std;

export namespace nx::ecs {

/// Slot of an entity in its world. A slot is not a handle: only an Entity, which also carries the
/// generation, names an entity.
enum class EntityIndex : std::uint32_t {};

/// Incarnation of a slot. It changes every time the entity in the slot is destroyed.
enum class Generation : std::uint32_t {};

/// Opaque 64-bit handle: slot index plus generation. It carries no data; a handle whose entity was
/// destroyed is never alive again and never names another entity.
class Entity {
public:
    /// Index of the null entity, which no slot ever has.
    static constexpr EntityIndex kNullIndex{std::numeric_limits<std::uint32_t>::max()};

    /// The null entity: never alive. Stands for "no entity" in components (`Owner{}`).
    constexpr Entity() noexcept = default;

    /// Raw indices are not handles: entities are issued by the World.
    Entity(std::uint32_t rawIndex) = delete;

    [[nodiscard]] constexpr auto index() const noexcept -> EntityIndex { return m_index; }
    [[nodiscard]] constexpr auto generation() const noexcept -> Generation { return m_generation; }
    [[nodiscard]] constexpr auto isNull() const noexcept -> bool { return m_index == kNullIndex; }

    // Members, not hidden friends: GCC 16 crashes on defaulted friend comparisons from modules
    // (docs/17-build-and-tools.md, section 2.2).
    [[nodiscard]] constexpr auto operator==(const Entity&) const noexcept -> bool = default;
    [[nodiscard]] constexpr auto operator<=>(const Entity&) const noexcept = default;

private:
    friend class EntityTable;

    constexpr Entity(EntityIndex index, Generation generation) noexcept
        : m_index{index}, m_generation{generation} {}

    EntityIndex m_index = kNullIndex;
    Generation m_generation{};
};

/// Issues entity handles and tells which ones are alive.
///
/// Freed slots are reused in FIFO order, so an index stays unused for as long as possible. A slot
/// whose generation reaches the limit is retired instead of wrapping around, so a stale handle can
/// never name another entity. The free slots form a queue threaded through the slots themselves:
/// creating and destroying never allocate once the table has grown.
class EntityTable {
public:
    /// Generations run from 0 to kGenerationLimit - 1.
    static constexpr std::uint32_t kGenerationLimit = std::numeric_limits<std::uint32_t>::max();

    EntityTable() noexcept = default;

    /// Retires slots once their generation reaches `generationLimit` (at least 1). Smaller limits
    /// than kGenerationLimit exist to test retirement without four billion destructions.
    explicit EntityTable(std::uint32_t generationLimit) noexcept;

    [[nodiscard]] auto create() -> Entity;

    /// `entity` must be alive.
    void destroy(Entity entity);

    [[nodiscard]] inline auto isAlive(Entity entity) const noexcept -> bool {
        // A freed or retired slot already holds a generation no issued handle carries.
        const std::uint32_t slot = std::to_underlying(entity.index());
        return slot < m_slots.size() && m_slots[slot].generation == entity.generation();
    }

    /// Slots ever used, alive or not; slot indices are below this.
    [[nodiscard]] inline auto slotCount() const noexcept -> std::size_t { return m_slots.size(); }
    [[nodiscard]] inline auto aliveCount() const noexcept -> std::size_t {
        return m_slots.size() - m_freeCount - m_retiredCount;
    }
    /// Slots whose generations ran out (for ecs_stats, planned in TODO.md).
    [[nodiscard]] inline auto retiredCount() const noexcept -> std::size_t { return m_retiredCount; }

private:
    /// Ends the queue of free slots; no slot has this index (it is the null entity's).
    static constexpr std::uint32_t kNoSlot = std::to_underlying(Entity::kNullIndex);

    struct Slot {
        Generation generation{};          // current generation: the alive handle carries it
        std::uint32_t nextFree = kNoSlot; // while free: the slot freed after this one
    };

    std::vector<Slot> m_slots;
    std::uint32_t m_firstFree = kNoSlot; // oldest freed: the next to reuse
    std::uint32_t m_lastFree = kNoSlot;  // newest freed
    std::size_t m_freeCount = 0;
    std::size_t m_retiredCount = 0;
    std::uint32_t m_generationLimit = kGenerationLimit;
};

} // namespace nx::ecs

template <>
struct std::hash<nx::ecs::Entity> {
    [[nodiscard]] inline auto operator()(nx::ecs::Entity entity) const noexcept -> std::size_t {
        constexpr unsigned kGenerationShift = 32;
        const std::uint64_t packed =
            (std::uint64_t{std::to_underlying(entity.generation())} << kGenerationShift) |
            std::to_underlying(entity.index());
        return std::hash<std::uint64_t>{}(packed);
    }
};

/// Formats as "<index>v<generation>" (e.g. "42v3"), or "null". Takes no format spec.
template <>
struct std::formatter<nx::ecs::Entity> {
    constexpr auto parse(std::format_parse_context& context) { return context.begin(); }

    template <class FormatContext>
    auto format(nx::ecs::Entity entity, FormatContext& context) const {
        if (entity.isNull()) {
            return std::format_to(context.out(), "null");
        }
        return std::format_to(context.out(), "{}v{}", std::to_underlying(entity.index()),
                              std::to_underlying(entity.generation()));
    }
};
