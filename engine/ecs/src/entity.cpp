// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ecs;

import std;
import nx.core;

namespace nx::ecs {

EntityTable::EntityTable(std::uint32_t generationLimit) noexcept : m_generationLimit{generationLimit} {
    expects(generationLimit > 0, "EntityTable: the generation limit must allow at least one generation");
    if (m_generationLimit == 0) {
        m_generationLimit = 1; // the handler let execution continue ("observe")
    }
}

auto EntityTable::create() -> Entity {
    if (m_firstFree != kNoSlot) {
        const std::uint32_t slot = m_firstFree;
        m_firstFree = m_slots[slot].nextFree;
        if (m_firstFree == kNoSlot) {
            m_lastFree = kNoSlot;
        }
        --m_freeCount;
        return Entity{EntityIndex{slot}, m_slots[slot].generation};
    }
    // Index max() is the null entity; memory runs out long before this does.
    invariant(m_slots.size() < kNoSlot, "EntityTable: the 32-bit index space is exhausted");
    const auto slot = static_cast<std::uint32_t>(m_slots.size());
    m_slots.emplace_back();
    return Entity{EntityIndex{slot}, Generation{0}};
}

void EntityTable::destroy(Entity entity) {
    if (!isAlive(entity)) {
        expects(false, "EntityTable::destroy: the entity is not alive");
        return;
    }
    const std::uint32_t slot = std::to_underlying(entity.index());
    const std::uint32_t next = std::to_underlying(entity.generation()) + 1;
    // The slot moves on to the next generation right away, so the destroyed handle stops matching.
    m_slots[slot].generation = Generation{next};
    if (next >= m_generationLimit) {
        ++m_retiredCount;
        return;
    }
    m_slots[slot].nextFree = kNoSlot;
    if (m_lastFree == kNoSlot) {
        m_firstFree = slot;
    } else {
        m_slots[m_lastFree].nextFree = slot;
    }
    m_lastFree = slot;
    ++m_freeCount;
}

} // namespace nx::ecs
