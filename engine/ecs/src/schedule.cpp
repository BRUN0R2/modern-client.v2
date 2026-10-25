// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ecs;

import std;
import nx.core;
import nx.jobs;

namespace nx::ecs {

namespace {

/// Square boolean matrix: `edges[from][to]`.
class Relation {
public:
    explicit Relation(std::size_t size) : m_size{size}, m_cells(size * size, false) {}

    [[nodiscard]] auto size() const noexcept -> std::size_t { return m_size; }
    [[nodiscard]] auto has(std::size_t from, std::size_t to) const noexcept -> bool {
        return m_cells[(from * m_size) + to];
    }
    void set(std::size_t from, std::size_t to) noexcept { m_cells[(from * m_size) + to] = true; }

    /// Transitive closure (Floyd-Warshall); phases hold tens of systems, so cubic is fine.
    [[nodiscard]] auto closure() const -> Relation {
        Relation reach = *this;
        for (std::size_t middle = 0; middle < m_size; ++middle) {
            for (std::size_t from = 0; from < m_size; ++from) {
                if (!reach.has(from, middle)) {
                    continue;
                }
                for (std::size_t to = 0; to < m_size; ++to) {
                    if (reach.has(middle, to)) {
                        reach.set(from, to);
                    }
                }
            }
        }
        return reach;
    }

private:
    std::size_t m_size;
    std::vector<bool> m_cells;
};

/// The shortest cycle through `start` (breadth-first), as positions from `start` back to it.
[[nodiscard]] auto shortestCycle(const Relation& edges, std::size_t start) -> std::vector<std::size_t> {
    constexpr std::size_t kUnvisited = std::numeric_limits<std::size_t>::max();
    std::vector<std::size_t> parent(edges.size(), kUnvisited);
    std::deque<std::size_t> frontier{start};
    std::size_t last = kUnvisited; // the node whose edge closes the cycle
    while (!frontier.empty() && last == kUnvisited) {
        const std::size_t node = frontier.front();
        frontier.pop_front();
        for (std::size_t next = 0; next < edges.size() && last == kUnvisited; ++next) {
            if (!edges.has(node, next)) {
                continue;
            }
            if (next == start) {
                last = node;
            } else if (parent[next] == kUnvisited) {
                parent[next] = node;
                frontier.push_back(next);
            }
        }
    }
    std::vector<std::size_t> path{start};
    for (std::size_t node = last; node != start && node != kUnvisited; node = parent[node]) {
        path.push_back(node);
    }
    std::ranges::reverse(path.begin() + 1, path.end());
    path.push_back(start);
    return path;
}

} // namespace

auto Schedule::addSystem(const SystemDesc& desc, std::unique_ptr<System> system) -> SystemId {
    m_isBuilt = false;
    const SystemId id{narrow<std::uint32_t>(m_entries.size())};
    m_entries.push_back(Entry{
        .system = std::move(system),
        .name = desc.name,
        .phase = desc.phase,
        .after = {desc.after.begin(), desc.after.end()},
        .before = {desc.before.begin(), desc.before.end()},
    });
    return id;
}

auto Schedule::build() -> Result<void, ScheduleError> {
    m_isBuilt = false;
    for (const Phase phase : m_order.keys()) {
        if (auto built = buildPhase(phase); !built) {
            return built;
        }
    }
    m_isBuilt = true;
    return {};
}

auto Schedule::buildPhase(Phase phase) -> Result<void, ScheduleError> {
    // The phase's systems in declaration order, and each system's position among them.
    std::vector<SystemId> members;
    std::vector<std::optional<std::size_t>> positionOf(m_entries.size());
    for (std::size_t index = 0; index < m_entries.size(); ++index) {
        if (m_entries[index].phase == phase) {
            positionOf[index] = members.size();
            members.push_back(SystemId{narrow<std::uint32_t>(index)});
        }
    }
    const std::size_t count = members.size();
    Relation edges{count};

    // Explicit constraints. One side is always a system of this phase.
    const auto constrain = [&](SystemId first, SystemId second) -> Result<void, ScheduleError> {
        const auto firstIndex = std::size_t{std::to_underlying(first)};
        const auto secondIndex = std::size_t{std::to_underlying(second)};
        if (firstIndex >= m_entries.size() || secondIndex >= m_entries.size()) {
            return fail(ScheduleError::UnknownSystem,
                        std::format("a before/after constraint in phase {} names system {}", enumName(phase),
                                    std::max(firstIndex, secondIndex)));
        }
        const Entry& earlier = m_entries[firstIndex];
        const Entry& later = m_entries[secondIndex];
        if (earlier.phase == later.phase) {
            edges.set(*positionOf[firstIndex], *positionOf[secondIndex]);
        } else if (earlier.phase > later.phase) {
            return fail(ScheduleError::OrderAcrossPhases,
                        std::format("'{}' ({}) cannot run before '{}' ({})", earlier.name.text(),
                                    enumName(earlier.phase), later.name.text(), enumName(later.phase)));
        }
        return {}; // across phases in phase order: holds by itself
    };
    for (const SystemId member : members) {
        const Entry& entry = m_entries[std::to_underlying(member)];
        for (const SystemId other : entry.after) {
            if (auto constrained = constrain(other, member); !constrained) {
                return constrained;
            }
        }
        for (const SystemId other : entry.before) {
            if (auto constrained = constrain(member, other); !constrained) {
                return constrained;
            }
        }
    }

    Relation reach = edges.closure();
    for (std::size_t position = 0; position < count; ++position) {
        if (!reach.has(position, position)) {
            continue;
        }
        const std::vector<std::size_t> path = shortestCycle(edges, position);
        std::string cycle;
        for (const std::size_t step : path) {
            cycle += std::format("{}{}", cycle.empty() ? "" : " -> ",
                                 m_entries[std::to_underlying(members[step])].name.text());
        }
        return fail(ScheduleError::Cycle, std::format("{} (phase {})", cycle, enumName(phase)));
    }

    // Conflicting systems that no constraint orders run in declaration order. An edge is only added
    // between unordered systems, so it never closes a cycle.
    for (std::size_t first = 0; first < count; ++first) {
        for (std::size_t second = first + 1; second < count; ++second) {
            const SystemAccess& firstAccess = m_entries[std::to_underlying(members[first])].system->access();
            const SystemAccess& secondAccess =
                m_entries[std::to_underlying(members[second])].system->access();
            if (!firstAccess.conflictsWith(secondAccess) || reach.has(first, second) ||
                reach.has(second, first)) {
                continue;
            }
            edges.set(first, second);
            for (std::size_t from = 0; from < count; ++from) {
                if (from != first && !reach.has(from, first)) {
                    continue;
                }
                for (std::size_t to = 0; to < count; ++to) {
                    if (to == second || reach.has(second, to)) {
                        reach.set(from, to);
                    }
                }
            }
        }
    }

    // Direct dependencies, and a run order that takes the earliest declared ready system first.
    std::vector<std::size_t> waitingOn(count, 0);
    for (std::size_t to = 0; to < count; ++to) {
        Entry& entry = m_entries[std::to_underlying(members[to])];
        entry.dependencies.clear();
        for (std::size_t from = 0; from < count; ++from) {
            if (edges.has(from, to)) {
                entry.dependencies.push_back(members[from]);
                ++waitingOn[to];
            }
        }
    }
    std::vector<SystemId>& order = m_order[phase];
    order.clear();
    std::vector<bool> isPlaced(count, false);
    while (order.size() < count) {
        std::size_t next = 0;
        while (isPlaced[next] || waitingOn[next] != 0) {
            ++next;
        }
        isPlaced[next] = true;
        order.push_back(members[next]);
        for (std::size_t to = 0; to < count; ++to) {
            if (edges.has(next, to)) {
                --waitingOn[to];
            }
        }
    }
    return {};
}

auto Schedule::run(Phase phase, jobs::JobSystem& jobSystem) -> CommandStats {
    expects(m_isBuilt, "Schedule::run: build() the schedule after adding systems");
    if (!m_isBuilt) {
        return {};
    }
    const std::vector<SystemId>& order = m_order[phase];

    // Versions are handed out before anything runs, in run order, so what Changed<T> sees does not
    // depend on timing.
    std::vector<SystemRun> runs;
    runs.reserve(order.size());
    for (const SystemId id : order) {
        runs.push_back(SystemRun{
            .world = *m_world,
            .jobs = jobSystem,
            .lastRun = m_entries[std::to_underlying(id)].lastRun,
            .thisRun = m_world->advanceVersion(),
        });
    }

    std::vector<jobs::JobHandle> handles(m_entries.size());
    std::vector<jobs::JobHandle> dependencies;
    for (std::size_t position = 0; position < order.size(); ++position) {
        Entry& entry = m_entries[std::to_underlying(order[position])];
        dependencies.clear();
        for (const SystemId dependency : entry.dependencies) {
            dependencies.push_back(handles[std::to_underlying(dependency)]);
        }
        handles[std::to_underlying(order[position])] = jobSystem.submit(
            {.label = entry.name, .priority = jobs::Priority::Critical, .dependsOn = dependencies},
            [&entry, &run = runs[position]] { entry.system->run(run); });
    }
    for (const SystemId id : order) {
        jobSystem.wait(handles[std::to_underlying(id)]);
    }
    for (std::size_t position = 0; position < order.size(); ++position) {
        m_entries[std::to_underlying(order[position])].lastRun = runs[position].thisRun;
    }

    // Sync point: command buffers in declaration order, whatever thread ran each system.
    CommandStats total;
    for (Entry& entry : m_entries) {
        if (entry.phase == phase) {
            total += entry.system->applyCommands(*m_world);
        }
    }
    return total;
}

auto Schedule::dependenciesOf(SystemId id) const noexcept -> std::span<const SystemId> {
    const auto index = std::size_t{std::to_underlying(id)};
    expects(index < m_entries.size(), "Schedule: unknown system");
    return index < m_entries.size() ? std::span<const SystemId>{m_entries[index].dependencies}
                                    : std::span<const SystemId>{};
}

auto Schedule::nameOf(SystemId id) const noexcept -> std::string_view {
    const auto index = std::size_t{std::to_underlying(id)};
    expects(index < m_entries.size(), "Schedule: unknown system");
    return index < m_entries.size() ? m_entries[index].name.text() : std::string_view{};
}

} // namespace nx::ecs
