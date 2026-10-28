// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.assets;

namespace cs {

namespace {

/// Beyond this speed a player's legs run, and its torso too once kAimAfterFiring passed since it fired.
constexpr nx::UnitsPerSecond kRunSpeed{135.0F};
constexpr nx::Seconds kAimAfterFiring{4.0F};

/// The longest frame the gait plays, and the speed below which a player stands, its legs turning after
/// its body (StudioEstimateGait).
constexpr double kLongestGaitFrame = 1.0;
constexpr double kLeastGaitSpeed = 5.0;
/// How fast its legs turn after its body, by the frame, when the body faces nearly ahead of them or away
/// from them (m_flYawModifier), and below which a turn is none.
constexpr double kNearlyAhead = 5.0;
constexpr double kFacingAway = 90.0;
constexpr float kNearlyAheadModifier = 0.05F;
constexpr float kFacingAwayModifier = 3.5F;
constexpr double kShortGaitFrame = 0.25;
constexpr double kLeastTurn = 0.1;
/// Beyond this angle between its body and the way it moves, a player moves backwards (CalculateYawBlend).
constexpr double kBackwardsAngle = 120.0;

constexpr double kDegreesPerTurn = 360.0;
constexpr double kHalfTurn = 180.0;
constexpr double kQuarterTurn = 90.0;
/// The yaw blend: the angle between the body and the legs over a quarter turn, times the blend's half,
/// from its middle; held to its range.
constexpr double kYawBlendHalf = 128.0;
constexpr double kYawBlendMiddle = 127.0;
constexpr double kBlendTop = 255.0;
/// The pitch blend (StudioPlayerBlend): the body's pitch times 3, the view's, from 45 up to 45 down.
constexpr float kBodyPitchScale = 3.0F;
constexpr float kPitchReach = 45.0F;
constexpr float kPitchSpan = 90.0F;

/// The view's pitch a body's is a third of, negated (SV_RunCmd).
constexpr double kBodyPitchShare = 3.0;

/// The deaths of a hit of no group, by a draw of 0 to 8 (SetAnimation's ACT_DIESIMPLE).
constexpr std::array kGenericDeaths{Activity::DieHeadshot,  Activity::DieGutshot,  Activity::DieBackshot,
                                    Activity::DieSimple,    Activity::DieBackward, Activity::DieForward,
                                    Activity::DieChestshot, Activity::DieGutshot,  Activity::DieHeadshot};
constexpr std::string_view kCrouchDeath = "crouch_die";
/// The deaths of a hit in either arm, and the flinches of one in the head and in the gut.
constexpr std::string_view kLeftArmDeath = "left";
constexpr std::string_view kRightArmDeath = "right";
constexpr std::string_view kHeadFlinch = "head_flinch";
constexpr std::string_view kGutFlinch = "gut_flinch";
/// A hit of no group flinches the head or the gut, each as likely (RANDOM_LONG(0, 1)).
constexpr std::uint64_t kGenericFlinches = 2;

/// Whether the sequence label of an animation begins with "crouch_" when the player ducks
/// (GetAnimDesired's ANIM_CROUCH), or "ref_" whatever it does (ANIM_NORMAL).
enum class Posture : std::uint8_t {
    Standing,
    AsItIs,
};

/// `value` wrapped into a half turn either way.
[[nodiscard]] auto withinHalfTurn(double value) noexcept -> double {
    if (value < -kHalfTurn) {
        return value + kDegreesPerTurn;
    }
    if (value > kHalfTurn) {
        return value - kDegreesPerTurn;
    }
    return value;
}

/// The sequence the player's torso plays for `animation` ("aim", "shoot", "reload", "run") of the weapon
/// it holds (GetAnimDesired); none the model lacks.
[[nodiscard]] auto desiredSequence(std::string_view animation, Posture posture,
                                   const PlayerAnimationContext& context)
    -> std::optional<nx::assets::SequenceIndex> {
    const std::string_view prefix = posture == Posture::AsItIs && context.isDucked ? "crouch_" : "ref_";
    return lookupSequence(context.model->sequences(),
                          std::format("{}{}_{}", prefix, animation, context.weaponAnimation));
}

/// The sequence of `activity`, drawn from `random` (LookupActivity); the first for none, as the original
/// bounds -1.
[[nodiscard]] auto sequenceOf(Activity activity, const PlayerAnimationContext& context, nx::Random& random)
    -> nx::assets::SequenceIndex {
    return lookupActivity(context.model->sequences(), activity, random).value_or(nx::assets::SequenceIndex{});
}

/// `animation`'s torso playing `sequence` from its first frame, as SetAnimation sets pev->sequence and
/// calls ResetSequenceInfo.
void playTorso(PlayerAnimation& animation, nx::assets::SequenceIndex sequence,
               const PlayerAnimationContext& context) {
    animation.torso.sequence = sequence;
    animation.torso.frame = 0.0F;
    resetSequence(animation.torso, context.model->sequences()[sequence.value], context.now);
}

/// Whether the player swims, hovers in water or dies: what a jump, a shot and a reload do not interrupt.
[[nodiscard]] auto isUninterrupted(std::optional<Activity> activity) noexcept -> bool {
    return activity == Activity::Swim || activity == Activity::DieSimple || activity == Activity::Hover;
}

/// What `action` asks the player to do next (SetAnimation's first switch).
[[nodiscard]] auto idealActivityOf(const PlayerAnimation& animation, PlayerAction action,
                                   const PlayerAnimationContext& context) -> std::optional<Activity> {
    const auto unlessUninterrupted = [&animation](Activity activity) {
        return isUninterrupted(animation.activity) ? animation.activity : std::optional{activity};
    };
    switch (action) {
    case PlayerAction::Jump:
        return unlessUninterrupted(Activity::Hop);
    case PlayerAction::Die:
        return Activity::DieSimple;
    case PlayerAction::Attack1:
        return unlessUninterrupted(Activity::RangeAttack1);
    case PlayerAction::Reload:
        return unlessUninterrupted(Activity::Reload);
    case PlayerAction::Flinch:
        return Activity::Flinch;
    case PlayerAction::LargeFlinch:
        return Activity::LargeFlinch;
    case PlayerAction::Idle:
    case PlayerAction::Walk:
        if (context.isOnGround ||
            (animation.activity != Activity::Hop && animation.activity != Activity::Leap)) {
            if (context.waterLevel <= WaterLevel::Feet) {
                return Activity::Walk;
            }
            return context.speed == nx::UnitsPerSecond{} ? Activity::Hover : Activity::Swim;
        }
        return animation.activity;
    }
    return animation.idealActivity;
}

/// Whether the torso's one-off sequence of `activity` is over or it does something else.
[[nodiscard]] auto isOverOrNot(const PlayerAnimation& animation, Activity activity) noexcept -> bool {
    return animation.activity != activity || animation.torso.isFinished;
}

/// Whether the torso's shots, flinches and reloads are over, or it does something else (ACT_WALK's).
[[nodiscard]] auto areOneOffsOver(const PlayerAnimation& animation) noexcept -> bool {
    return isOverOrNot(animation, Activity::RangeAttack1) && isOverOrNot(animation, Activity::Flinch) &&
           isOverOrNot(animation, Activity::LargeFlinch) && isOverOrNot(animation, Activity::Reload);
}

/// Whether the player flinches.
[[nodiscard]] auto isFlinching(std::optional<Activity> activity) noexcept -> bool {
    return activity == Activity::Flinch || activity == Activity::LargeFlinch;
}

/// The flinch the player plays (SetAnimation's ACT_FLINCH and ACT_LARGE_FLINCH), by where it was met:
/// "head_flinch" for the head and the chest, "gut_flinch" elsewhere, either drawn from `random` for a hit
/// of no group; the first sequence for the shield, and for one the model lacks.
[[nodiscard]] auto flinchSequenceOf(const PlayerAnimationContext& context, nx::Random& random)
    -> nx::assets::SequenceIndex {
    std::optional<std::string_view> flinch = kGutFlinch;
    switch (context.lastHitGroup) {
    case HitGroup::Generic:
        flinch = random.below(kGenericFlinches) != 0 ? kHeadFlinch : kGutFlinch;
        break;
    case HitGroup::Head:
    case HitGroup::Chest:
        flinch = kHeadFlinch;
        break;
    case HitGroup::Shield:
        flinch.reset();
        break;
    default:
        break;
    }
    return flinch
        .and_then(
            [&context](std::string_view label) { return lookupSequence(context.model->sequences(), label); })
        .value_or(nx::assets::SequenceIndex{});
}

/// The death the player plays (SetAnimation's ACT_DIESIMPLE), by where it was met: one of a hit of no
/// group, drawn from `random`; the head's, the chest's or the stomach's; "left" and "right" for the
/// arms; the simplest elsewhere; but "crouch_die" ducked; the first sequence for one the model lacks.
[[nodiscard]] auto deathSequenceOf(const PlayerAnimationContext& context, nx::Random& random)
    -> nx::assets::SequenceIndex {
    const std::span<const nx::assets::StudioSequence> sequences = context.model->sequences();
    const auto ofActivity = [&sequences, &random](Activity activity) {
        return lookupActivity(sequences, activity, random);
    };
    std::optional<nx::assets::SequenceIndex> sequence;
    switch (context.lastHitGroup) {
    case HitGroup::Generic:
        sequence = ofActivity(kGenericDeaths.at(random.below(kGenericDeaths.size())));
        break;
    case HitGroup::Head:
        sequence = ofActivity(Activity::DieHeadshot);
        break;
    case HitGroup::Chest:
        sequence = ofActivity(Activity::DieChestshot);
        break;
    case HitGroup::Stomach:
        sequence = ofActivity(Activity::DieGutshot);
        break;
    case HitGroup::LeftArm:
        sequence = lookupSequence(sequences, kLeftArmDeath);
        break;
    case HitGroup::RightArm:
        sequence = lookupSequence(sequences, kRightArmDeath);
        break;
    default:
        sequence = ofActivity(Activity::DieSimple);
        break;
    }
    if (context.isDucked) {
        sequence = lookupSequence(sequences, kCrouchDeath);
    }
    return sequence.value_or(nx::assets::SequenceIndex{});
}

} // namespace

void spawnAnimation(PlayerAnimation& animation) noexcept {
    animation.gaitSequence = nx::assets::SequenceIndex{};
    animation.gait.frame = 0.0F;
    animation.gait.yaw = 0.0F;
    animation.gait.movement = 0.0F;
    animation.gait.previousOrigin = nx::Vec3{};
    animation.lastFired.reset();
}

void setPlayerAnimation(PlayerAnimation& animation, PlayerAction action,
                        const PlayerAnimationContext& context, nx::Random& random) {
    nx::expects(context.model != nullptr, "setPlayerAnimation: a model");
    const nx::assets::SequenceIndex hop = sequenceOf(Activity::Hop, context, random);
    const nx::assets::SequenceIndex leap = sequenceOf(Activity::Leap, context, random);
    animation.idealActivity = idealActivityOf(animation, action, context);
    const std::optional<Activity> ideal = animation.idealActivity;
    const auto hasFiredLately = [&animation, &context] {
        return animation.lastFired &&
               context.now.since(*animation.lastFired, context.rate) <= kAimAfterFiring;
    };
    nx::assets::SequenceIndex desired{};
    if (ideal == Activity::Hop || ideal == Activity::Leap) {
        if (animation.activity == ideal) {
            return;
        }
        std::string_view held = "aim";
        if (animation.activity == Activity::RangeAttack1) {
            held = "shoot";
        } else if (animation.activity == Activity::Reload) {
            held = "reload";
        }
        desired = desiredSequence(held, Posture::Standing, context).value_or(nx::assets::SequenceIndex{});
        if (animation.torso.sequence != desired || !animation.torso.isLooping) {
            animation.torso.frame = 0.0F;
        }
        animation.gaitSequence = sequenceOf(Activity::Hop, context, random);
        animation.activity = ideal;
    } else if (ideal == Activity::RangeAttack1) {
        animation.lastFired = context.now;
        desired = desiredSequence("shoot", Posture::AsItIs, context).value_or(nx::assets::SequenceIndex{});
        playTorso(animation, desired, context);
        animation.activity = ideal;
    } else if (ideal == Activity::Reload) {
        desired = desiredSequence("reload", Posture::AsItIs, context).value_or(nx::assets::SequenceIndex{});
        if (animation.torso.sequence != desired || !animation.torso.isLooping) {
            animation.torso.frame = 0.0F;
        }
        animation.activity = ideal;
    } else if (ideal == Activity::Walk) {
        if (areOneOffsOver(animation)) {
            if (context.speed <= kRunSpeed || hasFiredLately()) {
                desired =
                    desiredSequence("aim", Posture::AsItIs, context).value_or(nx::assets::SequenceIndex{});
                animation.activity = Activity::Walk;
            } else if (const std::optional<nx::assets::SequenceIndex> run =
                           desiredSequence("run", Posture::Standing, context)) {
                desired = *run;
                animation.activity = Activity::Run;
                animation.gaitSequence = *run;
            } else {
                desired =
                    desiredSequence("aim", Posture::AsItIs, context).value_or(nx::assets::SequenceIndex{});
                animation.activity = Activity::Run;
                animation.gaitSequence = sequenceOf(Activity::Run, context, random);
            }
        } else {
            desired = animation.torso.sequence;
        }
        animation.gaitSequence =
            sequenceOf(context.speed > kRunSpeed ? Activity::Run : Activity::Walk, context, random);
    } else if (isFlinching(ideal)) {
        animation.activity = ideal;
        desired = flinchSequenceOf(context, random);
    } else if (ideal == Activity::DieSimple) {
        if (animation.activity == ideal) {
            return;
        }
        animation.activity = ideal;
        desired = deathSequenceOf(context, random);
        if (animation.torso.sequence != desired) {
            animation.gaitSequence = nx::assets::SequenceIndex{};
            playTorso(animation, desired, context);
        }
        return;
    } else {
        // Swimming and hovering in water, and what a model has a sequence of the activity for.
        if (animation.activity == ideal || !ideal) {
            return;
        }
        animation.activity = ideal;
        desired = sequenceOf(*ideal, context, random);
        if (animation.torso.sequence != desired) {
            animation.gaitSequence = nx::assets::SequenceIndex{};
            playTorso(animation, desired, context);
        }
        return;
    }
    if (animation.gaitSequence != hop && animation.gaitSequence != leap) {
        if (context.isDucked) {
            animation.gaitSequence =
                sequenceOf(context.speed != nx::UnitsPerSecond{} ? Activity::Crouch : Activity::CrouchIdle,
                           context, random);
        } else if (context.speed > kRunSpeed) {
            if (!hasFiredLately() && !isFlinching(animation.activity)) {
                if (const std::optional<nx::assets::SequenceIndex> run =
                        desiredSequence("run", Posture::Standing, context)) {
                    desired = *run;
                } else {
                    desired = desiredSequence("aim", Posture::AsItIs, context)
                                  .value_or(nx::assets::SequenceIndex{});
                }
                animation.activity = Activity::Run;
            }
            animation.gaitSequence = sequenceOf(Activity::Run, context, random);
        } else {
            animation.gaitSequence = sequenceOf(
                context.speed > nx::UnitsPerSecond{} ? Activity::Walk : Activity::Idle, context, random);
        }
    }
    if (animation.torso.sequence != desired) {
        playTorso(animation, desired, context);
    }
}

void processGait(Gait& gait, const nx::assets::MdlView& model, nx::assets::SequenceIndex gaitSequence,
                 nx::Vec3 origin, nx::EulerAngles angles, nx::Seconds frameTime, float framerate) noexcept {
    const double frame = std::clamp(static_cast<double>(frameTime.count()), 0.0, kLongestGaitFrame);
    // StudioEstimateGait.
    if (frame == 0.0) {
        gait.movement = 0.0F;
    } else {
        nx::Vec3 moved = origin - gait.previousOrigin;
        gait.previousOrigin = origin;
        gait.movement = static_cast<float>(nx::length(moved));
        if (static_cast<double>(gait.movement) / frame < kLeastGaitSpeed) {
            gait.movement = 0.0F;
            moved.x = 0.0F;
            moved.y = 0.0F;
        }
        if (moved.x == 0.0F && moved.y == 0.0F) {
            double turn = static_cast<double>(angles.yaw.value()) - static_cast<double>(gait.yaw);
            const double facing = withinHalfTurn(std::fmod(turn, kDegreesPerTurn));
            turn = withinHalfTurn(turn - (std::trunc(turn / kDegreesPerTurn) * kDegreesPerTurn));
            if (facing > -kNearlyAhead && facing < kNearlyAhead) {
                gait.yawModifier = kNearlyAheadModifier;
            }
            if (facing < -kFacingAway || facing > kFacingAway) {
                gait.yawModifier = kFacingAwayModifier;
            }
            turn *= frame < kShortGaitFrame ? frame * static_cast<double>(gait.yawModifier) : frame;
            // Parity: the original weighs the turn cut to a whole number of degrees.
            if (std::abs(std::trunc(turn)) < kLeastTurn) {
                turn = 0.0;
            }
            gait.yaw = static_cast<float>(static_cast<double>(gait.yaw) + turn);
            // In float, as m_flGaityaw is.
            constexpr std::int64_t kTurn = 360;
            gait.yaw -=
                static_cast<float>(static_cast<std::int64_t>(gait.yaw / static_cast<float>(kTurn)) * kTurn);
            gait.movement = 0.0F;
        } else {
            const double yaw = std::atan2(static_cast<double>(moved.y), static_cast<double>(moved.x)) *
                               kHalfTurn / std::numbers::pi;
            gait.yaw = static_cast<float>(std::clamp(yaw, -kHalfTurn, kHalfTurn));
        }
    }
    // CalculateYawBlend.
    double between = withinHalfTurn(
        std::fmod(static_cast<double>(angles.yaw.value()) - static_cast<double>(gait.yaw), kDegreesPerTurn));
    if (gait.movement != 0.0F) {
        if (between > kBackwardsAngle) {
            gait.yaw = static_cast<float>(static_cast<double>(gait.yaw) - kHalfTurn);
            gait.movement = -gait.movement;
            between -= kHalfTurn;
        } else if (between < -kBackwardsAngle) {
            gait.yaw = static_cast<float>(static_cast<double>(gait.yaw) + kHalfTurn);
            gait.movement = -gait.movement;
            between += kHalfTurn;
        }
    }
    const double blend =
        std::clamp(((between / kQuarterTurn) * kYawBlendHalf) + kYawBlendMiddle, 0.0, kBlendTop);
    gait.yawBlend = static_cast<float>(kBlendTop - blend);
    // CalculatePitchBlend, StudioPlayerBlend.
    const float range = std::trunc(angles.pitch.value() * kBodyPitchScale);
    if (range <= -kPitchReach) {
        gait.pitchBlend = static_cast<std::uint8_t>(kBlendTop);
    } else if (range >= kPitchReach) {
        gait.pitchBlend = 0;
    } else {
        gait.pitchBlend = static_cast<std::uint8_t>(
            std::trunc((kPitchReach - range) * (static_cast<float>(kBlendTop) / kPitchSpan)));
    }
    // The gait sequence's frame.
    const std::span<const nx::assets::StudioSequence> sequences = model.sequences();
    if (gaitSequence.value >= sequences.size()) {
        return;
    }
    const nx::assets::StudioSequence& sequence = sequences[gaitSequence.value];
    const auto frames = static_cast<float>(sequence.frameCount);
    if (sequence.linearMovement.x > 0.0F) {
        gait.frame += (gait.movement / sequence.linearMovement.x) * frames;
    } else {
        gait.frame = static_cast<float>(static_cast<double>(gait.frame) +
                                        (static_cast<double>(framerate * sequence.fps) * frame));
    }
    gait.frame -= std::trunc(gait.frame / frames) * frames;
    if (gait.frame < 0.0F) {
        gait.frame += frames;
    }
}

auto bodyAnglesOf(nx::EulerAngles view) noexcept -> nx::EulerAngles {
    return nx::EulerAngles{
        .pitch = nx::Degrees{static_cast<float>(-static_cast<double>(view.pitch.value()) / kBodyPitchShare)},
        .yaw = view.yaw,
        .roll = view.roll};
}

} // namespace cs
