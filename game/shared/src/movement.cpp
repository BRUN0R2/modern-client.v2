// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.collision;
import nx.game;

namespace cs {

namespace {

using nx::Vec3;
using nx::collision::BrushHulls;
using nx::collision::Contents;
using nx::collision::Hull;
using nx::collision::ModelIndex;

/// From a player's origin to its feet, and to the top of its head, standing and ducked.
constexpr float kStandingHalfHeight = kStandingBox.max.z;
constexpr float kDuckedHalfHeight = kDuckedBox.max.z;
/// How far the origin moves when a player on the ground ducks or stands: the boxes' feet meet.
constexpr double kDuckDrop = double{kStandingHalfHeight - kDuckedHalfHeight};

// The thresholds of the original's movement, each a behavior a player feels.

/// Faster upwards, a player has left the ground.
constexpr float kLiftOffSpeed = 180.0F;
/// How far below a player the ground is looked for.
constexpr float kGroundProbeDepth = 2.0F;
/// After a slide, a speed on an axis below this is none.
constexpr double kStopEpsilon = 0.1;
/// A move slides along this many planes at most before it stops.
constexpr std::size_t kMaxClipPlanes = 5;
/// A move is redirected this many times at most; the time left after them is lost.
constexpr int kMaxBumps = 4;
/// Slower than this, a walking player stops.
constexpr double kMinWalkSpeed = 1.0;
/// Slower than this, friction leaves a player alone.
constexpr float kMinFrictionSpeed = 0.1F;
/// Friction looks this far ahead of a player's feet for a drop, and a drop this deep is an edge.
constexpr float kEdgeLookAhead = 16.0F;
constexpr float kEdgeDropDepth = 34.0F;
/// In the air, acceleration stops once the speed along the wished direction is this.
constexpr float kAirWishSpeed = 30.0F;
/// A jump rises this high against this gravity, whatever sv_gravity is.
constexpr double kJumpHeight = 45.0;
constexpr double kJumpGravity = 800.0;
/// A jump's penalty lasts this long: a quarter of the speed at first, 19 percent less each second.
constexpr nx::Milliseconds kJumpPenalty{1315.789429F};
constexpr double kPenaltyPercentPerSecond = 19.0;
constexpr double kWholeInPercent = 100.0;
constexpr double kPerPercent = 0.01;
constexpr double kSecondsPerMillisecond = 0.001;
/// A jump faster than this multiple of the speed limit is slowed to this share of that multiple.
constexpr float kBunnyHopSpeedFactor = 1.2F;
constexpr double kBunnyHopSlowdown = 0.8;
/// A command's view roll is this multiple of the roll its sideways speed asks for.
constexpr float kRollScale = 4.0F;
/// A weapon's kick drops back by this many degrees a second, and this share of itself a second
/// more (PM_DropPunchAngle).
constexpr double kPunchDrop = 10.0;
constexpr double kPunchDropShare = 0.5;
/// Ducking or ducked, a player's keys ask for this share of their speed.
constexpr double kDuckSpeedShare = 0.333;
/// A duck on the ground starts with this much time and ends when it is down to this share of it:
/// it takes 0.4 s, the eyes easing down meanwhile. In the air it is at once.
constexpr nx::Milliseconds kDuckTime{1000.0F};
constexpr double kDuckLeftWhenDown = 0.6;
/// A player stuck by ducking is raised a unit at a time, as far as this.
constexpr int kMaxCrouchUnstick = 36;
/// On a ladder, a player climbs at this speed at most, and leaves it at this one when it jumps; on
/// the floor, moving away from it, it is pushed off at the climbing speed.
constexpr float kClimbSpeed = 200.0F;
constexpr float kLadderJumpSpeed = 270.0F;
/// The floor under a player on a ladder is looked for this far below its feet.
constexpr float kFloorProbeDepth = 1.0F;
/// Water: the feet are this far above the bottom of the box.
constexpr float kFeetProbeHeight = 1.0F;
/// Without a key held, a swimmer drifts down at this speed.
constexpr float kSinkSpeed = 60.0F;
/// A swimmer wishes for this share of the speed its keys ask for.
constexpr double kSwimSpeedShare = 0.8;
/// Slower wishes do not move a swimmer.
constexpr float kMinSwimSpeed = 0.1F;
/// Slower than this, a player's feet make no sound, and its next step waits kQuietStepTime
/// (PM_UpdateStepSound); a jump as fast sounds the ground at kJumpStepVolume (PM_Jump).
constexpr float kStepSpeed = 150.0F;
constexpr nx::Milliseconds kQuietStepTime{400.0F};
constexpr float kJumpStepVolume = 1.0F;
/// The time from a step to the next: on the ground or sloshing, on a ladder, and wading; ducked or
/// on a ladder, this much more, and standing up takes as much off.
constexpr nx::Milliseconds kStepTime{300.0F};
constexpr nx::Milliseconds kLadderStepTime{350.0F};
constexpr nx::Milliseconds kWadeStepTime{600.0F};
constexpr nx::Milliseconds kSlowerStepTime{100.0F};
/// A ducked player's steps are quiet once its duck has this much left (flDuckTime < 950).
constexpr nx::Milliseconds kQuietDuckTime{950.0F};
/// The share of a step's volume a ducked player's steps keep.
constexpr float kQuietStepShare = 0.35F;
/// A player's knees and feet, as shares of its box's height below its origin.
constexpr double kKneeShare = 0.3;
constexpr double kFeetShare = 0.5;
/// Landings (PM_CheckFalling): falling at least this fast sounds and rolls the view; faster than the
/// safest fall, at kHardLandingVolume, than half of it at kFirmLandingVolume, slower than the
/// bounce speed in silence, and else at kSoftLandingVolume; in water, always the soft one.
constexpr float kFallPunchSpeed = 250.0F;
constexpr float kMostSafeFallSpeed = 580.0F;
constexpr float kLeastBounceSpeed = 350.0F;
constexpr float kSoftLandingVolume = 0.5F;
constexpr float kFirmLandingVolume = 0.85F;
constexpr float kHardLandingVolume = 1.0F;
/// The view rolls this many degrees for each unit a second of the fall, and its pitch is held to this.
constexpr double kLandingRollPerSpeed = 0.013;
constexpr nx::Degrees kMostLandingPitch{8.0F};
/// Swimming up splashes once in this long at most (flSwimTime).
constexpr nx::Milliseconds kSwimSplashTime{1000.0F};
/// A stuck player is tried at its next nudge once more than this has passed since the last
/// (PM_CHECKSTUCK_MINTIME).
constexpr nx::Milliseconds kStuckCheckInterval{50.0F};
/// How many nudges a stuck player is tried at in turn (rgv3tStuckTable), and from which of them on
/// the server moves it to one that frees it: it leaves the earlier ones, the smaller, to the client.
constexpr std::uint32_t kStuckNudgeCount = 54;
constexpr std::uint32_t kFirstServerNudge = kStuckNudgeCount / 2;
/// The nudges of a stuck player, in turn, as PM_CreateStuckTable orders them: an eighth of a unit,
/// the network's precision, along z, y and x, each less, none and more, then to each corner of that;
/// then up by none, 1 and 6, along y and x by 2, each less, none and more, then at each of those
/// heights each of those x with each of those y; the last none.
constexpr std::array<Vec3, kStuckNudgeCount> kStuckNudges = [] {
    constexpr float kSmall = 0.125F;
    constexpr float kLarge = 2.0F;
    constexpr std::array kSteps{-1.0F, 0.0F, 1.0F};
    constexpr std::array kCorners{-1.0F, 1.0F};
    constexpr std::array kHeights{0.0F, 1.0F, 6.0F};
    std::array<Vec3, kStuckNudgeCount> nudges{};
    std::size_t next = 0;
    const auto add = [&nudges, &next](Vec3 nudge) {
        nudges[next++] = nudge;
    };
    for (const float step : kSteps) {
        add(Vec3{.z = step * kSmall});
    }
    for (const float step : kSteps) {
        add(Vec3{.y = step * kSmall});
    }
    for (const float step : kSteps) {
        add(Vec3{.x = step * kSmall});
    }
    for (const float x : kCorners) {
        for (const float y : kCorners) {
            for (const float z : kCorners) {
                add(Vec3{.x = x * kSmall, .y = y * kSmall, .z = z * kSmall});
            }
        }
    }
    for (const float height : kHeights) {
        add(Vec3{.z = height});
    }
    for (const float step : kSteps) {
        add(Vec3{.y = step * kLarge});
    }
    for (const float step : kSteps) {
        add(Vec3{.x = step * kLarge});
    }
    for (const float height : kHeights) {
        for (const float x : kSteps) {
            for (const float y : kSteps) {
                add(Vec3{.x = x * kLarge, .y = y * kLarge, .z = height});
            }
        }
    }
    return nudges;
}();
/// The speed jump swims up at, by what the player is in.
constexpr float kSwimUpInWater = 100.0F;
constexpr float kSwimUpInSlime = 80.0F;
constexpr float kSwimUpInLava = 50.0F;
/// A jump out of water: a wall this far ahead at this height above the origin, as steep as this,
/// with nothing over it at head height...
constexpr float kWaterJumpReach = 24.0F;
constexpr float kWaterJumpProbeHeight = 8.0F;
constexpr float kWallNormalZ = 0.1F;
/// ...lifts the player at this speed for this long, pushed against the wall at this speed; not
/// while sinking faster than this.
constexpr float kWaterJumpSpeed = 225.0F;
constexpr nx::Milliseconds kWaterJumpDuration{2000.0F};
constexpr nx::Milliseconds kMaxWaterJumpTime{10000.0F};
constexpr float kWaterJumpPush = 50.0F;
constexpr float kMaxSinkSpeedToHop = 180.0F;

/// What a player trace met, as the original's PM_PlayerTrace returns it: the world's movement trace
/// when it stopped, or nothing at all.
struct PlayerTrace {
    float fraction = 1.0F;
    Vec3 end{};
    Vec3 normal{};
    bool startsSolid = false;
    bool isAllSolid = false;
    MovementSolid met{}; ///< What stopped it; the world when nothing did.
};

/// Length as the original measures it: each square in single precision, their sum in double.
[[nodiscard]] auto lengthOf(Vec3 v) -> double {
    double sum = 0.0;
    for (const float component : {v.x, v.y, v.z}) {
        sum += double{component * component};
    }
    return std::sqrt(sum);
}

/// Scales `v` to unit length as the original does, its squares summed in single precision and the
/// scale in double, and returns the length it had. The zero vector stays as it is.
auto normalize(Vec3& v) -> double {
    const double length = std::sqrt(double{nx::dot(v, v)});
    if (length != 0.0) {
        const double inverse = 1.0 / length;
        v = Vec3{.x = static_cast<float>(double{v.x} * inverse),
                 .y = static_cast<float>(double{v.y} * inverse),
                 .z = static_cast<float>(double{v.z} * inverse)};
    }
    return length;
}

/// `v` plus `scale` times `direction`, each sum in double precision.
[[nodiscard]] auto addScaled(Vec3 v, double scale, Vec3 direction) -> Vec3 {
    return Vec3{.x = static_cast<float>(double{v.x} + scale * double{direction.x}),
                .y = static_cast<float>(double{v.y} + scale * double{direction.y}),
                .z = static_cast<float>(double{v.z} + scale * double{direction.z})};
}

/// `velocity` slid along a plane facing `normal`: what goes into the plane removed, in double
/// precision; an axis left slower than kStopEpsilon stops.
[[nodiscard]] auto clipVelocity(Vec3 velocity, Vec3 normal) -> Vec3 {
    const double into = double{nx::dot(velocity, normal)};
    const auto clip = [into](float component, float axis) {
        const auto out = static_cast<float>(double{component} - double{axis} * into);
        return double{out} > -kStopEpsilon && double{out} < kStopEpsilon ? 0.0F : out;
    };
    return Vec3{
        .x = clip(velocity.x, normal.x), .y = clip(velocity.y, normal.y), .z = clip(velocity.z, normal.z)};
}

/// The share of its speed a player keeps with `left` of a jump penalty to go.
[[nodiscard]] auto penaltyShare(nx::Milliseconds left) -> double {
    return (kWholeInPercent - double{left.count()} * kSecondsPerMillisecond * kPenaltyPercentPerSecond) *
           kPerPercent;
}

/// Water, slime, lava and the other contents movement swims in.
[[nodiscard]] auto isLiquid(Contents contents) -> bool {
    return std::to_underlying(contents) <= std::to_underlying(Contents::Water) &&
           std::to_underlying(contents) > std::to_underlying(Contents::Translucent);
}

/// The original's ease-in, ease-out curve: 3t² - 2t³ of `value` times `scale`, the cube in double
/// precision.
[[nodiscard]] auto easeInOut(float value, float scale) -> float {
    const float scaled = scale * value;
    const double squared = double{scaled * scaled};
    return static_cast<float>(3.0 * squared - 2.0 * squared * double{scaled});
}

/// The axes of a move along the ground or through the air: `axis` without its height, as the
/// original normalizes it.
[[nodiscard]] auto flattened(Vec3 axis) -> Vec3 {
    Vec3 flat{.x = axis.x, .y = axis.y};
    normalize(flat);
    return flat;
}

/// One command applied to one player: the original's steps, in its order, each reading and writing
/// the player as it goes.
class PlayerMove {
public:
    PlayerMove(const MovementState& state, const UserCommand& command, const MovementSettings& settings,
               const BrushHulls& world, std::span<const BrushEntity> entities,
               std::span<const SolidBox> boxes) noexcept
        : m_player{state}, m_command{command}, m_settings{settings}, m_world{world}, m_entities{entities},
          m_boxes{boxes} {}

