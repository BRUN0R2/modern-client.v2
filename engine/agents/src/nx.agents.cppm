// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Intent and perception memory, independent of the body's kind (ADRs 0024 and 0028), the watch that gets
/// a stuck body out, and the decisions of many brains at once, in batches in jobs.
export module nx.agents;
import std;
import nx.core;
import nx.meta;
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

/// Whether a decision of `behavior` moves along the brain's corridor: Chase, Investigate and MoveTo.
[[nodiscard]] constexpr auto followsCorridor(Behavior behavior) noexcept -> bool {
    return behavior == Behavior::Chase || behavior == Behavior::Investigate || behavior == Behavior::MoveTo;
}

/// How a brain perceives and decides, a kind's settings.
struct Rules {
    /// How long a target unseen is remembered: from when it was last perceived, or from when the brain last
    /// fought (saw a target it recognized), the later, so that what it perceived while it fought waits for
    /// it to look into once the fight is over.
    Seconds memoryLifetime{};
    /// How near a target it sees must be to be attacked.
    HammerUnits attackDistance{};
    /// How near the corridor's end counts as arrived.
    HammerUnits arrivalDistance{};
    /// How far a goal moves, or the agent after a failed path, before another path is searched.
    HammerUnits replanDistance{};
    /// How far a goal moves before another path is searched, in shares of how far it is across, when that
    /// is more than replanDistance: a far goal that moves a little keeps its way, the corridor's end slid
    /// to it along the mesh, as DetourCrowd adjusts a target without a search (adjustMoveTarget). None,
    /// replanDistance alone.
    float replanShare = 0.0F;
    /// How long where a target was placed holds against an estimate of it less salient, or as salient and
    /// sensed farther (Condition Zero's short-term memory of a noise, OnEvent's 3 seconds), so that its way
    /// and its eyes do not jump at each step heard. None, every estimate moves it.
    Seconds shortTermMemory{};
    /// How recent a high-salience estimate is a threat that draws a brain off the target it lost sight of,
    /// and how much nearer than it it must be (CanHearNearbyEnemyGunfire: 0.5 seconds, 100 units).
    Seconds threatRecency{};
    HammerUnits threatMargin{};
    /// How many targets are remembered at once, the one seen longest ago forgotten first.
    std::size_t memoryLimit = 0;
    /// How long after it first sees a target it knows of it: none before then pursues it (a CS bot's
    /// reaction time). None, at once.
    Seconds reactionTime{};
    /// How fast its body loses speed with nothing driving it on, in shares of it a second (the ground's
    /// friction: sv_friction's 4), and below stopSpeed, as fast as it would at stopSpeed (PM_Friction), so
    /// that the speed it can still stop from short of a point is the root of twice that slowing times how
    /// far the point is, and farther than it stops from stopSpeed in, stopSpeed and braking times how much
    /// farther. Its body cannot turn on the spot: it slows down by it for the end of a way it stops at and
    /// for a corner, the more the sharper the corner turns, rather than overshoot them and circle back.
    /// None, it never slows (a body that stops and turns at once).
    float braking = 0.0F;
    /// The speed under which its body loses speed as fast as at it (sv_stopspeed's 75), which slows down a
    /// slow body, and a body at rest a walk slower than braking times it over its acceleration cannot start
    /// at. None, its friction takes a share of its speed alone however slow it goes.
    UnitsPerSecond stopSpeed{};
    /// How fast a walk gains its body speed, in shares of the walk a second (sv_accelerate's 5): below its
    /// stop speed its friction takes stopSpeed times braking a second, so that a walk slower than that over
    /// acceleration gains it nothing, and it walks no slower than twice that, the slowest walk it gains as
    /// fast as its friction would stop it from, but to stop where its way ends. None, at any walk.
    float acceleration = 0.0F;
};

/// A sensed fact: where a target was seen, touched, or roughly where it was heard (Brain::sense), never a
/// position no sense gave, as a target's through a wall it makes no noise behind.
struct Observation {
    ecs::Entity entity{};
    Vec3 position{};
    std::optional<Vec3> aimAt{}; ///< The observed aim point, distinct from the navigation destination.
};

