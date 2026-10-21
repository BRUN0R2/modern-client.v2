// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Player movement (docs/07-simulation-and-physics.md, section 3): one user command applied to a
/// player, the same on the server and in the client's prediction. It moves as the original's
/// pmove moves, as the GPL code that reconstructs Counter-Strike describes it, written anew
/// (docs/20-legal-and-licensing.md, section 3): its order of steps, its thresholds and the
/// precision of each sum, which change where a player ends up.
///
/// Today: walking, the air, jumping and its penalties, stairs, sliding along walls and floors, and
/// water. Ducking, ladders, noclip, observers, unsticking and what is not the world (entities,
/// conveyors, currents) come next (TODO.md).
export module cs.shared:movement;

import std;
import nx.core;
import nx.math;
import nx.collision;

export namespace cs {

/// The buttons of a user command that movement reads.
enum class Button : std::uint8_t {
    Jump,
    Count,
};

/// How a player asks to move: the speeds its keys ask for along its view (cl_forwardspeed and the
/// others), before movement caps them.
struct MoveIntent {
    nx::UnitsPerSecond forward{};
    nx::UnitsPerSecond side{}; ///< Positive to the right.
    nx::UnitsPerSecond up{};   ///< Read in water.
};

/// What a client asks for one stretch of its time (docs/07, section 2.1). Today the fields movement
/// reads; the rest arrive with what reads them.
struct UserCommand {
    nx::Milliseconds duration{}; ///< The stretch of time the command moves the player through.
    nx::EulerAngles viewAngles{};
    MoveIntent move{};
    nx::Flags<Button> buttons{};
};

/// The movement cvars (docs/07, section 3.1), which the server replicates to its clients. The
/// defaults are those Counter-Strike plays with.
struct MovementSettings {
    nx::UnitsPerSecondSquared gravity{800.0F}; ///< sv_gravity.
    nx::PerSecond friction{4.0F};              ///< sv_friction, on the ground and in water.
    nx::Scale edgeFriction{2.0F};              ///< edgefriction: friction's multiple near a drop.
    nx::UnitsPerSecond stopSpeed{75.0F};       ///< sv_stopspeed: slower, friction stops as if at it.
    nx::PerSecond accelerate{5.0F};            ///< sv_accelerate, on the ground and in water.
    nx::PerSecond airAccelerate{10.0F};        ///< sv_airaccelerate.
    nx::UnitsPerSecond maxSpeed{320.0F};       ///< sv_maxspeed.
    nx::UnitsPerSecond maxVelocity{2000.0F};   ///< sv_maxvelocity, on each axis.
    nx::HammerUnits stepSize{18.0F};           ///< sv_stepsize: the highest stair walked up.
    nx::Degrees rollAngle{0.0F};               ///< sv_rollangle: the roll of a sideways move.
    nx::UnitsPerSecond rollSpeed{0.0F};        ///< sv_rollspeed: sideways, the speed of a full roll.
};

/// How deep a player is in water, by which of its feet, its middle and its eyes are in.
enum class WaterLevel : std::uint8_t {
    Dry,
    Feet,
    Waist,
    Eyes,
};

/// The height of a standing player's eyes above its origin.
inline constexpr nx::HammerUnits kStandingViewHeight{17.0F};

/// A player between commands: what one command reads and leaves for the next.
struct MovementState {
    nx::Vec3 origin{}; ///< The center of its standing box.
    nx::Vec3 velocity{};
    /// Its own speed limit, that of the weapon it holds; zero leaves the server's (sv_maxspeed).
    nx::UnitsPerSecond maxSpeed{};
    bool isOnGround = false;
    WaterLevel waterLevel = WaterLevel::Dry;
    nx::collision::Contents waterType = nx::collision::Contents::Empty; ///< What it is in, when wet.
    /// Left of the slowdown a jump costs: the next jump is lower and walking slower while it lasts.
    nx::Milliseconds jumpPenalty{};
    /// Left of a jump out of water, which pushes the player at waterJumpVelocity meanwhile.
    nx::Milliseconds waterJumpTime{};
    nx::Vec3 waterJumpVelocity{};
    /// The jump button was held at the end of the last command: holding it does not jump again.
    bool isJumpHeld = false;
};

/// `state` after `command`, under `settings`, in the world whose hulls are model 0 of `world`. Pure:
/// the result depends on the arguments alone.
[[nodiscard]] auto simulateMovement(const MovementState& state, const UserCommand& command,
                                    const MovementSettings& settings, const nx::collision::BrushHulls& world)
    -> MovementState;

} // namespace cs