    [[nodiscard]] auto run() -> MovementResult {
        prepare();
        if (m_player.isDead) {
            // Killed by nothing, it observes its own body, which moves no more (PM_SpectatorMove).
            m_player.viewHeight = kDeadViewHeight;
            categorizePosition();
            return result();
        }
        // Parity: never stuck in noclip; stuck, the player does nothing else
        if (m_player.moveType != MoveType::Noclip && isStuck()) {
            return result();
        }
        categorizePosition();
        m_oldWaterLevel = m_player.waterLevel;
        if (!m_player.isOnGround) {
            m_player.fallSpeed = nx::UnitsPerSecond{-m_player.velocity.z};
        }
        const BrushEntity* const ladder = ladderTouched();
        duck();
        updateStepSound();
        m_player.isOnLadder = false;
        if (m_player.moveType == MoveType::Noclip) {
            // Parity: a ladder does not hold a player in noclip, nor water, nor gravity
            noclip();
            return result();
        }
        if (ladder != nullptr && climb(*ladder)) {
            m_player.isOnLadder = true;
            // Parity: no gravity on a ladder, and jump leaves it rather than jumping
            checkWater();
            if (!m_command.buttons.test(Button::Jump)) {
                m_player.isJumpHeld = false;
            }
            slide();
            return result();
        }
        if (!isSwimming()) {
            startGravity();
        }
        if (m_player.waterJumpTime != nx::Milliseconds{}) {
            continueWaterJump();
            slide();
            checkWater();
            return result(); // Parity: no splash as a jump out of water goes on
        }
        if (m_player.waterLevel >= WaterLevel::Waist) {
            if (m_player.waterLevel == WaterLevel::Waist) {
                checkWaterJump();
            }
            if (m_player.velocity.z < 0.0F && m_player.waterJumpTime != nx::Milliseconds{}) {
                m_player.waterJumpTime = {};
            }
            readJump(true);
            swim();
            categorizePosition();
            splashAcrossTheSurface();
            return result();
        }
        readJump(ladder == nullptr);
        if (m_player.isOnGround) {
            m_player.velocity.z = 0.0F;
            applyFriction();
        }
        checkVelocity();
        if (m_player.isOnGround) {
            walk();
        } else {
            moveThroughTheAir();
        }
        categorizePosition();
        checkVelocity();
        if (!isSwimming()) {
            finishGravity();
        }
        if (m_player.isOnGround) {
            m_player.velocity.z = 0.0F;
        }
        checkFalling();
        splashAcrossTheSurface();
        return result();
    }

private:
    [[nodiscard]] auto result() const -> MovementResult {
        return MovementResult{.state = m_player, .touchedModels = m_touched, .sounds = m_sounds};
    }