/// How much what a sense tells of where a target is matters, as Condition Zero ranks the noises a bot hears
/// (PriorityType: PRIORITY_LOW, PRIORITY_MEDIUM, PRIORITY_HIGH), and Certain for what is seen or touched.
enum class Salience : std::uint8_t {
    Low,
    Medium,
    High,
    Certain,
    Count,
};

/// What a sense but sight tells a brain of a target (Brain::sense): the observation, how salient it is,
/// and how far across from the agent it was sensed.
struct Sensed {
    Observation observation{};
    Salience salience = Salience::Low;
    HammerUnits distance{};
};

/// What a brain remembers of a target: its last observation, when it perceived it last (seen or sensed),
/// when it first perceived it since it remembers it, whether it sees it now, and whether its reaction
/// time since it first perceived it went by; and how salient and how far what placed it where it is was,
/// and when it was placed there (Rules::shortTermMemory).
struct Memory {
    Observation observation{};
    GameTime perceivedAt{};
    GameTime noticedAt{};
    bool isVisible = false;
    bool isRecognized = false;
    Salience salience = Salience::Certain;
    HammerUnits distance{};
    GameTime placedAt{};
    /// Unseen, placed by a high-salience estimate within threatRecency: a threat (Rules::threatRecency).
    bool isThreat = false;
};

/// A point ahead on an agent's way that it slows down for (Rules::braking): how far along its way it is,
/// and the share of its full speed it may pass there, none where its way ends at a stop.
struct Approach {
    HammerUnits distance{};
    float passShare = 1.0F;
};

/// The most points ahead an agent slows down for: as many as the corners it sees ahead.
inline constexpr std::size_t kMostApproaches = 4;

/// What a decision asks of a body, as a player's command would: where to move, where to look, whether
/// to attack or to jump; and what ahead on its way it slows down for, which steering heeds.
struct Intent {
    Vec3 direction{};        ///< A unit direction on the ground plane, or zero.
    float speedShare = 1.0F; ///< How much of its full speed it moves at along direction, more than 0 to 1.
    /// The way to drive a body that gains speed only along the way it is driven and loses the rest to
    /// friction (a GoldSrc body: PM_Accelerate, PM_Friction), a unit direction on the ground plane: the way
    /// its velocity must change to become the one it steered to, so that it cancels its drift across its way
    /// rather than circle round what it heads for; none to drive it along direction.
    std::optional<Vec3> drive{};

    /// The way to drive a body that gains speed only along the way it is driven: drive, else direction.
    [[nodiscard]] inline auto drivenWay() const noexcept -> Vec3 { return drive.value_or(direction); }
    /// The way on along its corridor, a unit direction on the ground plane, or zero: where it faces with
    /// nothing to look at, however it steers or wiggles (SetLookAngles of m_lookAheadAngle).
    Vec3 ahead{};
    std::optional<Vec3> lookAt{};
    bool attacks = false;
    bool jumps = false;
    /// What ahead on its way it slows down for, the nearest first, approachCount of them: each corner it
    /// sees ahead with the way on after it, passed at the cosine of half the way's turn there (straight on
    /// at full speed, a right angle at 0.71 of it, turning back at none); and where its way ends, when it
    /// stops there (MoveTo, Investigate: not a target it chases), passed at none.
    std::array<Approach, kMostApproaches> approaches{};
    std::size_t approachCount = 0;

