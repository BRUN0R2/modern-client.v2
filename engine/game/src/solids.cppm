// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What is solid in a world to the server's traces, and the traces among it, as the original's engine
/// moves a box through it (SV_Move, written anew from ReHLDS, GPL, consulted: docs/20 section 3): what
/// the pushers carry bodies through, what an entity the game drops lands on, and what a weapon's
/// attack meets.
export module nx.game:solids;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.ecs;
import nx.collision;
import :components;

export namespace nx::game {

/// What a server trace meets of the entities, as the original's SV_Move types say.
enum class TraceType : std::uint8_t {
    Normal,     ///< Every solid entity (MOVE_NORMAL, dont_ignore_monsters).
    NoMonsters, ///< The solid brush entities alone (MOVE_NOMONSTERS, ignore_monsters).
};

/// A server trace: of a box `box` about its origin (a line's of none) from `start` to `end`, of `type`,
/// leaving out `ignored` (its passedict).
struct ServerTrace {
    Aabb box{};
    Vec3 start{};
    Vec3 end{};
    TraceType type = TraceType::Normal;
    std::optional<ecs::Entity> ignored{};
};

/// A server trace of `hull`'s box (collision::hullBoxOf) from `start` to `end`, of every solid entity.
[[nodiscard]] auto hullTrace(collision::Hull hull, Vec3 start, Vec3 end) noexcept -> ServerTrace;

/// How a server trace meets an entity of a kind solid as a box (BoxSolidity) when it is not its box
/// alone (SV_ClipMoveToEntity of it), given by what makes it: a studio model's line meets its hitboxes
/// (SV_HullForStudioModel). Called with the box traced about its origin, `traced`, from `start` to
/// `end`.
struct ClipMethod {
    collision::Trace (*clip)(ecs::World& world, ecs::Entity entity, const Aabb& traced, Vec3 start,
                             Vec3 end) = nullptr;
};

/// What a trace through what is solid met: the trace, and the entity that stopped it, none for the
/// world or nothing (the original's TraceResult::pHit).
struct SolidHit {
    collision::Trace trace{};
    std::optional<ecs::Entity> entity{};
};

/// A solid brush entity (Solid::Bsp), where it was when gathered, and the box it is linked by there, which
/// a way that does not meet it does not meet.
struct SolidBrushEntity {
    ecs::Entity entity{};
    collision::ModelIndex model{};
    Vec3 origin{};
    EulerAngles angles{};
    Aabb linked{};
};

/// An entity solid as a box (BoxSolidity), where it was when gathered, and how a trace meets it.
struct SolidBoxEntity {
    ecs::Entity entity{};
    Vec3 origin{};
    Aabb box{};
    ClipMethod clip{};
};

/// The entities solid as boxes of a world, found by the square of the ground each stands in, as the
/// original's area nodes keep them (SV_LinkEdict, SV_ClipToLinks), so that a trace or a move looks only at
/// those about it however many the others are; few, it looks at them all, which is as quick. Each is kept
/// where it is told it is (place), and goes when it is solid no more (forget). Its memory is kept from one
/// gathering to the next.
class SolidBoxes {
public:
    /// None any more.
    void clear() noexcept;
    /// `solid`, which it does not hold.
    void add(const SolidBoxEntity& solid);
    /// Those of `world`, in the world's order, in place of those it held, in jobs if given.
    void gather(ecs::World& world, jobs::JobSystem* jobs = nullptr);
    /// The one of `entity` as it is now, `box` about `origin`; false when it holds none.
    auto place(ecs::Entity entity, Vec3 origin, const Aabb& box) -> bool;
    /// The one of `entity` goes, if it holds it.
    void forget(ecs::Entity entity);

    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return boxes.size(); }

    /// Calls `visit` with each that may meet `area`, which it must weigh itself: all those that do, each
    /// once, and of the others those that stand by them. As many threads may ask as want to while none
    /// changes it.
    template <class Visit>
    void forEachAbout(const Aabb& area, Visit&& visit) const {
        const std::optional<CellSpan> span = indexedSpanOf(area);
        if (!span) {
            for (const SolidBoxEntity& solid : boxes) {
                visit(solid);
            }
            return;
        }
        for (std::size_t row = span->firstRow; row <= span->lastRow; ++row) {
            for (std::size_t column = span->firstColumn; column <= span->lastColumn; ++column) {
                for (std::uint32_t place = cells[(row * kCellsAcross) + column]; place != kNoPlace;
                     place = nextInCell[place]) {
                    visit(boxes[place]);
                }
            }
        }
    }

private:
    /// How wide a square is, how many there are along each side of the 65536 units of the world's
    /// coordinates, and the fewest boxes worth finding by squares.
    static constexpr float kCellSize = 256.0F;
    static constexpr std::size_t kCellsAcross = 256;
    static constexpr std::size_t kLeastIndexed = 256;
    static constexpr std::uint32_t kNoPlace = std::numeric_limits<std::uint32_t>::max();