    /// Where the player stands now, for the texture under it; none in the air.
    [[nodiscard]] auto groundNow() const -> std::optional<StepGround> {
        if (!m_player.isOnGround) {
            return std::nullopt;
        }
        return StepGround{.origin = m_player.origin, .ground = m_player.ground};
    }

    /// A step of the other foot (PM_PlayStepSound) at `place`, standing on `ground`, `share` of its
    /// place's volume or `volume` of its own.
    void step(StepPlace place, std::optional<StepGround> ground, float share,
              std::optional<float> volume = std::nullopt) {
        m_player.isLeftStep = !m_player.isLeftStep;
        m_sounds.emplace_back(Footstep{.place = place,
                                       .isLeftFoot = m_player.isLeftStep,
                                       .share = share,
                                       .volume = volume,
                                       .ground = ground});
    }

    /// The feet sound when their time has come and the player runs on the ground or climbs
    /// (PM_UpdateStepSound): on a ladder, wading, sloshing, or on the ground, each with the time until
    /// the next; slower and quieter ducked. Slower than kStepSpeed, nothing sounds, and the next step
    /// waits.
    void updateStepSound() {
        if (m_player.stepTime > nx::Milliseconds{}) {
            return;
        }
        // Parity: the length kept in single precision before it is compared
        if (static_cast<float>(lengthOf(m_player.velocity)) <= kStepSpeed) {
            m_player.stepTime = kQuietStepTime;
            return;
        }
        const bool isClimbing = m_player.isOnLadder;
        if (!isClimbing && !m_player.isOnGround) {
            return;
        }
        const nx::Aabb& box = m_hull == Hull::Ducked ? kDuckedBox : kStandingBox;
        const float height = box.max.z - box.min.z;
        const Vec3& origin = m_player.origin;
        const Vec3 knee{.x = origin.x,
                        .y = origin.y,
                        .z = static_cast<float>(double{origin.z} - (kKneeShare * double{height}))};
        const Vec3 feet{.x = origin.x,
                        .y = origin.y,
                        .z = static_cast<float>(double{origin.z} - (kFeetShare * double{height}))};
        StepPlace place = StepPlace::Ground;
        if (isClimbing) {
            place = StepPlace::Ladder;
            m_player.stepTime = kLadderStepTime;
        } else if (contentsAt(knee) == Contents::Water) {
            place = StepPlace::Wade;
            m_player.stepTime = kWadeStepTime;
        } else if (contentsAt(feet) == Contents::Water) {
            place = StepPlace::Slosh;
            m_player.stepTime = kStepTime;
        } else {
            m_player.stepTime = kStepTime;
        }
        float share = 1.0F;
        if (m_player.isDucked || isClimbing) {
            m_player.stepTime += kSlowerStepTime;
            if (m_player.isDucked && m_player.duckTime < kQuietDuckTime) {
                share = kQuietStepShare;
            }
        }
        step(place, groundNow(), share);
    }

    /// A landing hard enough sounds the step of the ground at a volume by how hard, and rolls the view
    /// (PM_CheckFalling); then, on the ground, the fall is over.
    void checkFalling() {
        const float fall = m_player.fallSpeed.value();
        if (m_player.isOnGround && fall >= kFallPunchSpeed) {
            float volume = kSoftLandingVolume;
            if (m_player.waterLevel == WaterLevel::Dry) {
                if (fall > kMostSafeFallSpeed) {
                    volume = kHardLandingVolume;
                } else if (fall > kMostSafeFallSpeed / 2.0F) {
                    volume = kFirmLandingVolume;
                } else if (fall < kLeastBounceSpeed) {
                    volume = 0.0F;
                }
            }
            if (volume > 0.0F) {
                step(StepPlace::Ground, groundNow(), 1.0F, volume);
                m_player.stepTime = kStepTime;
                m_player.punch.roll = nx::Degrees{static_cast<float>(double{fall} * kLandingRollPerSpeed)};
                m_player.punch.pitch = std::min(m_player.punch.pitch, kMostLandingPitch);
            }
        }
        if (m_player.isOnGround) {
            m_player.fallSpeed = {};
        }
    }

    /// Going into water or out of it splashes (PM_PlayWaterSounds).
    void splashAcrossTheSurface() {
        if ((m_oldWaterLevel == WaterLevel::Dry) != (m_player.waterLevel == WaterLevel::Dry)) {
            m_sounds.emplace_back(Splash{});
        }
    }

    /// The box, the speed limit, the command's moves capped by it, the frame time, the view and its
    /// axes, and the timers.
    void prepare() {
        m_hull = m_player.isDucked ? Hull::Ducked : Hull::Standing;
        MoveIntent& move = m_command.move;
        const float forward = move.forward.value();
        const float side = move.side.value();
        const float up = move.up.value();
        const auto requested =
            static_cast<float>(std::sqrt(double{side * side + forward * forward + up * up}));
        m_maxSpeed = m_settings.maxSpeed.value();
        if (m_player.maxSpeed.value() != 0.0F) {
            m_maxSpeed = std::min(m_player.maxSpeed.value(), m_maxSpeed);
        }
        if (requested != 0.0F && requested > m_maxSpeed) {
            const double share = double{m_maxSpeed / requested};
            move = MoveIntent{.forward = nx::UnitsPerSecond{static_cast<float>(double{forward} * share)},
                              .side = nx::UnitsPerSecond{static_cast<float>(double{side} * share)},
                              .up = nx::UnitsPerSecond{static_cast<float>(double{up} * share)}};
        }

        m_frameTime = frameTimeOf(m_command).count();
        // The weapon's kick drops back, then turns the view the moves follow.
        m_player.punch = droppedPunch(m_player.punch, m_frameTime);
        const nx::EulerAngles kicked{.pitch = m_command.viewAngles.pitch + m_player.punch.pitch,
                                     .yaw = m_command.viewAngles.yaw + m_player.punch.yaw,
                                     .roll = m_command.viewAngles.roll + m_player.punch.roll};
        nx::EulerAngles view = kicked;
        view.roll = nx::Degrees{rollFor(kicked) * kRollScale};
        if (view.yaw > nx::kHalfTurnDegrees) {
            view.yaw = view.yaw - (nx::kHalfTurnDegrees + nx::kHalfTurnDegrees);
        }
        m_axes = nx::angleVectors(view);

        for (nx::Milliseconds* const timer :
             {&m_player.stepTime, &m_player.duckTime, &m_player.swimTime, &m_player.jumpPenalty}) {
            if (*timer > nx::Milliseconds{}) {
                *timer = std::max(*timer - m_command.duration, nx::Milliseconds{});
            }
        }
        // The commands' own time, which the original's clock keeps as they come in real time.
        if (std::optional<nx::Milliseconds>& since = m_player.sinceStuckCheck) {
            *since += m_command.duration;
            if (*since > kStuckCheckInterval) {
                since.reset();
            }
        }
    }

