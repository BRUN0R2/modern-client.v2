// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Intent and perception memory, independent of the body's kind (ADRs 0024 and 0028), and the decisions
/// of many brains at once, in batches in jobs.
export module nx.agents;
import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.jobs;
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

/// How a brain perceives and decides, a kind's settings.
struct Rules {
    /// How long a target unseen is remembered.
    Seconds memoryLifetime{};
    /// How near a target it sees must be to be attacked.
    HammerUnits attackDistance{};
    /// How near the corridor's end counts as arrived.
    HammerUnits arrivalDistance{};
    /// How far a goal moves, or the agent after a failed path, before another path is searched.
    HammerUnits replanDistance{};
    /// How many targets are remembered at once, the one seen longest ago forgotten first.
    std::size_t memoryLimit = 0;
    /// How long after it first sees a target it knows of it: none before then pursues it (a CS bot's
    /// reaction time). None, at once.
    Seconds reactionTime{};
};

/// A sensed fact, never a target's position obtained through a wall.
struct Observation {
    ecs::Entity entity{};
    Vec3 position{};
    std::optional<Vec3> aimAt{}; ///< The observed aim point, distinct from the navigation destination.
};

/// What a brain remembers of a target: its last observation, when, when it first saw it since it
/// remembers it, whether it sees it now, and whether its reaction time since it first saw it went by.
struct Memory {
    Observation observation{};
    GameTime seenAt{};
    GameTime noticedAt{};
    bool isVisible = false;
    bool isRecognized = false;
};

/// What a decision asks of a body, as a player's command would: where to move, where to look, whether
/// to attack.
struct Intent {
    Vec3 direction{}; ///< A unit direction on the ground plane, or zero.
    std::optional<Vec3> lookAt{};
    bool attacks = false;
};

/// A brain's decision of a tick: its intent, its behavior, the target it pursues (none for its goal or
/// for nothing) and, Blocked, why no path was found.
struct Decision {
    Intent intent{};
    Behavior behavior = Behavior::Idle;
    std::optional<ecs::Entity> target{};
    std::optional<navigation::NavigationError> navigationError{};
};

/// Bounded perception memory and a corridor, never a body's position or health. The mesh used by
/// decide must outlive the brain's cached corridor.
class Brain {
public:
    /// Requires positive distances, a positive memory lifetime, a nonzero memory budget and a reaction
    /// time of none or more.
    explicit Brain(Rules rules);
    /// Each tick replaces visibility and forgets expired records; fresh observations update memory, a
    /// target seen anew noticed now; the targets noticed at least the reaction time ago are recognized.
    void perceive(std::span<const Observation> seen, GameTime now, TickRate rate);
    /// Forgets every target it remembers that is not among `present`, sorted: the targets there still are
    /// to perceive, so that it pursues none that died or left, and plans no way to where one was last.
    void forgetAbsent(std::span<const ecs::Entity> present);
    /// A deterministic choice among the targets recognized: closest visible target, else freshest
    /// remembered target. Ties retain memory order. A failed path is attempted again only when its goal or
    /// source moves sufficiently. Smaller observation changes update the corridor's destination without
    /// another full search. Query/path failures become Blocked decisions carrying their NavigationError.
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

/// A brain to decide with, and where its body's feet are this tick (Deciders::decide).
struct DecisionRequest {
    Brain* brain = nullptr;
    Vec3 feet{};
};

/// The fewest brains a batch of Deciders decides: a decision costs some microseconds on a large map
/// (nxBenchNavigation's agentDecisions), so that a batch outweighs handing it to a worker; fewer brains
/// decide on the calling thread alone.
inline constexpr std::size_t kSmallestBatch = 8;

/// Decides for many brains on one mesh at once (ADR-0023's queries in batches in jobs): in batches of at
/// least kSmallestBatch, at most one for each thread that runs jobs, each batch with a query of its own,
/// so that batches run side by side. Each decision is written at its request's index, the same with any
/// number of workers (docs/04, section 8): a brain reads and writes only its own state and the mesh.
class Deciders {
public:
    /// Deciders on `mesh`, which must outlive them and the corridors of the brains they decide for.
    explicit Deciders(const navigation::NavigationMesh& mesh) noexcept;

    /// Each request's brain decides at its feet (Brain::decide), its decision written at its index of
    /// `decisions`, which must be as long, in jobs of `jobs` of the priority Critical, which the calling
    /// thread runs too while it waits for them. The brains must be distinct, and none of them used by
    /// another thread meanwhile.
    void decide(jobs::JobSystem& jobs, std::span<const DecisionRequest> requests,
                std::span<Decision> decisions);

private:
    const navigation::NavigationMesh* decidedOn;
    std::vector<navigation::NavigationQuery> queries; ///< One for each batch, made as batches need them.
};

/// The brain component attached to the same generational entity as its body. Mod-specific rules
/// choose its observations; the shared brain owns its memory and corridor.
struct Agent {
    Brain brain;
    Behavior behavior = Behavior::Idle;
    std::size_t nextCandidate = 0;
};

} // namespace nx::agents
