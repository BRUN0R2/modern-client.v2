// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a body leaves the start of a link over a gap (docs/adr/0023-navigation.md): the least speed across its
/// flight takes, from the ground the mesh tells of the start (Takeoff), and the most a body leaves that
/// ground at, by how it moves. A way goes over no gap whose flight takes more than its body leaves the start
/// at (PathRequest::body), and a brain runs at one from as far back as the speed it leaves at takes.
export module nx.navigation:flight;

import std;
import nx.core;
import nx.math;
import :profile;
import :mesh;

export namespace nx::navigation {

/// How a body moves across, as its game moves it: how fast it runs at most, standing and crouched; what it
/// gains across in the air, wishing for no more speed there (PM_AirAccelerate's cap); how fast a run gains it
/// speed, in shares of the run a second (sv_accelerate), none for a body at its full speed at once; and how
/// fast its friction takes speed, in shares of its speed a second (sv_friction), none for none.
struct BodyMotion {
    UnitsPerSecond runSpeed{};
    UnitsPerSecond crouchedSpeed{};
    UnitsPerSecond airSpeed{};
    float acceleration = 0.0F;
    float braking = 0.0F;

    [[nodiscard]] inline auto operator==(const BodyMotion&) const noexcept -> bool = default;
};

/// Whether `motion` is one a way is searched for: each of its numbers finite and none below none.
[[nodiscard]] auto isValidMotion(const BodyMotion& motion) noexcept -> bool;

/// How fast across a body of `profile` leaves the start of `link`, up to `shortOfStart` short of it, for its
/// box to come down over the floor at its end: over the gap between them, what its two sides do not span, by
/// the time it is in the air, rising as high as the link's kind jumps, or as the ground at its start lets it
/// (Takeoff::rise), its head stopping it there, then falling to the end's height. None for a ladder, a door,
/// or a link with no gap; more than any body's for one whose end is higher than a body rises from its start.
/// A link that tells nothing of its start is left as from open ground, a full jump high. Requires a profile
/// of a gravity and jump heights finite and more than none, as checkProfile has it.
[[nodiscard]] auto leastLeapSpeedOf(const MeshLink& link, const AgentProfile& profile,
                                    HammerUnits shortOfStart) noexcept -> UnitsPerSecond;

/// How fast across a body of `motion`, of `profile`'s size, leaves the start of `link` at most: as fast as it
/// gains running at it from rest over the floor behind it (Takeoff::room) but for its radius, at its run
/// there, its crouched one where only a crouched body stands (Takeoff::isCrouched), or what the air gives it,
/// if more. From open ground, as a link that tells nothing of its start is left, its run or what the air
/// gives it. Requires a valid motion (isValidMotion).
[[nodiscard]] auto fastestTakeoffOf(const MeshLink& link, const AgentProfile& profile,
                                    const BodyMotion& motion) noexcept -> UnitsPerSecond;

/// How far behind the start of `link` a body of `motion`, of `profile`'s size, starts running from rest to
/// leave it at `takeoff`: its radius, and as far as its run gains it that speed while its friction takes what
/// the speed it has gives it (PM_Accelerate, PM_Friction), counted at that speed; its radius alone for a body
/// at its full speed at once. None for a takeoff faster than its run there, crouched where only a crouched
/// body stands, or one its friction takes as much of as its run gains it. Requires a valid motion
/// (isValidMotion).
[[nodiscard]] auto runUpOf(const MeshLink& link, const AgentProfile& profile, const BodyMotion& motion,
                           UnitsPerSecond takeoff) noexcept -> std::optional<HammerUnits>;

} // namespace nx::navigation