    /// `punch` dropped back over `frameTime` seconds, as PM_DropPunchAngle drops it: its length less
    /// 10 and half of itself a second, none below 0; its length summed in float, the rest in double
    /// and rounded to float, as the original's real_t makes them.
    [[nodiscard]] static auto droppedPunch(nx::EulerAngles punch, float frameTime) -> nx::EulerAngles {
        std::array<float, 3> axes{punch.pitch.value(), punch.yaw.value(), punch.roll.value()};
        const float squares = (axes[0] * axes[0]) + (axes[1] * axes[1]) + (axes[2] * axes[2]);
        double length = std::sqrt(double{squares});
        if (length != 0.0) {
            const double inverse = 1.0 / length;
            for (float& axis : axes) {
                axis = static_cast<float>(double{axis} * inverse);
            }
        }
        length -= (kPunchDrop + (length * kPunchDropShare)) * double{frameTime};
        length = std::max(length, 0.0);
        for (float& axis : axes) {
            axis = static_cast<float>(double{axis} * length);
        }
        return nx::EulerAngles{
            .pitch = nx::Degrees{axes[0]}, .yaw = nx::Degrees{axes[1]}, .roll = nx::Degrees{axes[2]}};
    }

    /// The roll a sideways speed asks for, seen from `view`: sv_rollangle at sv_rollspeed and over,
    /// in proportion below.
    [[nodiscard]] auto rollFor(nx::EulerAngles view) const -> float {
        double side = double{nx::dot(m_player.velocity, nx::angleVectors(view).right)};
        const float sign = side < 0.0 ? -1.0F : 1.0F;
        side = std::abs(side);
        const auto rollAngle = double{m_settings.rollAngle.value()};
        const auto rollSpeed = double{m_settings.rollSpeed.value()};
        side = side < rollSpeed ? side * rollAngle / rollSpeed : rollAngle;
        return static_cast<float>(side * double{sign});
    }

    [[nodiscard]] auto isSwimming() const -> bool { return m_player.waterLevel > WaterLevel::Feet; }

    /// From the origin to the feet, or to the top of the head, of the box the player is in.
    [[nodiscard]] auto halfHeight() const -> float {
        return m_hull == Hull::Ducked ? kDuckedHalfHeight : kStandingHalfHeight;
    }

    /// The way of the player's box from `start` to `end`, by player movement's rules.
    [[nodiscard]] auto trace(Vec3 start, Vec3 end) const -> PlayerTrace { return trace(start, end, m_hull); }

    /// The same in `hull`: through the world and each solid entity, stopped by the first that stops
    /// it soonest; all the way when none does.
    [[nodiscard]] auto trace(Vec3 start, Vec3 end, Hull hull) const -> PlayerTrace {
        PlayerTrace nearest{.end = end};
        const auto meet = [&](ModelIndex model, Vec3 offset, nx::EulerAngles angles, MovementSolid solid) {
            const nx::collision::Trace hit =
                m_world.traceMovement(nx::collision::HullTraceQuery{.model = model,
                                                                    .hull = hull,
                                                                    .offset = offset,
                                                                    .angles = angles,
                                                                    .start = start,
                                                                    .end = end});
            if (hit.fraction < nearest.fraction) {
                nearest = PlayerTrace{.fraction = hit.fraction,
                                      .end = hit.end,
                                      .normal = hit.plane.normal,
                                      .startsSolid = hit.startsSolid,
                                      .isAllSolid = hit.isAllSolid,
                                      .met = solid};
            }
        };
        meet(nx::collision::kWorldModel, Vec3{}, nx::EulerAngles{}, WorldSolid{});
        for (const BrushEntity& entity : m_entities) {
            if (entity.role == BrushRole::Solid) {
                meet(entity.model, entity.origin, entity.angles, entity.model);
            }
        }
        for (const SolidBox& solid : m_boxes) {
            const nx::collision::Trace hit = nx::collision::traceBoxMovement(
                nx::collision::grownByHull(solid.box, hull), solid.origin, start, end);
            if (hit.fraction < nearest.fraction) {
                nearest = PlayerTrace{.fraction = hit.fraction,
                                      .end = hit.end,
                                      .normal = hit.plane.normal,
                                      .startsSolid = hit.startsSolid,
                                      .isAllSolid = hit.isAllSolid,
                                      .met = BoxSolid{}};
            }
        }
        return nearest;
    }

    /// Whether the player's box at `point` is in the solid of the world, of a solid entity or of a box
    /// (PM_TestPlayerPosition).
    [[nodiscard]] auto isInSolidAt(Vec3 point) const -> bool {
        const auto isSolidIn = [&](ModelIndex model, Vec3 offset, nx::EulerAngles angles) {
            return m_world.movementContentsAt(model, m_hull, point, offset, angles) == Contents::Solid;
        };
        return isSolidIn(nx::collision::kWorldModel, Vec3{}, nx::EulerAngles{}) ||
               std::ranges::any_of(m_entities,
                                   [&](const BrushEntity& entity) {
                                       return entity.role == BrushRole::Solid &&
                                              isSolidIn(entity.model, entity.origin, entity.angles);
                                   }) ||
               std::ranges::any_of(m_boxes, [&](const SolidBox& solid) {
                   return nx::collision::boxContentsAt(nx::collision::grownByHull(solid.box, m_hull),
                                                       point - solid.origin) == Contents::Solid;
               });
    }

    /// Whether the player is stuck in the solid, as the server's PM_CheckStuck finds it: once more
    /// than kStuckCheckInterval has passed since its last try, a stuck player touches what it is in
    /// and is tried at its next nudge, and one that frees it frees the player, which the nudges move
    /// from kFirstServerNudge on.
    [[nodiscard]] auto isStuck() -> bool {
        if (!isInSolidAt(m_player.origin)) {
            m_player.stuckNudge = 0;
            return false;
        }
        if (m_player.sinceStuckCheck) {
            return true;
        }
        m_player.sinceStuckCheck = nx::Milliseconds{};
        touch(modelOf(trace(m_player.origin, m_player.origin).met));
        const std::uint32_t nudge = m_player.stuckNudge;
        m_player.stuckNudge = (nudge + 1) % kStuckNudgeCount;
        const Vec3 nudged = m_player.origin + kStuckNudges[nudge];
        if (isInSolidAt(nudged)) {
            return true;
        }
        m_player.stuckNudge = 0;
        // Parity: the server leaves the smaller nudges, for the network's precision, to the client
        if (nudge >= kFirstServerNudge) {
            m_player.origin = nudged;
        }
        return false;
    }

