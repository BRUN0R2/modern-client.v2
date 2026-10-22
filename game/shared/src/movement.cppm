// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Player movement (docs/07-simulation-and-physics.md, section 3): one user command applied to a
/// player, the same on the server and in the client's prediction. It moves as the original's
/// pmove moves, as the GPL code that reconstructs Counter-Strike describes it, written anew
/// (docs/20-legal-and-licensing.md, section 3): its order of steps, its thresholds and the
/// precision of each sum, which change where a player ends up.
///
/// Today: walking, the air, jumping and its penalties, ducking, stairs, sliding along walls and
/// floors, water, and ladders, among the world and the brush entities where the game puts and turns
/// them; noclip; and the weapon's kick to the view, dropped back each move. Observers, unsticking, boxes
/// (players, hostages), conveyors and currents come next (TODO.md).
export module cs.shared:movement;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.game;
import :weapons;

export namespace cs {

/// The buttons of a user command, those movement reads and those the game does.
enum class Button : std::uint8_t {
    Jump,
    Duck,
    Forward, ///< On a ladder, the keys climb by the buttons they hold, not by the speeds they ask.
    Back,
    MoveLeft,
    MoveRight,
    Use,     ///< The game reads it: pressed, the player uses what it looks at.
    Attack,  ///< The game reads it: held, the weapon in hand fires.
    Attack2, ///< The game reads it: held, the weapon in hand switches to its alternate and back.
    Reload,  ///< The game reads it: held, the weapon in hand is reloaded.
    Count,
};

/// How a player asks to move: the speeds its keys ask for along its view (cl_forwardspeed and the
/// others), before movement caps them.
struct MoveIntent {
    nx::UnitsPerSecond forward{};
    nx::UnitsPerSecond side{}; ///< Positive to the right.
    nx::UnitsPerSecond up{};   ///< Read in water.
};

/// Which of a client's commands one is: they count up from 1, one a command.
struct CommandNumber {
    std::uint32_t value = 0;

    [[nodiscard]] inline auto operator<=>(const CommandNumber&) const noexcept = default;
};

/// What a client asks for one stretch of its time (docs/07, section 2.1). Today the fields movement
/// and the weapons read; the rest arrive with what reads them.
struct UserCommand {
    CommandNumber number{};      ///< What seeds the random numbers the command makes the game draw.
    nx::Milliseconds duration{}; ///< The stretch of time the command moves the player through.
    nx::EulerAngles viewAngles{};
    MoveIntent move{};
    nx::Flags<Button> buttons{};
    std::optional<WeaponIndex> weaponSelect{}; ///< The weapon the player asks to draw.
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

/// The box of a standing player about its origin, and of a ducked one: the boxes the map compilers
/// grow hulls 1 and 3 by.
inline constexpr nx::Aabb kStandingBox{.min = {.x = -16.0F, .y = -16.0F, .z = -36.0F},
                                       .max = {.x = 16.0F, .y = 16.0F, .z = 36.0F}};
inline constexpr nx::Aabb kDuckedBox{.min = {.x = -16.0F, .y = -16.0F, .z = -18.0F},
                                     .max = {.x = 16.0F, .y = 16.0F, .z = 18.0F}};

/// The height of a standing player's eyes above its origin, and of a ducked player's.
inline constexpr nx::HammerUnits kStandingViewHeight{17.0F};
inline constexpr nx::HammerUnits kDuckedViewHeight{12.0F};

/// How a player moves, the original's movetype that the game sets.
enum class MoveType : std::uint8_t {
    Walk,   ///< Under gravity, among the world and the brush entities.
    Noclip, ///< Through everything, at the speeds its command asks, without gravity (the `noclip` cheat).
};

/// A player between commands: what one command reads and leaves for the next.
struct MovementState {
    MoveType moveType = MoveType::Walk;
    nx::Vec3 origin{}; ///< The center of its box, the standing or the ducked one.
    nx::Vec3 velocity{};
    /// Its eyes above its origin: they go down while it ducks.
    nx::HammerUnits viewHeight = kStandingViewHeight;
    bool isDucked = false;  ///< Its box is the ducked one.
    bool isDucking = false; ///< Going down, in its standing box still.
    /// Left of the time a duck takes on the ground, from a second: it ends with 0.6 left.
    nx::Milliseconds duckTime{};
    /// The duck button was held at the end of the last command: pressing it again starts a duck.
    bool isDuckHeld = false;
    /// Its own speed limit, that of the weapon it holds; zero leaves the server's (sv_maxspeed).
    nx::UnitsPerSecond maxSpeed{};
    bool isOnGround = false;
    /// The brush model of the entity it stands on; none on the world or in the air.
    std::optional<nx::collision::ModelIndex> groundModel{};
    WaterLevel waterLevel = WaterLevel::Dry;
    nx::collision::Contents waterType = nx::collision::Contents::Empty; ///< What it is in, when wet.
    /// Left of the slowdown a jump costs: the next jump is lower and walking slower while it lasts.
    nx::Milliseconds jumpPenalty{};
    /// Left of a jump out of water, which pushes the player at waterJumpVelocity meanwhile.
    nx::Milliseconds waterJumpTime{};
    nx::Vec3 waterJumpVelocity{};
    /// The jump button was held at the end of the last command: holding it does not jump again.
    bool isJumpHeld = false;
    /// The kick its weapon gave its view (punchangle): added to the view its moves follow and its
    /// shots aim along, and dropped back a little each move.
    nx::EulerAngles punch{};
};

/// What a brush entity is to a player's movement, as the game made it.
enum class BrushRole : std::uint8_t {
    Solid,  ///< It stops the player.
    Volume, ///< A point inside it is in what it holds: water, slime, lava.
    Ladder, ///< The player climbs it where its box touches it.
};

/// A brush entity a player moves among (a physent of the original): brush model `model` of the map,
/// placed at `origin`; a solid one turned by `angles` about it.
struct BrushEntity {
    nx::collision::ModelIndex model{};
    nx::Vec3 origin{};
    nx::EulerAngles angles{};
    BrushRole role = BrushRole::Solid;
    nx::collision::Contents contents = nx::collision::Contents::Water; ///< What a Volume holds.
};

/// The brush entities of `world`, made by nx.game, that movement meets, in the world's order, as the
/// original's engine lists them for it: the solid ones, and the others that hold contents, a ladder
/// or a volume.
[[nodiscard]] auto brushEntitiesOf(nx::ecs::World& world) -> std::vector<BrushEntity>;

/// The body of a player in `state` that the game's pushers move: its box, standing or ducked, and the
/// brush model it stands on.
[[nodiscard]] auto bodyOf(const MovementState& state) -> nx::game::Body;

/// What a command did: the player after it, and the brush entities it ran into or stood on, by their
/// models, in the order it met them, each once (the original's touch list): the game has them touched.
struct MovementResult {
    MovementState state{};
    std::vector<nx::collision::ModelIndex> touchedModels{};
};

/// `state` after `command`, under `settings`, in the world whose hulls are model 0 of `hulls`, among
/// `entities`, brush models of the same hulls; where two stop the player at once, the first does, the
/// world before them all. Pure: the result depends on the arguments alone.
[[nodiscard]] auto simulateMovement(const MovementState& state, const UserCommand& command,
                                    const MovementSettings& settings, const nx::collision::BrushHulls& hulls,
                                    std::span<const BrushEntity> entities = {}) -> MovementResult;

} // namespace cs
