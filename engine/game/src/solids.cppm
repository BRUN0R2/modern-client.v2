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

/// traceSolidsHit's trace of `hull` from `start` to `end` (hullTrace).
[[nodiscard]] auto traceSolids(ecs::World& world, const collision::BrushHulls& hulls, collision::Hull hull,
                               Vec3 start, Vec3 end) -> collision::Trace;

} // namespace nx::game