    /// What `point` is in, as movement reads it: the world's contents, a current counted as water;
    /// where the world is not solid, the contents of the first volume the point is in.
    [[nodiscard]] auto contentsAt(Vec3 point) const -> Contents {
        Contents contents = m_world.movementContentsAt(nx::collision::kWorldModel, Hull::Point, point);
        const auto value = std::to_underlying(contents);
        if (value <= std::to_underlying(Contents::Current0) &&
            value >= std::to_underlying(Contents::CurrentDown)) {
            contents = Contents::Water;
        }
        if (contents == Contents::Solid) {
            return contents;
        }
        for (const BrushEntity& entity : m_entities) {
            if (entity.role == BrushRole::Volume &&
                m_world.movementContentsAt(entity.model, Hull::Point, point - entity.origin) !=
                    Contents::Empty) {
                return entity.contents;
            }
        }
        return contents;
    }

    /// Whether the player's box at `origin` is in solid: the world's or a solid entity's.
    /// The first ladder the player's box touches, or none.
    [[nodiscard]] auto ladderTouched() const -> const BrushEntity* {
        const auto touched = std::ranges::find_if(m_entities, [this](const BrushEntity& entity) {
            return entity.role == BrushRole::Ladder &&
                   m_world.movementContentsAt(entity.model, m_hull, m_player.origin - entity.origin) !=
                       Contents::Empty;
        });
        return touched == m_entities.end() ? nullptr : &*touched;
    }

    /// On `ladder`: the movement buttons climb along its face, towards where the view looks, at most
    /// kClimbSpeed; nothing held holds the player still. Jump pushes the player off it: then it
    /// returns false, and the player moves as it would off a ladder.
    [[nodiscard]] auto climb(const BrushEntity& ladder) -> bool {
        // Parity: the original aims at the middle of the ladder's model in its own space, wherever
        // the entity is
        const nx::Aabb bounds = m_world.boundsOf(ladder.model);
        const Vec3 middle = (bounds.min + bounds.max) * 0.5F;
        Vec3 floor = m_player.origin;
        floor.z += -halfHeight() - kFloorProbeDepth;
        const bool isOnFloor = contentsAt(floor) == Contents::Solid;
        const nx::collision::Trace face =
            m_world.trace(nx::collision::HullTraceQuery{.model = ladder.model,
                                                        .hull = Hull::Point,
                                                        .offset = ladder.origin,
                                                        .start = m_player.origin,
                                                        .end = middle});
        if (face.fraction == 1.0F) {
            return true;
        }
        float speed = std::min(kClimbSpeed, m_maxSpeed);
        if (m_player.isDucked) {
            speed = static_cast<float>(double{speed} * kDuckSpeedShare);
        }
        const auto held = [this, speed](Button positive, Button negative) {
            return (m_command.buttons.test(positive) ? speed : 0.0F) -
                   (m_command.buttons.test(negative) ? speed : 0.0F);
        };
        const float forward = held(Button::Forward, Button::Back);
        const float right = held(Button::MoveRight, Button::MoveLeft);
        const Vec3 normal = face.plane.normal;
        if (m_command.buttons.test(Button::Jump)) {
            m_player.velocity = normal * kLadderJumpSpeed;
            return false;
        }
        if (forward == 0.0F && right == 0.0F) {
            m_player.velocity = {};
            return true;
        }
        // What goes into the face turns into going up or down along it.
        const Vec3 wish = (m_axes.forward * forward) + (m_axes.right * right);
        Vec3 across = nx::cross(Vec3{.z = 1.0F}, normal);
        normalize(across);
        const float into = nx::dot(wish, normal);
        const Vec3 lateral = wish - (normal * into);
        m_player.velocity = lateral + (nx::cross(normal, across) * -into);
        if (isOnFloor && into > 0.0F) {
            m_player.velocity = m_player.velocity + (normal * kClimbSpeed);
        }
        return true;
    }

    /// The water level and type: the feet, the middle of the box and the eyes, in turn.
    void checkWater() {
        m_player.waterLevel = WaterLevel::Dry;
        m_player.waterType = Contents::Empty;
        const Vec3& origin = m_player.origin;
        Vec3 point{.x = origin.x, .y = origin.y, .z = origin.z - halfHeight() + kFeetProbeHeight};
        const Contents feet = contentsAt(point);
        if (!isLiquid(feet)) {
            return;
        }
        m_player.waterType = feet;
        m_player.waterLevel = WaterLevel::Feet;
        point.z = origin.z;
        if (isLiquid(contentsAt(point))) {
            m_player.waterLevel = WaterLevel::Waist;
            point.z = origin.z + m_player.viewHeight.value();
            if (isLiquid(contentsAt(point))) {
                m_player.waterLevel = WaterLevel::Eyes;
            }
        }
    }

    /// The water, and whether the player stands on ground: a plane at most kGroundProbeDepth below,
    /// facing up enough, onto which it then drops when it is not swimming.
    void categorizePosition() {
        checkWater();
        const Vec3 below = m_player.origin - Vec3{.z = kGroundProbeDepth};
        if (m_player.velocity.z > kLiftOffSpeed) {
            m_player.isOnGround = false;
            m_player.ground = WorldSolid{};
            return;
        }
        const PlayerTrace ground = trace(m_player.origin, below);
        m_player.isOnGround = ground.normal.z >= kMinGroundNormalZ;
        m_player.ground = m_player.isOnGround ? ground.met : MovementSolid{WorldSolid{}};
        if (m_player.isOnGround) {
            m_player.waterJumpTime = {};
            if (!isSwimming() && !ground.startsSolid && !ground.isAllSolid) {
                m_player.origin = ground.end;
            }
        }
        touch(modelOf(ground.met));
    }

    /// Remembers that the player met the entity of `model`, when there is one, once.
    void touch(std::optional<ModelIndex> model) {
        if (model && !std::ranges::contains(m_touched, *model)) {
            m_touched.push_back(*model);
        }
    }

    /// Every axis of the velocity a number and within sv_maxvelocity, and the origin a number.
    void checkVelocity() {
        const float limit = m_settings.maxVelocity.value();
        for (float* const component : {&m_player.velocity.x, &m_player.velocity.y, &m_player.velocity.z}) {
            if (std::isnan(*component)) {
                *component = 0.0F;
            }
            if (*component > limit) {
                *component = limit;
            } else if (*component < -limit) {
                *component = -limit;
            }
        }
        for (float* const component : {&m_player.origin.x, &m_player.origin.y, &m_player.origin.z}) {
            if (std::isnan(*component)) {
                *component = 0.0F;
            }
        }
    }

    /// Half of gravity's pull over the command, before the move: its product exact in double precision.
    void startGravity() {
        if (m_player.waterJumpTime != nx::Milliseconds{}) {
            return;
        }
        constexpr double kHalf = 0.5;
        m_player.velocity.z = static_cast<float>(
            double{m_player.velocity.z} - double{m_settings.gravity.value()} * kHalf * double{m_frameTime});
        checkVelocity();
    }

    /// The other half, after it: gravity times the time in single precision, as the original rounds it.
    void finishGravity() {
        constexpr double kHalf = 0.5;
        m_player.velocity.z = static_cast<float>(double{m_player.velocity.z} -
                                                 double{m_settings.gravity.value() * m_frameTime} * kHalf);
        checkVelocity();
    }

