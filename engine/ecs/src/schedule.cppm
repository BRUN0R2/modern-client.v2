// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The scheduler: phases, a dependency graph per phase derived from declared access, and command
/// buffers applied at the end of each phase (docs/03-ecs.md, sections 7 and 8).
export module nx.ecs:schedule;

import std;
import nx.core;
import nx.jobs;
import :commands;
import :system;
import :world;

export namespace nx::ecs {

/// The parts of a tick (and of a client frame), in the order they run.
enum class Phase : std::uint8_t {
    Input,          // apply the user commands received
    PreSimulation,  // timers, entity think, triggers
    Simulation,     // movement, physics, weapons, damage
    Rules,          // serial game rules and synchronous Rook hooks
    Script,         // Rook event dispatch in deterministic order
    PostSimulation, // cleanup
    Replication,    // per-client snapshots (server)
    Presentation,   // interpolation, visual animation, extract (client)
    Count,
};

enum class SystemId : std::uint32_t {};

enum class ScheduleError : std::uint8_t {
    Cycle,             // before/after constraints that loop
    UnknownSystem,     // a constraint names a system this schedule does not have
    OrderAcrossPhases, // a constraint that the phase order contradicts
};

struct SystemDesc {
    jobs::JobLabel name; // the system's job label too
    Phase phase = Phase::Simulation;
    /// Run after these systems (read during add()). Constraints on systems of earlier phases hold
    /// by themselves.
    std::span<const SystemId> after = {};
    /// Run before these systems (read during add()).
    std::span<const SystemId> before = {};
};

/// Systems of one world, grouped by phase. In each phase, systems whose access conflicts run in
/// declaration order, adjusted by before/after; the others may run in parallel. Change versions
/// are handed out and command buffers applied in schedule order, so the outcome does not depend
/// on the number of workers.
class Schedule {
public:
    explicit Schedule(World& world) noexcept : m_world{&world} {}

    /// Adds a system (a function or a non-generic lambda; see :system). Invalidates the build.
    template <class Function>
    auto add(const SystemDesc& desc, Function&& system) -> SystemId {
        using Callable = std::decay_t<Function>;
        return addSystem(desc, detail::makeSystem(*m_world, Callable{std::forward<Function>(system)},
                                                  typename detail::SystemSignature<Callable>::Parameters{}));
    }

    /// Orders every phase. Fails on contradictory before/after constraints.
    [[nodiscard]] auto build() -> Result<void, ScheduleError>;

    /// Runs the systems of `phase`, then applies their command buffers in declaration order. The
    /// schedule must be built.
    auto run(Phase phase, jobs::JobSystem& jobSystem) -> CommandStats;

    /// After build(): the systems `id` waits for in its phase.
    [[nodiscard]] auto dependenciesOf(SystemId id) const noexcept -> std::span<const SystemId>;
    [[nodiscard]] auto nameOf(SystemId id) const noexcept -> std::string_view;
    [[nodiscard]] auto systemCount() const noexcept -> std::size_t { return m_entries.size(); }

private:
    struct Entry {
        std::unique_ptr<System> system;
        jobs::JobLabel name;
        Phase phase;
        std::vector<SystemId> after;
        std::vector<SystemId> before;
        Version lastRun{0};
        std::vector<SystemId> dependencies{}; // filled by build()
    };

    auto addSystem(const SystemDesc& desc, std::unique_ptr<System> system) -> SystemId;
    [[nodiscard]] auto buildPhase(Phase phase) -> Result<void, ScheduleError>;

    World* m_world;
    std::vector<Entry> m_entries;
    EnumArray<Phase, std::vector<SystemId>> m_order; // run order of each phase, filled by build()
    bool m_isBuilt = false;
};

} // namespace nx::ecs
