// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ecs;

import std;
import nx.core;

namespace nx::ecs {

auto ComponentRegistry::add(ComponentInfo info, const void* typeKey)
    -> Result<ComponentTypeId, ComponentError> {
    if (info.name.empty()) {
        return fail(ComponentError::InvalidName, "component names must not be empty");
    }
    if (info.alignment > kMaxComponentAlignment || !std::has_single_bit(info.alignment)) {
        return fail(ComponentError::InvalidAlignment,
                    std::format("'{}' needs alignment {}, chunks guarantee {}", info.name, info.alignment,
                                kMaxComponentAlignment));
    }
    if (typeKey != nullptr && m_byTypeKey.contains(typeKey)) {
        return fail(ComponentError::TypeAlreadyRegistered,
                    std::format("'{}' names a type already registered as '{}'", info.name,
                                m_infos[std::to_underlying(m_byTypeKey.at(typeKey))].name));
    }
    if (const auto existing = m_byStableId.find(info.stableId); existing != m_byStableId.end()) {
        const ComponentInfo& other = m_infos[std::to_underlying(existing->second)];
        if (other.name == info.name) {
            return fail(ComponentError::NameAlreadyRegistered, std::format("'{}'", info.name));
        }
        return fail(ComponentError::StableIdCollision, std::format("'{}' and '{}' share the stable id {}",
                                                                   info.name, other.name, info.stableId));
    }

    const ComponentTypeId id{narrow<std::uint32_t>(m_infos.size())};
    m_byStableId.emplace(info.stableId, id);
    if (typeKey != nullptr) {
        m_byTypeKey.emplace(typeKey, id);
    }
    m_infos.push_back(std::move(info));
    return id;
}

auto ComponentRegistry::findByTypeKey(const void* typeKey) const -> std::optional<ComponentTypeId> {
    if (const auto found = m_byTypeKey.find(typeKey); found != m_byTypeKey.end()) {
        return found->second;
    }
    return std::nullopt;
}

auto ComponentRegistry::findByStableId(StableId stableId) const -> std::optional<ComponentTypeId> {
    if (const auto found = m_byStableId.find(stableId); found != m_byStableId.end()) {
        return found->second;
    }
    return std::nullopt;
}

auto ComponentRegistry::info(ComponentTypeId id) const noexcept -> const ComponentInfo& {
    if (std::to_underlying(id) >= m_infos.size()) {
        expects(false, "ComponentRegistry::info: unknown component type");
        static const ComponentInfo kUnknown{.name = "unknown",
                                            .stableId = StableId::fromName("unknown")}; // a tag
        return kUnknown;
    }
    return m_infos[std::to_underlying(id)];
}

} // namespace nx::ecs