    /// The duck button: a press starts a duck, holding it goes on ducking, letting go stands up. A
    /// duck on the ground takes its time, the eyes easing down, and then drops the origin into the
    /// ducked box; in the air it is at once. Ducking or ducked, the keys ask for a third of their
    /// speed.
    void duck() {
        const bool isHeld = m_command.buttons.test(Button::Duck);
        const bool isPressed = isHeld && !m_player.isDuckHeld;
        m_player.isDuckHeld = isHeld;
        if (!isHeld && !m_player.isDucking && !m_player.isDucked) {
            return;
        }
        MoveIntent& move = m_command.move;
        for (nx::UnitsPerSecond* const speed : {&move.forward, &move.side, &move.up}) {
            *speed = nx::UnitsPerSecond{static_cast<float>(double{speed->value()} * kDuckSpeedShare)};
        }
        if (!isHeld) {
            standUp();
            return;
        }
        if (isPressed && !m_player.isDucked) {
            m_player.duckTime = kDuckTime;
            m_player.isDucking = true;
        }
        if (!m_player.isDucking) {
            return;
        }
        const double left = double{m_player.duckTime.count()} / double{kDuckTime.count()};
        if (left > kDuckLeftWhenDown && m_player.isOnGround) {
            // Going down: the eyes ease from the standing height towards the ducked one, where the
            // ducked box will put them once the origin drops.
            const auto fraction = double{easeInOut(static_cast<float>(1.0 - left),
                                                   static_cast<float>(1.0 / (1.0 - kDuckLeftWhenDown)))};
            const float duckedEyes = kDuckedViewHeight.value() - (kStandingHalfHeight - kDuckedHalfHeight);
            m_player.viewHeight = nx::HammerUnits{static_cast<float>(
                (double{duckedEyes} * fraction) + (double{kStandingViewHeight.value()} * (1.0 - fraction)))};
            return;
        }
        m_hull = Hull::Ducked;
        m_player.viewHeight = kDuckedViewHeight;
        m_player.isDucked = true;
        m_player.isDucking = false;
        if (m_player.isOnGround) {
            m_player.origin.z = static_cast<float>(double{m_player.origin.z} - kDuckDrop);
            unstickCrouch();
            categorizePosition();
        }
    }

    /// Stands up where the standing box fits: as it is in the air, raised onto the feet on the
    /// ground; else the player stays ducked.
    void standUp() {
        Vec3 standing = m_player.origin;
        if (m_player.isOnGround) {
            // Parity: a player still going down, its box standing already, is raised too: the ducktap
            standing.z = static_cast<float>(double{standing.z} + kDuckDrop);
        }
        if (trace(standing, standing).startsSolid) {
            return;
        }
        m_hull = Hull::Standing;
        if (trace(standing, standing).startsSolid) {
            m_hull = Hull::Ducked;
            return;
        }
        m_player.isDucked = false;
        m_player.isDucking = false;
        m_player.viewHeight = kStandingViewHeight;
        m_player.duckTime = {};
        m_player.stepTime = std::max(m_player.stepTime - kSlowerStepTime, nx::Milliseconds{});
        m_player.origin = standing;
        categorizePosition();
    }

    /// A player that ducking put into solid is raised out of it, a unit at a time; or left as it is.
    void unstickCrouch() {
        if (!isInSolidAt(m_player.origin)) {
            return;
        }
        Vec3 raised = m_player.origin;
        for (int step = 0; step < kMaxCrouchUnstick; ++step) {
            raised.z += 1.0F;
            if (!isInSolidAt(raised)) {
                m_player.origin = raised;
                return;
            }
        }
    }

    /// Jumps when the button is pressed and `mayJump`, and remembers whether it is held.
    void readJump(bool mayJump) {
        if (!m_command.buttons.test(Button::Jump)) {
            m_player.isJumpHeld = false;
        } else if (mayJump) {
            jump();
        }
    }

    void jump() {
        if (m_player.waterJumpTime != nx::Milliseconds{}) {
            m_player.waterJumpTime =
                std::max(m_player.waterJumpTime - m_command.duration, nx::Milliseconds{});
            return;
        }
        if (isSwimming()) {
            m_player.isOnGround = false;
            m_player.velocity.z = m_player.waterType == Contents::Water   ? kSwimUpInWater
                                  : m_player.waterType == Contents::Slime ? kSwimUpInSlime
                                                                          : kSwimUpInLava;
            if (m_player.swimTime <= nx::Milliseconds{}) {
                m_player.swimTime = kSwimSplashTime;
                m_sounds.emplace_back(Splash{});
            }
            return;
        }
        if (!m_player.isOnGround) {
            m_player.isJumpHeld = true;
            return;
        }
        if (m_player.isJumpHeld) {
            return; // Parity: no jump again until the button is released
        }
        // What it jumps off, before it leaves it.
        const std::optional<StepGround> ground = groundNow();
        m_player.isOnGround = false;
        limitBunnyHop();
        if (lengthOf(m_player.velocity) >= double{kStepSpeed}) {
            step(StepPlace::Ground, ground, 1.0F, kJumpStepVolume);
        }
        m_player.velocity.z = static_cast<float>(std::sqrt(2.0 * kJumpGravity * kJumpHeight));
        if (m_player.jumpPenalty > nx::Milliseconds{}) {
            m_player.velocity.z =
                static_cast<float>(double{m_player.velocity.z} * penaltyShare(m_player.jumpPenalty));
        }
        m_player.jumpPenalty = kJumpPenalty;
        finishGravity();
        m_player.isJumpHeld = true;
    }

    /// A jump faster than kBunnyHopSpeedFactor times the speed limit is slowed below it, Counter-Strike's
    /// limit on bunny hopping.
    void limitBunnyHop() {
        const float limit = kBunnyHopSpeedFactor * m_maxSpeed;
        if (limit <= 0.0F) {
            return;
        }
        const double speed = lengthOf(m_player.velocity);
        if (speed <= double{limit}) {
            return;
        }
        m_player.velocity *= static_cast<float>(double{limit} / speed * kBunnyHopSlowdown);
    }

    /// Ground friction: what the speed loses over the command, at least as if it were sv_stopspeed;
    /// more near a drop.
    void applyFriction() {
        const Vec3 velocity = m_player.velocity;
        const auto speed = static_cast<float>(std::sqrt(double{nx::dot(velocity, velocity)}));
        if (speed < kMinFrictionSpeed) {
            return;
        }
        Vec3 ahead{.x = m_player.origin.x + velocity.x / speed * kEdgeLookAhead,
                   .y = m_player.origin.y + velocity.y / speed * kEdgeLookAhead,
                   .z = m_player.origin.z - halfHeight()};
        const PlayerTrace drop =
            trace(ahead, Vec3{.x = ahead.x, .y = ahead.y, .z = ahead.z - kEdgeDropDepth});
        const float friction = m_settings.friction.value();
        const double frictionHere =
            drop.fraction == 1.0F ? double{friction * m_settings.edgeFriction.value()} : double{friction};
        const float stopSpeed = m_settings.stopSpeed.value();
        const double control = double{speed < stopSpeed ? stopSpeed : speed};
        const double lost = frictionHere * (control * double{m_frameTime});
        const double share = std::max(double{speed} - lost, 0.0) / double{speed};
        // Parity: the original scales x by the share in double precision, y and z by it rounded.
        const auto rounded = static_cast<float>(share);
        m_player.velocity = Vec3{.x = static_cast<float>(double{velocity.x} * share),
                                 .y = velocity.y * rounded,
                                 .z = velocity.z * rounded};
    }

    /// Speeds the player up towards `direction`, until its speed along it is `wishSpeed`.
    void accelerate(Vec3 direction, double wishSpeed, float rate) {
        const double current = double{nx::dot(m_player.velocity, direction)};
        const auto missing = static_cast<float>(wishSpeed - current);
        if (missing <= 0.0F) {
            return;
        }
        const double gain = std::min(double{rate * m_frameTime} * wishSpeed, double{missing});
        m_player.velocity = addScaled(m_player.velocity, gain, direction);
    }

