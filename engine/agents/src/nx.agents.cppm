// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Intent and perception memory, independent of the body's kind (ADRs 0024 and 0028).
export module nx.agents;
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.navigation;

export namespace nx::agents {

/// What a brain's decision does.
enum class Behavior : std::uint8_t {
    Idle,        ///< Nothing to pursue, or arrived where a target was last observed or at its goal.
    Chase,       ///< Moves along its corridor towards a target it sees.
    Attack,      ///< Attacks a target it sees within its attack distance.
    Investigate, ///< Moves along its corridor to where a target it no longer sees was last observed.
    Blocked,     ///< No path to its target or its goal: the decision carries the navigation's error.
    MoveTo,      ///< Moves along its corridor to the goal it was given, with no target to pursue.
};

struct Rules {
    Seconds memoryLifetime{};
    HammerUnits attackDistance{};
    HammerUnits arrivalDistance{};
    HammerUnits replanDistance{};
    std::size_t memoryLimit = 0;
};

/// A sensed fact, never a target's position obtained through a wall.
struct Observation {
    ecs::Entity entity{};
    Vec3 position{};
    std::optional<Vec3> aimAt{}; ///< The observed aim point, distinct from the navigation destination.
};

struct Memory {
    Observation observation{};
    GameTime seenAt{};
    bool isVisible = false;
};

struct Intent {
    Vec3 direction{}; ///< A unit direction on the ground plane, or zero.
    std::optional<Vec3> lookAt{};
    bool attacks = false;
};

struct Decision {
    Intent intent{};
    Behavior behavior = Behavior::Idle;
    std::optional<navigation::NavigationError> navigationError{};
};

/// Bounded perception memory and a corridor, never a body's position or health. The mesh used by
/// decide must outlive the brain's cached corridor.
class Brain {
public:
    /// Requires positive distances, a positive memory lifetime and a nonzero memory budget.
    explicit Brain(Rules rules);
    /// Each tick replaces visibility and forgets expired records; fresh observations update memory.
    void perceive(std::span<const Observation> seen, GameTime now, TickRate rate);
    /// A deterministic choice: closest visible target, else freshest remembered target. Ties retain
    /// memory order. A failed path is attempted again only when its goal or source moves sufficiently.
    /// Smaller observation changes update the corridor's destination without another full search.
    /// Query/path failures become Blocked decisions carrying their NavigationError.
    /// Investigations finish at the corridor's projected destination, including an airborne observation.
    /// With no target remembered, it moves to its goal (MoveTo), which it forgets as it arrives
    /// (Idle).
    [[nodiscard]] auto decide(Vec3 feet, navigation::NavigationQuery& query) -> Decision;
    /// Gives it `goal`, a point of the mesh to move to when it has no target to pursue; none forgets it.
    inline void setGoal(std::optional<Vec3> goal) noexcept { givenGoal = goal; }
    /// The goal it was given and has not reached yet.
    [[nodiscard]] inline auto goal() const noexcept -> std::optional<Vec3> { return givenGoal; }
    [[nodiscard]] inline auto memories() const noexcept -> std::span<const Memory> { return memory; }

private:
    Rules rules;
    std::vector<Memory> memory;
    std::optional<ecs::Entity> target{};
    std::optional<Vec3> givenGoal{};
    std::optional<Vec3> plannedGoal{};
    std::optional<Vec3> plannedFrom{};
    std::optional<navigation::Corridor> corridor{};
    std::optional<navigation::NavigationError> pathError{};
};

/// The brain component attached to the same generational entity as its body. Mod-specific rules
/// choose its observations; the shared brain owns its memory and corridor.
struct Agent {
    Brain brain;
    Behavior behavior = Behavior::Idle;
    std::size_t nextCandidate = 0;
};

} // namespace nx::agents