    /// The points it approaches.
    [[nodiscard]] inline auto approached() const noexcept -> std::span<const Approach> {
        return std::span{approaches}.first(approachCount);
    }
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
    /// After perceive of the same tick, remembers where each target of `sensed`, perceived by a sense but
    /// sight (heard roughly where it made a noise: cs::heardBy; touched where it is: cs::touchedBy), was
    /// perceived now, unless it sees it, its sight the better. Within shortTermMemory of where it was placed,
    /// an estimate less salient, or as salient and sensed no nearer, leaves it there, and it was perceived
    /// now all the same; a certain one, touched, always places it. A target sensed anew is noticed now, and
    /// recognized once its reaction time went by, as one seen. It investigates a target it sensed as one it
    /// lost sight of: it turns to look where it sensed it, and goes there.
    void sense(std::span<const Sensed> sensed, GameTime now, TickRate rate);
    /// Forgets every target it remembers that is not among `present`, sorted: the targets there still are
    /// to perceive, so that it pursues none that died or left, and plans no way to where one was last.
    void forgetAbsent(std::span<const ecs::Entity> present);
    /// A deterministic choice among the targets recognized: the closest it sees; else the one it pursued
    /// last, lost from sight (AttackState chases its last known position), unless a threat (Memory::isThreat)
    /// is threatMargin nearer than it; else the freshest remembered. Ties retain memory order. A failed path
    /// is attempted again only when its goal or source moves sufficiently. Observation changes smaller than
    /// replanDistance, or than replanShare of how far the target is, update the corridor's destination
    /// without another full search; a body farther across from its corridor than the replan distance (spawned
    /// again, teleported, pushed off the mesh) searches its way anew from where it is. Query/path failures
    /// become Blocked decisions carrying their NavigationError. Investigations finish at the corridor's
    /// projected destination, including an airborne observation. With no target remembered, it moves to its
    /// goal (MoveTo), which it forgets as it arrives (Idle): within arrivalDistance of its corridor's end
    /// across, and a step of its height, as it measures how near a corner is; a corner it reached it heads
    /// on from, however far it drifts from it before its corridor drops it. Its feet within
    /// arrivalDistance of the start of
    /// a link of its corridor (navigation::MeshLink), it crosses it: it jumps once for a link up, and heads
    /// for the link's end, which its corridor goes on from, until it stands within a step of the end's height
    /// as far from the start as the end is; across it, it searches no way, as DetourCrowd plans none for an
    /// agent over a link, the corridor's end following its goal along the mesh.
    [[nodiscard]] auto decide(Vec3 feet, navigation::NavigationQuery& query) -> Decision;
    /// Steers `intent`, a decision's that moves, along its corridor among `nearby`, the bodies about it,
    /// the nearest first, and the walls of the mesh (navigation::Corridor::steer): its body moving at
    /// `velocity`, at `maxSpeed` at most, and no faster than it can still slow down for what the intent
    /// approaches (Rules::braking): each approached point's pass share of `maxSpeed`, and the speed it can
    /// still stop from short of it by friction (Rules::stopSpeed); no slower than the slowest walk its body
    /// gains speed at (Rules::acceleration), at most `maxSpeed`, but as it stops where its way ends: a walk
    /// slower than its friction lets it start left it where it was. It waits, its direction none, when no way
    /// on is worth a tenth of the speed it asked for. Else it goes the way it steers to, as fast as that; and
    /// a body that gains speed only along the way it is driven is to be driven (Intent::drive) the way its
    /// velocity must change to become the steered one, as DetourCrowd integrates an agent's velocity toward
    /// its desired one, unless it goes within a tenth of how it steers. Across a link, it heads for the
    /// link's end as it decided, off the mesh, as DetourCrowd moves an agent over a link without steering.
    void steer(Intent& intent, Vec3 velocity, UnitsPerSecond maxSpeed,
               std::span<const navigation::SteeringBody> nearby, navigation::NavigationQuery& query);
    /// Gives up its way, its body stuck on it too long (StuckWatch), and searches another from where it is:
    /// around `stuckAt`, a place its ways keep away from while it pursues what it pursues, as long as it
    /// keeps away from fewer than `reroutes` places. Past them, it gives up what it pursues as well: forgets
    /// its goal when it pursues no target, the target it investigates (UpdatePathMovement's PATH_FAILURE,
    /// after which a hunt picks another place), and keeps away from its last `reroutes` places on its way
    /// to the one it chases.
    void giveUp(const navigation::AvoidedPlace& stuckAt, std::size_t reroutes);
    /// Gives it `goal`, a point of the mesh to move to when it has no target to pursue; none forgets it. A
    /// goal is pursued anew: its ways keep away from none of the places it got stuck at.
    inline void setGoal(std::optional<Vec3> goal) noexcept {
        givenGoal = goal;
        avoided.clear();
    }
    /// The goal it was given and has not reached yet.
    [[nodiscard]] inline auto goal() const noexcept -> std::optional<Vec3> { return givenGoal; }
    /// Appends to `points` the way left along its corridor (navigation::Corridor::appendWay), from where
    /// its last decision left it to its target or its goal, on the mesh of `query`, which must be the one
    /// it decides on; nothing without a corridor. A decision that does not follow it (followsCorridor)
    /// leaves it as it was.
    void appendWay(navigation::NavigationQuery& query, std::vector<Vec3>& points) const;
    [[nodiscard]] inline auto memories() const noexcept -> std::span<const Memory> { return memory; }

private:
    /// Remembers `sensed` of a target it remembers nothing of, perceived now, seen or not, the record
    /// perceived longest ago forgotten first when its memory is full.
    void remember(const Sensed& sensed, GameTime now, bool isVisible);
    /// Whether another way is searched for `goal` from `feet`: with no search yet, or once it moved from
    /// where the last search aimed more than replanDistance, or replanShare of how far it is, the farther.
    [[nodiscard]] auto movedFarEnough(Vec3 feet, Vec3 goal) const noexcept -> bool;
    /// The target it pursues from `feet`, as decide chooses it; none when it recognizes none.
    [[nodiscard]] auto choose(Vec3 feet) const -> const Memory*;
    /// Tells `decision`'s intent what it approaches from `feet` along `ahead`, the corners from the one it
    /// steers to on (Intent::approaches), when it brakes (Rules::braking): the turn at each measured from the
    /// way into it to the way on kTurnSpanRadii of its body's radius past it, so that a corner the mesh
    /// rounds into short turns is slowed for as a whole.
    void approach(Decision& decision, Vec3 feet, std::span<const Vec3> ahead,
                  navigation::NavigationQuery& query);
    /// Crosses the link its corridor reaches next when `feet` are within arrivalDistance of its start
    /// (decide), `decision` heading for its end and jumping for a link up. Returns whether it crosses one.
    [[nodiscard]] auto beginCrossing(Decision& decision, Vec3 feet, navigation::NavigationQuery& query)
        -> bool;
    /// Forgets the corridor it follows, and the link it crosses on it: another is searched.
    void forgetCorridor() noexcept;
    /// Marks which records are threats `now` (Memory::isThreat).
    void markThreats(GameTime now, TickRate rate);