    /// Noclip: through everything, along the view at the speeds the command asks and up at its up
    /// speed. No velocity is left, so that gravity gathers none.
    void noclip() {
        Vec3 forward = m_axes.forward;
        Vec3 right = m_axes.right;
        normalize(forward);
        normalize(right);
        Vec3 wish = (forward * m_command.move.forward.value()) + (right * m_command.move.side.value());
        wish.z += m_command.move.up.value();
        m_player.origin = m_player.origin + (wish * m_frameTime);
        m_player.velocity = Vec3{};
    }

    /// On the ground: the jump penalty, acceleration, then the move, up a stair when that goes farther.
    void walk() {
        if (m_player.jumpPenalty > nx::Milliseconds{}) {
            const double share = penaltyShare(m_player.jumpPenalty);
            m_player.velocity.x = static_cast<float>(double{m_player.velocity.x} * share);
            m_player.velocity.y = static_cast<float>(double{m_player.velocity.y} * share);
        }
        const float forwardMove = m_command.move.forward.value();
        const float sideMove = m_command.move.side.value();
        const Vec3 forward = flattened(m_axes.forward);
        const Vec3 right = flattened(m_axes.right);
        Vec3 direction{.x = forward.x * forwardMove + right.x * sideMove,
                       .y = forward.y * forwardMove + right.y * sideMove};
        const double wishSpeed = std::min(normalize(direction), double{m_maxSpeed});

        m_player.velocity.z = 0.0F;
        accelerate(direction, wishSpeed, m_settings.accelerate.value());
        m_player.velocity.z = 0.0F;
        if (lengthOf(m_player.velocity) < kMinWalkSpeed) {
            m_player.velocity = {};
            return;
        }

        const Vec3 start = m_player.origin;
        const Vec3 destination{.x = start.x + m_player.velocity.x * m_frameTime,
                               .y = start.y + m_player.velocity.y * m_frameTime,
                               .z = start.z};
        if (const PlayerTrace direct = trace(start, destination); direct.fraction == 1.0F) {
            m_player.origin = direct.end;
            return;
        }

        // Blocked: slide along the ground, and up a stair then down onto it; keep whichever went farther.
        const Vec3 startVelocity = m_player.velocity;
        slide();
        const Vec3 down = m_player.origin;
        const Vec3 downVelocity = m_player.velocity;

        m_player.origin = start;
        m_player.velocity = startVelocity;
        const float stepSize = m_settings.stepSize.value();
        if (const PlayerTrace lift = trace(start, start + Vec3{.z = stepSize});
            !lift.startsSolid && !lift.isAllSolid) {
            m_player.origin = lift.end;
        }
        slide();
        const PlayerTrace land = trace(m_player.origin, m_player.origin - Vec3{.z = stepSize});
        if (land.normal.z < kMinGroundNormalZ) {
            m_player.origin = down;
            m_player.velocity = downVelocity;
            return;
        }
        if (!land.startsSolid && !land.isAllSolid) {
            m_player.origin = land.end;
        }
        const Vec3 up = m_player.origin;
        const auto horizontalSquared = [start](Vec3 end) {
            return (end.x - start.x) * (end.x - start.x) + (end.y - start.y) * (end.y - start.y);
        };
        if (horizontalSquared(down) > horizontalSquared(up)) {
            m_player.origin = down;
            m_player.velocity = downVelocity;
        } else {
            m_player.velocity.z = downVelocity.z;
        }
    }

    /// In the air: acceleration capped at kAirWishSpeed along the wish, then the move.
    void moveThroughTheAir() {
        const float forwardMove = m_command.move.forward.value();
        const float sideMove = m_command.move.side.value();
        const Vec3 forward = flattened(m_axes.forward);
        const Vec3 right = flattened(m_axes.right);
        Vec3 direction{.x = forward.x * forwardMove + right.x * sideMove,
                       .y = forward.y * forwardMove + right.y * sideMove};
        const float wishSpeed = std::min(static_cast<float>(normalize(direction)), m_maxSpeed);

        const float capped = std::min(wishSpeed, kAirWishSpeed);
        const double current = double{nx::dot(m_player.velocity, direction)};
        if (const auto missing = static_cast<float>(double{capped} - current); missing > 0.0F) {
            // Parity: the gain grows with the whole wish, not the capped one: the root of air strafing.
            const double gain =
                std::min(double{m_settings.airAccelerate.value() * wishSpeed * m_frameTime}, double{missing});
            m_player.velocity = addScaled(m_player.velocity, gain, direction);
        }
        slide();
    }

    /// The move over the command's time, sliding along what it meets: the original's PM_FlyMove.
    void slide() {
        const Vec3 primalVelocity = m_player.velocity;
        Vec3 originalVelocity = m_player.velocity;
        std::array<Vec3, kMaxClipPlanes> planes{};
        std::size_t planeCount = 0;
        float allFraction = 0.0F;
        float timeLeft = m_frameTime;

        for (int bump = 0; bump < kMaxBumps; ++bump) {
            Vec3& velocity = m_player.velocity;
            if (velocity == Vec3{}) {
                break;
            }
            const Vec3& origin = m_player.origin;
            const Vec3 end{.x = static_cast<float>(double{origin.x} + double{timeLeft * velocity.x}),
                           .y = static_cast<float>(double{origin.y} + double{timeLeft * velocity.y}),
                           .z = static_cast<float>(double{origin.z} + double{timeLeft * velocity.z})};
            const PlayerTrace hit = trace(origin, end);
            allFraction += hit.fraction;
            if (hit.isAllSolid) {
                velocity = {};
                return;
            }
            if (hit.fraction > 0.0F) {
                m_player.origin = hit.end;
                originalVelocity = velocity;
                planeCount = 0;
            }
            if (hit.fraction == 1.0F) {
                break;
            }
            touch(modelOf(hit.met));
            timeLeft -= timeLeft * hit.fraction;
            if (planeCount >= kMaxClipPlanes) {
                velocity = {};
                break;
            }
            planes[planeCount++] = hit.normal;

            if (planeCount == 1 && !m_player.isOnGround) {
                velocity = clipVelocity(originalVelocity, planes[0]);
                originalVelocity = velocity;
                continue;
            }
            // Along the first plane whose slide enters none of the others; else along the crease of two.
            std::size_t along = 0;
            for (; along < planeCount; ++along) {
                velocity = clipVelocity(originalVelocity, planes[along]);
                const auto entersAnother = [&](std::size_t other) {
                    return other != along && nx::dot(velocity, planes[other]) < 0.0F;
                };
                if (std::ranges::none_of(std::views::iota(std::size_t{0}, planeCount), entersAnother)) {
                    break;
                }
            }
            if (along == planeCount) {
                if (planeCount != 2) {
                    velocity = {};
                    break;
                }
                const Vec3 crease = nx::cross(planes[0], planes[1]);
                velocity = crease * nx::dot(crease, velocity);
            }
            if (nx::dot(velocity, primalVelocity) <= 0.0F) {
                velocity =
                    {}; // Parity: a slide turned back against the move stops it, so corners do not jitter
                break;
            }
        }
        if (allFraction == 0.0F) {
            m_player.velocity = {};
        }
    }

