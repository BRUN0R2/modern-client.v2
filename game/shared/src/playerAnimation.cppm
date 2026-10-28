// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a player's model plays (docs/10-counter-strike-gameplay.md, section 5), as ReGameDLL_CS's
/// CBasePlayer animates it (SetAnimation, GetAnimDesired, StudioEstimateGait, CalculateYawBlend,
/// CalculatePitchBlend, StudioProcessGait; GPL, consulted: docs/20 section 3), the same on the server,
/// which places its hitboxes by it, and on the clients that draw it: its torso's sequence, chosen by what
/// it does and by the weapon it holds ("ref_aim_ak47", "crouch_shoot_knife"), its legs' gait sequence,
/// by how fast it moves and whether it ducks, the way its legs face, and the aim's yaw and pitch blends
/// that turn its torso and its arms towards where it looks.
///
/// Today: standing, walking, running, ducking, jumping, swimming, firing, reloading, flinching and dying
/// by where an attack met it; the bomb's and the grenades' deaths come with them (TODO.md).
export module cs.shared:playerAnimation;

import std;
import nx.core;
import nx.math;
import nx.assets;
import :studio;
import :movement;
import :hitGroups;

export namespace cs {

/// What the game asks a player's model to play, as the original's SetAnimation is asked (PLAYER_ANIM).
enum class PlayerAction : std::uint8_t {
    Idle,        ///< PLAYER_IDLE: it stands still.
    Walk,        ///< PLAYER_WALK: it moves, on the ground or in water, or it ducks.
    Jump,        ///< PLAYER_JUMP.
    Die,         ///< PLAYER_DIE.
    Attack1,     ///< PLAYER_ATTACK1: its weapon fired or struck.
    Reload,      ///< PLAYER_RELOAD: its weapon is being reloaded.
    Flinch,      ///< PLAYER_FLINCH: an attack hurt it.
    LargeFlinch, ///< PLAYER_LARGE_FLINCH: a heavy weapon's attack hurt it but in the legs, standing.
};

/// How a player's legs play its gait sequence, and the aim's blends that turn its torso, as the
/// original's CBasePlayer keeps them (m_flGaityaw, m_flGaitMovement, m_flGaitframe, m_flYawModifier,
/// m_prevgaitorigin, m_flYaw, m_flPitch), each player's client keeping its own for those it draws.
struct Gait {
    float yaw = 0.0F;            ///< The way its legs face, in degrees.
    float movement = 0.0F;       ///< How far it went in the last frame: negative when it went backwards.
    float frame = 0.0F;          ///< Of the gait sequence, counted in its frames.
    float yawModifier = 0.0F;    ///< How fast its legs turn after its body while it stands.
    nx::Vec3 previousOrigin{};   ///< Where it was in the last frame.
    float yawBlend = 0.0F;       ///< 0 to 255: its aim from its legs' right (0) to their left (255).
    std::uint8_t pitchBlend = 0; ///< 0 to 255: its aim from up (0) to down (255).

    [[nodiscard]] inline auto operator==(const Gait&) const noexcept -> bool = default;
};

/// How a player's model plays, as the server keeps it: the sequence of its torso (pev->sequence, its
/// frame, CBaseAnimating's), its legs' gait sequence (pev->gaitsequence), what it does and what it is to
/// do next (m_Activity, m_IdealActivity), when it last fired (m_flLastFired), and its gait.
struct PlayerAnimation {
    Animation torso{};
    nx::assets::SequenceIndex gaitSequence{};
    std::optional<Activity> activity{};
    std::optional<Activity> idealActivity{};
    std::optional<nx::GameTime> lastFired{}; ///< None: long ago, as a spawn sets it (-15).
    Gait gait{};
};

/// What a player's model plays by: the model, the end of the labels of the sequences of the weapon it
/// holds (its m_szAnimExtention: "ak47"), whether it ducks (FL_DUCKING), stands on the ground
/// (FL_ONGROUND) and how deep it is in water, how fast it moves along the ground, the hit group an attack
/// last met it in (m_LastHitGroup), and the moment, the game running at `rate`.
struct PlayerAnimationContext {
    const nx::assets::MdlView* model = nullptr;
    std::string_view weaponAnimation{};
    bool isDucked = false;
    bool isOnGround = false;
    WaterLevel waterLevel = WaterLevel::Dry;
    nx::UnitsPerSecond speed{};
    HitGroup lastHitGroup = HitGroup::Generic;
    nx::GameTime now{};
    nx::TickRate rate{nx::kDefaultTickRate};
};

/// `animation` as a player that spawns has it (CBasePlayer::Spawn): its gait and its gait sequence from
/// the start, fired long ago; what it plays and does kept.
void spawnAnimation(PlayerAnimation& animation) noexcept;

/// What `animation` plays after `action`, by `context`, as the original's SetAnimation chooses it:
/// what it is to do (its ideal activity), a jump, a shot, a reload or a death but while it swims, hovers
/// or dies; then its torso's sequence by what it does, the weapon it holds and whether it ducks (its
/// "ref_" or "crouch_" "aim", "shoot", "reload" or "run" of the weapon's end; its first sequence for one
/// the model lacks), started again for a shot and for a sequence that changes, a shot's, a reload's and
/// a flinch's played to their end; and its gait sequence by whether it ducks and how fast it moves
/// (running beyond 135 units a second, running in the torso too once 4 seconds passed since it fired
/// and while it does not flinch), a jump's kept. A flinch plays "head_flinch" for the head and the
/// chest, "gut_flinch" elsewhere, either at random for a hit of no group, none for the shield. A death
/// plays one of the model's deaths by where the attack met it: the head's, the chest's, the stomach's,
/// "left" and "right" for the arms, a death at random for a hit of no group, the simplest elsewhere;
/// "crouch_die" ducked; its legs playing none. Sequences of an activity are drawn from `random`
/// (LookupActivity).
void setPlayerAnimation(PlayerAnimation& animation, PlayerAction action,
                        const PlayerAnimationContext& context, nx::Random& random);

/// `gait` after a frame of `frameTime` (gpGlobals->frametime, held to 0 to 1 second), the player at
/// `origin`, turned by `angles` (pev->angles: the pitch a third of the view's, negated), its legs
/// playing `gaitSequence` of `model` at `framerate` times its own speed, as the original's
/// StudioProcessGait plays it: its legs turned along the way it moved, or after its body while it
/// stands (faster when the body faces away, slower when nearly ahead); turned around and moving backwards
/// when its body faces more than 120 degrees from the way it moves; the aim's yaw blend from the angle
/// between its body and its legs, its pitch blend from its pitch; the gait sequence's frame advanced by
/// the distance moved over the sequence's length, or by time for a sequence that does not move, wrapped
/// into the sequence.
void processGait(Gait& gait, const nx::assets::MdlView& model, nx::assets::SequenceIndex gaitSequence,
                 nx::Vec3 origin, nx::EulerAngles angles, nx::Seconds frameTime, float framerate) noexcept;

/// The angles a player's model is turned by when its view is `view` (SV_RunCmd): its pitch a third of
/// the view's, negated, in double; its yaw and roll the view's.
[[nodiscard]] auto bodyAnglesOf(nx::EulerAngles view) noexcept -> nx::EulerAngles;

} // namespace cs