    /// The squares a look covers.
    struct CellSpan {
        std::size_t firstColumn = 0;
        std::size_t lastColumn = 0;
        std::size_t firstRow = 0;
        std::size_t lastRow = 0;
    };

    /// The squares that hold whatever may meet `area`; none when looking at them all is as quick.
    [[nodiscard]] auto indexedSpanOf(const Aabb& area) const noexcept -> std::optional<CellSpan>;
    [[nodiscard]] static auto cellOf(const SolidBoxEntity& solid) noexcept -> std::size_t;
    void link(std::uint32_t place, std::size_t cell) noexcept;
    void unlink(std::uint32_t place, std::size_t cell) noexcept;
    void buildCells();

    std::vector<SolidBoxEntity> boxes;
    std::vector<std::uint32_t>
        placeOfEntity;                ///< By an entity's index: one past its place in `boxes`, 0 for none.
    std::vector<std::uint32_t> cells; ///< The first box of each square, while they are enough for them.
    std::vector<std::uint32_t> nextInCell; ///< Of each box, the next of its square.
    std::vector<std::uint32_t> cellOfBox;  ///< Of each box as they are gathered; kept for its room.
    float widest = 0.0F;                   ///< How far across from its middle a box goes, the most of them.
};

/// The entities solid in a world to the server's traces, where they were when gathered (solidsOf): for
/// many traces while none moves, which then gather them once instead of once a trace.
struct Solids {
    std::vector<SolidBrushEntity> brushes{};
    SolidBoxes boxes{};
};

/// The entities solid in `world`, whose map's hulls are `hulls`, where they are now, in the world's order.
[[nodiscard]] auto solidsOf(ecs::World& world, const collision::BrushHulls& hulls) -> Solids;

/// The solid brush entities of `world`, whose map's hulls are `hulls`, where they are now, in the world's
/// order: all that a line through them alone meets (lineReaches).
[[nodiscard]] auto solidBrushesOf(ecs::World& world, const collision::BrushHulls& hulls)
    -> std::vector<SolidBrushEntity>;

/// The server's way of `trace` through what is solid in `world`, whose map's hulls are `hulls`, as the
/// original's game traces a line or a hull (UTIL_TraceLine, UTIL_TraceHull: SV_Move): the world's
/// first, through the hull the box's size chooses (collision::boxHullOf); when it stops at once, that
/// alone; else each solid entity's of its type, but the one left out, whose linked box the way's box
/// meets (SV_MoveBounds, SV_ClipToLinks), from `start` to where the world stopped it, as it chooses
/// (a brush entity through its model's hull, an entity solid as a box through its box grown by the one
/// traced, or as its ClipMethod says), kept when it stops it sooner, starts in solid or is all in it
/// (collision::keepFirstStop), none looked at once all in solid; its fraction then of the whole way.
[[nodiscard]] auto traceSolidsHit(ecs::World& world, const collision::BrushHulls& hulls,
                                  const ServerTrace& trace) -> SolidHit;
/// traceSolidsHit's trace through `solids`, the entities solid in `world` gathered while none has moved
/// since (solidsOf).
[[nodiscard]] auto traceSolidsHit(ecs::World& world, const collision::BrushHulls& hulls, const Solids& solids,
                                  const ServerTrace& trace) -> SolidHit;

/// The same through `brushes` and `boxes`.
[[nodiscard]] auto traceSolidsHit(ecs::World& world, const collision::BrushHulls& hulls,
                                  std::span<const SolidBrushEntity> brushes, const SolidBoxes& boxes,
                                  const ServerTrace& trace) -> SolidHit;

/// Whether traceSolidsHit's trace of a line from `start` to `end`, of the brush entities alone
/// (TraceType::NoMonsters), through `brushes`, goes the whole way, as many threads as ask while nothing
/// moves: the world stopping it, or a brush entity, ends it at once.
[[nodiscard]] auto lineReaches(const collision::BrushHulls& hulls, std::span<const SolidBrushEntity> brushes,
                               Vec3 start, Vec3 end) noexcept -> bool;

/// traceSolidsHit's trace of `hull` from `start` to `end` (hullTrace).
[[nodiscard]] auto traceSolids(ecs::World& world, const collision::BrushHulls& hulls, collision::Hull hull,
                               Vec3 start, Vec3 end) -> collision::Trace;

} // namespace nx::game
