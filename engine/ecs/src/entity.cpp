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
    if (!m_freeSlots.empty()) {
        const std::uint32_t slot = m_freeSlots.front();
        m_freeSlots.pop_front();
        return Entity{EntityIndex{slot}, m_generations[slot]};
    }
    // Index max() is the null entity; memory runs out long before this does.
    invariant(m_generations.size() < std::to_underlying(Entity::kNullIndex),
              "EntityTable: the 32-bit index space is exhausted");
    const auto slot = static_cast<std::uint32_t>(m_generations.size());
    m_generations.push_back(Generation{0});
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
    m_generations[slot] = Generation{next};
    if (next >= m_generationLimit) {
        ++m_retiredCount;
    } else {
        m_freeSlots.push_back(slot);
    }
}

auto EntityTable::isAlive(Entity entity) const noexcept -> bool {
    // A freed or retired slot already holds a generation no issued handle carries.
    const std::uint32_t slot = std::to_underlying(entity.index());
    return slot < m_generations.size() && m_generations[slot] == entity.generation();
}

} // namespace nx::ecs
