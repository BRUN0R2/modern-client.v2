// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a system reads and writes, from which the scheduler derives parallelism
/// (docs/03-ecs.md, section 7.3).
export module nx.ecs:access;

import std;
import nx.core;
import :component;

export namespace nx::ecs {

struct SystemAccess {
    std::vector<ComponentTypeId> componentReads;
    std::vector<ComponentTypeId> componentWrites;
    std::vector<const void*> resourceReads; // type keys of resources (events are resources)
    std::vector<const void*> resourceWrites;
    /// Takes the whole World: runs alone.
    bool isExclusive = false;

    /// Two systems conflict when one writes a component or resource the other reads or writes.
    [[nodiscard]] inline auto conflictsWith(const SystemAccess& other) const noexcept -> bool {
        if (isExclusive || other.isExclusive) {
            return true;
        }
        return writesAny(other.componentReads, componentWrites) ||
               writesAny(other.componentWrites, componentWrites) ||
               writesAny(componentReads, other.componentWrites) ||
               writesAny(other.resourceReads, resourceWrites) ||
               writesAny(other.resourceWrites, resourceWrites) ||
               writesAny(resourceReads, other.resourceWrites);
    }

private:
    template <class Key>
    [[nodiscard]] static auto writesAny(const std::vector<Key>& touched,
                                        const std::vector<Key>& written) noexcept -> bool {
        return std::ranges::any_of(
            touched, [&written](const Key& key) { return std::ranges::contains(written, key); });
    }
};

} // namespace nx::ecs