    Rules rules;
    std::vector<Memory> memory;
    std::optional<ecs::Entity> target{};
    std::optional<Vec3> givenGoal{};
    std::optional<Vec3> plannedGoal{};
    std::optional<Vec3> plannedFrom{};
    std::optional<navigation::Corridor> corridor{};
    /// The link of its corridor its body crosses, the corridor gone on from the link's end; none while it
    /// walks along it.
    std::optional<navigation::MeshLink> crossing{};
    /// The corner of its corridor it last reached, which it heads on from (decide).
    std::optional<Vec3> reachedCorner{};
    std::optional<navigation::NavigationError> pathError{};
    /// Where its body got stuck on its way to what it pursues, which its ways keep away from (giveUp).
    std::vector<navigation::AvoidedPlace> avoided{};
    /// The target its last decision pursued, attacked or not; none for a goal or nothing.
    std::optional<ecs::Entity> focus{};
    /// When it last saw a target it recognized, which its memory's lifetime counts from too.
    std::optional<GameTime> foughtAt{};
};

/// A time drawn at random, from its shortest to its longest.
struct TimeRange {
    Seconds shortest{};
    Seconds longest{};
};

/// How an agent finds its body stuck on its way and gets it out, Condition Zero's bot's (StuckCheck,
/// Wiggle, cs_bot_nav.cpp; UpdatePathMovement's giving up, cs_bot_pathfind.cpp): where its feet are is
/// sampled each sampleInterval, and it is stuck when across the last `samples` intervals it went slower
/// than stuckSpeed; stuck, it moves a way drawn among ahead, back and either side for a wiggle's time,
/// jumping within firstJump and then each jumpInterval, until it is unstuckDistance from where it got stuck;
/// stuck giveUpTime, it gives up its way (Brain::giveUp), and searches another on which the polygons
/// within unstuckDistance of where it got stuck cost avoidedCost times their own; stuck `reroutes` times
/// so on its way to what it pursues, it gives up what it pursues.
struct StuckRules {
    Seconds sampleInterval{};
    std::uint32_t samples = 0;
    UnitsPerSecond stuckSpeed{};
    HammerUnits unstuckDistance{};
    TimeRange wiggle{};
    Seconds firstJump{};
    TimeRange jumpInterval{};
    Seconds giveUpTime{};
    float avoidedCost = 1.0F;
    std::uint32_t reroutes = 0;
};