    /// Swimming: the wish along the full view, sinking without a key held, water's friction and
    /// acceleration; then the move, as if from a stair above it.
    void swim() {
        const float forwardMove = m_command.move.forward.value();
        const float sideMove = m_command.move.side.value();
        const float upMove = m_command.move.up.value();
        Vec3 wish = m_axes.forward * forwardMove + m_axes.right * sideMove;
        if (forwardMove == 0.0F && sideMove == 0.0F && upMove == 0.0F) {
            wish.z -= kSinkSpeed;
        } else {
            wish.z += upMove;
        }
        Vec3 direction = wish;
        double wishSpeed = normalize(direction);
        if (wishSpeed > double{m_maxSpeed}) {
            wish *= static_cast<float>(double{m_maxSpeed} / wishSpeed);
            wishSpeed = double{m_maxSpeed};
        }
        wishSpeed *= kSwimSpeedShare;

        Vec3 heading = m_player.velocity;
        const double speed = normalize(heading);
        float newSpeed = 0.0F;
        if (speed != 0.0) {
            newSpeed = std::max(
                static_cast<float>(speed - double{m_settings.friction.value() * m_frameTime} * speed), 0.0F);
            m_player.velocity *= static_cast<float>(double{newSpeed} / speed);
        }
        const auto wished = static_cast<float>(wishSpeed);
        if (wished < kMinSwimSpeed) {
            return;
        }
        if (const float missing = wished - newSpeed; missing > 0.0F) {
            normalize(wish);
            const double gain =
                std::min(double{m_settings.accelerate.value() * m_frameTime * wished}, double{missing});
            m_player.velocity = addScaled(m_player.velocity, gain, wish);
        }

        // Parity: the original moves a swimmer down from a stair above where it goes, not from where it is.
        const Vec3 destination = m_player.origin + m_player.velocity * m_frameTime;
        const Vec3 above = destination + Vec3{.z = m_settings.stepSize.value() + 1.0F};
        if (const PlayerTrace drop = trace(above, destination); !drop.startsSolid && !drop.isAllSolid) {
            m_player.origin = drop.end;
            return;
        }
        slide();
    }

    /// At the surface, facing a wall it can climb out over: starts a jump out of the water.
    void checkWaterJump() {
        if (m_player.waterJumpTime != nx::Milliseconds{} || m_player.velocity.z < -kMaxSinkSpeedToHop) {
            return;
        }
        Vec3 flatVelocity{.x = m_player.velocity.x, .y = m_player.velocity.y};
        const auto speed = static_cast<float>(normalize(flatVelocity));
        const Vec3 forward = flattened(m_axes.forward);
        if (speed != 0.0F && nx::dot(flatVelocity, forward) < 0.0F) {
            return; // backing into the water
        }
        Vec3 start = m_player.origin + Vec3{.z = kWaterJumpProbeHeight};
        const PlayerTrace wall = trace(start, start + forward * kWaterJumpReach, Hull::Point);
        if (!(wall.fraction < 1.0F && std::abs(double{wall.normal.z}) < double{kWallNormalZ})) {
            return;
        }
        start.z += halfHeight() - kWaterJumpProbeHeight;
        m_player.waterJumpVelocity = Vec3{} + wall.normal * -kWaterJumpPush;
        if (trace(start, start + forward * kWaterJumpReach, Hull::Point).fraction == 1.0F) {
            m_player.waterJumpTime = kWaterJumpDuration;
            m_player.velocity.z = kWaterJumpSpeed;
            m_player.isJumpHeld = true;
        }
    }

    /// A jump out of water under way: its time runs out, or ends with the water, pushing the player on.
    void continueWaterJump() {
        m_player.waterJumpTime = std::min(m_player.waterJumpTime, kMaxWaterJumpTime);
        m_player.waterJumpTime -= m_command.duration;
        if (m_player.waterJumpTime < nx::Milliseconds{} || m_player.waterLevel == WaterLevel::Dry) {
            m_player.waterJumpTime = {};
        }
        m_player.velocity.x = m_player.waterJumpVelocity.x;
        m_player.velocity.y = m_player.waterJumpVelocity.y;
    }

    MovementState m_player;
    UserCommand m_command;
    const MovementSettings& m_settings;
    const BrushHulls& m_world;
    std::span<const BrushEntity> m_entities;
    std::span<const SolidBox> m_boxes;
    std::vector<ModelIndex> m_touched;            ///< The models of the entities met, in order.
    std::vector<MovementSound> m_sounds;          ///< Made, in order.
    WaterLevel m_oldWaterLevel = WaterLevel::Dry; ///< As the command started.
    Hull m_hull = Hull::Standing;                 ///< The player's box.
    float m_maxSpeed = 0.0F;
    float m_frameTime = 0.0F; ///< Seconds.
    nx::AngleVectors m_axes{};
};

} // namespace

auto solidBoxesOf(nx::ecs::World& world, std::optional<nx::ecs::Entity> ignored) -> std::vector<SolidBox> {
    std::vector<SolidBox> boxes;
    nx::ecs::Query<nx::ecs::Entity, const nx::game::Transform&, const nx::game::BoxSolidity&>{world}.forEach(
        [&boxes, ignored](nx::ecs::Entity entity, const nx::game::Transform& transform,
                          const nx::game::BoxSolidity& solidity) {
            if (ignored == entity) {
                return;
            }
            boxes.push_back(SolidBox{.origin = transform.origin, .box = solidity.box});
        });
    return boxes;
}

auto brushEntitiesOf(nx::ecs::World& world) -> std::vector<BrushEntity> {
    std::vector<BrushEntity> entities;
    nx::ecs::Query<const nx::game::BrushModel&, const nx::game::Transform&, const nx::game::BrushSolidity&>
        brushes{world};
    brushes.forEach([&](const nx::game::BrushModel& model, const nx::game::Transform& transform,
                        const nx::game::BrushSolidity& solidity) {
        if (solidity.solid == nx::game::Solid::Bsp) {
            entities.push_back(
                {.model = model.index, .origin = transform.origin, .angles = transform.angles});
        } else if (solidity.contents == Contents::Ladder) {
            entities.push_back({.model = model.index, .origin = transform.origin, .role = BrushRole::Ladder});
        } else if (solidity.contents) {
            entities.push_back({.model = model.index,
                                .origin = transform.origin,
                                .role = BrushRole::Volume,
                                .contents = *solidity.contents});
        }
    });
    return entities;
}

auto modelOf(const MovementSolid& solid) noexcept -> std::optional<ModelIndex> {
    const ModelIndex* const model = std::get_if<ModelIndex>(&solid);
    return model != nullptr ? std::optional{*model} : std::nullopt;
}

auto bodyOf(const MovementState& state) -> nx::game::Body {
    return nx::game::Body{.hull = state.isDucked ? Hull::Ducked : Hull::Standing,
                          .box = state.isDucked ? kDuckedBox : kStandingBox,
                          .groundModel = state.isOnGround ? modelOf(state.ground) : std::nullopt,
                          .isPushed = state.moveType == MoveType::Walk && !state.isDead};
}

auto frameTimeOf(const UserCommand& command) noexcept -> nx::Seconds {
    return nx::Seconds{static_cast<float>(double{command.duration.count()} * kSecondsPerMillisecond)};
}

auto simulateMovement(const MovementState& state, const UserCommand& command,
                      const MovementSettings& settings, const BrushHulls& hulls,
                      std::span<const BrushEntity> entities, std::span<const SolidBox> boxes)
    -> MovementResult {
    nx::expects(hulls.modelCount() > 0, "simulateMovement: the world is model 0 of its hulls");
    nx::expects(
        std::ranges::all_of(
            entities, [&](const BrushEntity& entity) { return entity.model.value < hulls.modelCount(); }),
        "simulateMovement: every entity is a model of the hulls");
    return PlayerMove{state, command, settings, hulls, entities, boxes}.run();
}

} // namespace cs
