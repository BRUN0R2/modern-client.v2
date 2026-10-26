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

/// The seconds `command` lasts, as the original makes its frame time of it: its milliseconds times a
/// thousandth in double precision, then in float (pmove's frametime, the game's gpGlobals->frametime).
[[nodiscard]] auto frameTimeOf(const UserCommand& command) noexcept -> nx::Seconds;

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

/// Where a step of a player's feet sounds (PM_UpdateStepSound): what it steps on or in.
enum class StepPlace : std::uint8_t {
    Ground, ///< The ground, by what its texture is made of.
    Ladder,
    Wade,  ///< Water up to its knees.
    Slosh, ///< Water at its feet.
};

/// How far under a player the texture it stands on is looked for (PM_CatagorizeTextureType).
inline constexpr nx::HammerUnits kStepTextureDepth{64.0F};

/// The world, as what a player's box meets.
struct WorldSolid {
    [[nodiscard]] constexpr auto operator==(const WorldSolid&) const noexcept -> bool = default;
};

/// A box (SolidBox), as what a player's box meets: it has no model, and so no texture.
struct BoxSolid {
    [[nodiscard]] constexpr auto operator==(const BoxSolid&) const noexcept -> bool = default;
};

/// What a player's box meets, as the original's physents are: the world, the brush model of an
/// entity, or a box.
using MovementSolid = std::variant<WorldSolid, nx::collision::ModelIndex, BoxSolid>;

/// The brush model of `solid` when it is an entity's; none for the world and a box.
[[nodiscard]] auto modelOf(const MovementSolid& solid) noexcept -> std::optional<nx::collision::ModelIndex>;

/// Where a player stood on the ground as its feet sounded: the texture kStepTextureDepth under
/// `origin`, in what it stood on (`ground`), is what it stood on: none on a box, which has no model.
struct StepGround {
    nx::Vec3 origin{};
    MovementSolid ground{};
};

/// A step of a player's feet (PM_PlayStepSound): where, with which foot, and how loud.
struct Footstep {
    StepPlace place = StepPlace::Ground;
    bool isLeftFoot = false;
    /// The share of the place's volume it has (a ground's by what it is made of): 1, less while the
    /// player ducks (PM_UpdateStepSound).
    float share = 1.0F;
    /// A volume of its own instead, from 0 to 1, as the player jumps or lands (PM_Jump,
    /// PM_CheckFalling).
    std::optional<float> volume{};
    /// What it stood on; none in the air, where no texture is under it.
    std::optional<StepGround> ground{};
};

/// A splash: the player went into water or out of it, or swam up (PM_PlayWaterSounds, PM_Jump).
struct Splash {};

/// A sound a player's movement makes.
using MovementSound = std::variant<Footstep, Splash>;

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
    /// What it stands on while on the ground; the world while in the air.
    MovementSolid ground{};
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
    /// It climbed a ladder in its last move (the original's MOVETYPE_FLY).
    bool isOnLadder = false;
    /// Left until its feet may sound again (flTimeStepSound).
    nx::Milliseconds stepTime{};
    /// Its last step was of its left foot (iStepLeft).
    bool isLeftStep = false;
    /// How fast it was falling when last in the air (flFallVelocity), down positive.
    nx::UnitsPerSecond fallSpeed{};
    /// Left until swimming up splashes again (flSwimTime).
    nx::Milliseconds swimTime{};
    /// The next of the nudges a stuck player is tried at (rgStuckLast).
    std::uint32_t stuckNudge = 0;
    /// Since a stuck player was last tried at a nudge, while that is too recent to try the next one
    /// (rgStuckCheckTime): none once it is not.
    std::optional<nx::Milliseconds> sinceStuckCheck{};
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

/// A box that stops a player, the original's SOLID_SLIDEBOX and SOLID_BBOX physents (a hostage,
/// another player): `box` about `origin`, never turned.
struct SolidBox {
    nx::Vec3 origin{};
    nx::Aabb box{};
};

/// The brush entities of `world`, made by nx.game, that movement meets, in the world's order, as the
/// original's engine lists them for it: the solid ones, and the others that hold contents, a ladder
/// or a volume.
[[nodiscard]] auto brushEntitiesOf(nx::ecs::World& world) -> std::vector<BrushEntity>;

/// The entities of `world` solid as boxes (nx::game::BoxSolidity) that stop a player, in the world's
/// order, as the original's engine lists them for it (SOLID_SLIDEBOX, SOLID_BBOX): a hostage.
[[nodiscard]] auto solidBoxesOf(nx::ecs::World& world) -> std::vector<SolidBox>;

/// The body of a player in `state` that the game's pushers move: its box, standing or ducked, and the
/// brush model it stands on.
[[nodiscard]] auto bodyOf(const MovementState& state) -> nx::game::Body;

/// What a command did: the player after it, the brush entities it ran into or stood on, by their
/// models, in the order it met them, each once (the original's touch list): the game has them touched;
/// and the sounds its movement made, in their order.
struct MovementResult {
    MovementState state{};
    std::vector<nx::collision::ModelIndex> touchedModels{};
    std::vector<MovementSound> sounds{};
};

/// `state` after `command`, under `settings`, in the world whose hulls are model 0 of `hulls`, among
/// `entities`, brush models of the same hulls, and `boxes`, each grown by the player's box as the
/// original grows a physent's (PM_HullForBox); where two stop the player at once, the first does, the
/// world before them all and the brush entities before the boxes. Pure: the result depends on the
/// arguments alone.
[[nodiscard]] auto simulateMovement(const MovementState& state, const UserCommand& command,
                                    const MovementSettings& settings, const nx::collision::BrushHulls& hulls,
                                    std::span<const BrushEntity> entities = {},
                                    std::span<const SolidBox> boxes = {}) -> MovementResult;

} // namespace cs