/// Whether a watch may follow `rules`: its interval, samples, speed, distance and give-up time more than
/// none, its first jump none or more, each range's shortest more than none, its longest no shorter, and
/// its avoided cost finite, at least 1.
[[nodiscard]] auto isValid(const StuckRules& rules) noexcept -> bool;

/// What a watch makes of its body's progress (StuckWatch::watch).
enum class Progress : std::uint8_t {
    Moving,  ///< It makes its way, or means to make none.
    Stuck,   ///< Stuck: the intent wiggles it out.
    GivesUp, ///< Stuck too long: its brain gives up where it goes (Brain::giveUp).
};

/// Watches an agent's body make its way along its corridor, and gets it out when it is stuck (StuckRules).
/// How fast it went is how far across its last sample is from its first, over their time, not Condition
/// Zero's mean of each interval's speed, so that a body going back and forth in a doorway is stuck too.
/// The same for the same inputs and draws.
class StuckWatch {
public:
    /// A watch by `rules`, which must be valid (isValid), its wiggles and jumps drawn from `random`.
    StuckWatch(StuckRules rules, Random random);
    /// Watches its body, its feet at `feet`, `elapsed` after it last did, meaning to move along its way when
    /// `moves`, and able to when its `maxSpeed` is more than stuckSpeed (a frozen round's player is not):
    /// else it forgets how it moved (reset). Stuck, it makes `intent` move its wiggle's way at its full
    /// speed, turned from intent.ahead, and jump as its jumps are due.
    [[nodiscard]] auto watch(Intent& intent, Vec3 feet, UnitsPerSecond maxSpeed, bool moves, Seconds elapsed)
        -> Progress;
    /// Forgets how its body moved: none of its samples, not stuck.
    void reset() noexcept;
    [[nodiscard]] inline auto isStuck() const noexcept -> bool { return stuckAt.has_value(); }
    /// Where its body last got stuck, the polygons within unstuckDistance of it costing avoidedCost times
    /// their own: what its ways keep away from as its brain gives its way up (Brain::giveUp).
    [[nodiscard]] auto stuckPlace() const noexcept -> navigation::AvoidedPlace;
    /// How many places its brain keeps away from before it gives up what it pursues (StuckRules::reroutes).
    [[nodiscard]] inline auto reroutes() const noexcept -> std::size_t { return rules.reroutes; }

private:
    /// The ways a wiggle moves, from where it faces: CZ's NavRelativeDirType.
    enum class Way : std::uint8_t { Ahead, Back, Left, Right, Count };

    /// Samples `feet` when its interval went by, `elapsed` after the last watch.
    void sample(Vec3 feet, Seconds elapsed);
    /// Whether the samples span the whole of `samples` intervals, and its body went slower across them than
    /// stuckSpeed.
    [[nodiscard]] auto isTooSlow() const noexcept -> bool;
    /// Makes `intent` wiggle, `elapsed` after the last watch.
    void wiggle(Intent& intent, Seconds elapsed);
    /// `ahead` turned to `way`: itself, back, or a quarter turn to either side.
    [[nodiscard]] static auto turnedTo(Way way, Vec3 ahead) noexcept -> Vec3;
    [[nodiscard]] auto drawn(const TimeRange& range) noexcept -> Seconds;

    StuckRules rules;
    Random random;
    std::vector<Vec3> sampled; ///< The last of where its feet were, oldest first.
    Seconds sinceSample{};
    std::optional<Vec3> stuckAt{};
    Vec3 lastStuckAt{}; ///< Kept as it gives up, which forgets stuckAt.
    Seconds stuckFor{};
    Way way = Way::Ahead;
    Seconds wiggleLeft{};
    Seconds jumpIn{};
};

/// A body agents steer around (Deciders::decide): the entity it is, where its feet are, how it moves, and
/// how far it reaches across from its middle.
struct Body {
    ecs::Entity entity{};
    Vec3 feet{};
    Vec3 velocity{};
    HammerUnits radius{};
};

/// A brain to decide with, and its body this tick (Deciders::decide): where its feet are, how it moves,
/// how fast it may run, the entity it is, none for a body it steers no other body around, and the watch of
/// its progress, none for a body nothing gets out, `elapsed` after its last request.
struct DecisionRequest {
    Brain* brain = nullptr;
    Vec3 feet{};
    Vec3 velocity{};
    UnitsPerSecond maxSpeed{};
    std::optional<ecs::Entity> body{};
    StuckWatch* stuck = nullptr;
    Seconds elapsed{};
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

    /// Each request's brain decides at its feet (Brain::decide), then steers its decision among `bodies`,
    /// those of the tick, when it moves and its request has a speed (Brain::steer): the bodies within the
    /// steering range of it across and a standing height up or down, but its own, the nearest first, ties
    /// by entity. Its watch, last, gets its body out when it is stuck on the way its decision follows
    /// (StuckWatch::watch, followsCorridor), and its brain gives up where it goes when it is stuck too long
    /// (Brain::giveUp). Its decision is written at its index of `decisions`, which must be as long, in jobs
    /// of `jobs` of the priority Critical, which the calling thread runs too while it waits for them. The
    /// brains and the watches must be distinct, and none of them used by another thread meanwhile.
    void decide(jobs::JobSystem& jobs, std::span<const DecisionRequest> requests,
                std::span<Decision> decisions, std::span<const Body> bodies = {});

private:
    const navigation::NavigationMesh* decidedOn;
    std::vector<navigation::NavigationQuery> queries; ///< One for each batch, made as batches need them.
};

/// The brain component attached to the same generational entity as its body, with the watch that gets its
/// body out when it is stuck. Mod-specific rules choose its observations; the shared brain owns its memory
/// and corridor.
struct Agent {
    Brain brain;
    StuckWatch stuck;
    Behavior behavior = Behavior::Idle; ///< Of its brain's last decision.
    std::size_t nextCandidate = 0;

    /// Appends to `points` the way its brain follows (Brain::appendWay) while its last decision follows
    /// its corridor; else nothing.
    void appendWay(navigation::NavigationQuery& query, std::vector<Vec3>& points) const;
};

} // namespace nx::agents

template <>
struct nx::meta::Description<nx::agents::TimeRange> {
    static constexpr std::string_view kName = "nx::agents::TimeRange";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::agents::TimeRange::shortest>("shortest"),
        nx::meta::field<&nx::agents::TimeRange::longest>("longest"),
    };
};

template <>
struct nx::meta::Description<nx::agents::StuckRules> {
    static constexpr std::string_view kName = "nx::agents::StuckRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&nx::agents::StuckRules::sampleInterval>("sampleInterval"),
        nx::meta::field<&nx::agents::StuckRules::samples>("samples"),
        nx::meta::field<&nx::agents::StuckRules::stuckSpeed>("stuckSpeed"),
        nx::meta::field<&nx::agents::StuckRules::unstuckDistance>("unstuckDistance"),
        nx::meta::field<&nx::agents::StuckRules::wiggle>("wiggle"),
        nx::meta::field<&nx::agents::StuckRules::firstJump>("firstJump"),
        nx::meta::field<&nx::agents::StuckRules::jumpInterval>("jumpInterval"),
        nx::meta::field<&nx::agents::StuckRules::giveUpTime>("giveUpTime"),
        nx::meta::field<&nx::agents::StuckRules::avoidedCost>("avoidedCost"),
        nx::meta::field<&nx::agents::StuckRules::reroutes>("reroutes"),
    };
};
