// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.math;
import nx.collision;

namespace cs {

namespace {

using nx::Vec3;
using nx::collision::BrushHulls;
using nx::collision::Contents;
using nx::collision::Hull;

// A standing player's box, about its origin: the size the map compilers grow hull 1 by.

/// From its origin to its feet, and to the top of its head.
constexpr float kStandingHalfHeight = 36.0F;

// The thresholds of the original's movement, each a behavior a player feels.

/// Ground faces up at least this much; on a steeper plane a player slides.
constexpr float kMinGroundNormalZ = 0.7F;
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
/// Water: the feet are this far above the bottom of the box.
constexpr float kFeetProbeHeight = 1.0F;
/// Without a key held, a swimmer drifts down at this speed.
constexpr float kSinkSpeed = 60.0F;
/// A swimmer wishes for this share of the speed its keys ask for.
constexpr double kSwimSpeedShare = 0.8;
/// Slower wishes do not move a swimmer.
constexpr float kMinSwimSpeed = 0.1F;
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
               const BrushHulls& world) noexcept
        : m_player{state}, m_command{command}, m_settings{settings}, m_world{world} {}

    [[nodiscard]] auto run() -> MovementState {
        prepare();
        categorizePosition();
        if (!isSwimming()) {
            startGravity();
        }
        if (m_player.waterJumpTime != nx::Milliseconds{}) {
            continueWaterJump();
            slide();
            checkWater();
            return m_player;
        }
        if (m_player.waterLevel >= WaterLevel::Waist) {
            if (m_player.waterLevel == WaterLevel::Waist) {
                checkWaterJump();
            }
            if (m_player.velocity.z < 0.0F && m_player.waterJumpTime != nx::Milliseconds{}) {
                m_player.waterJumpTime = {};
            }
            readJump();
            swim();
            categorizePosition();
            return m_player;
        }
        readJump();
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
        return m_player;
    }

private:
    /// The speed limit, the command's moves capped by it, the frame time, the view and its axes.
    void prepare() {
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

        nx::EulerAngles view = m_command.viewAngles;
        view.roll = nx::Degrees{rollFor(m_command.viewAngles) * kRollScale};
        if (view.yaw > nx::kHalfTurnDegrees) {
            view.yaw = view.yaw - (nx::kHalfTurnDegrees + nx::kHalfTurnDegrees);
        }
        m_axes = nx::angleVectors(view);

        m_frameTime = static_cast<float>(double{m_command.duration.count()} * kSecondsPerMillisecond);
        if (m_player.jumpPenalty > nx::Milliseconds{}) {
            m_player.jumpPenalty = std::max(m_player.jumpPenalty - m_command.duration, nx::Milliseconds{});
        }
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

    /// The way from `start` to `end` in `hull` of the world, by player movement's rules.
    [[nodiscard]] auto trace(Vec3 start, Vec3 end, Hull hull = Hull::Standing) const -> PlayerTrace {
        const nx::collision::Trace hit =
            m_world.traceMovement(nx::collision::HullTraceQuery{.hull = hull, .start = start, .end = end});
        if (hit.fraction < 1.0F) {
            return PlayerTrace{.fraction = hit.fraction,
                               .end = hit.end,
                               .normal = hit.plane.normal,
                               .startsSolid = hit.startsSolid,
                               .isAllSolid = hit.isAllSolid};
        }
        return PlayerTrace{.end = end};
    }

    /// What `point` is in, as movement reads it: a current is water.
    [[nodiscard]] auto contentsAt(Vec3 point) const -> Contents {
        const Contents contents = m_world.movementContentsAt(0, Hull::Point, point);
        const auto value = std::to_underlying(contents);
        if (value <= std::to_underlying(Contents::Current0) &&
            value >= std::to_underlying(Contents::CurrentDown)) {
            return Contents::Water;
        }
        return contents;
    }

    /// The water level and type: the feet, the middle of the box and the eyes, in turn.
    void checkWater() {
        m_player.waterLevel = WaterLevel::Dry;
        m_player.waterType = Contents::Empty;
        const Vec3& origin = m_player.origin;
        Vec3 point{.x = origin.x, .y = origin.y, .z = origin.z - kStandingHalfHeight + kFeetProbeHeight};
        const Contents feet = contentsAt(point);
        if (!isLiquid(feet)) {
            return;
        }
        m_player.waterType = feet;
        m_player.waterLevel = WaterLevel::Feet;
        point.z = origin.z;
        if (isLiquid(contentsAt(point))) {
            m_player.waterLevel = WaterLevel::Waist;
            point.z = origin.z + kStandingViewHeight.value();
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
            return;
        }
        const PlayerTrace ground = trace(m_player.origin, below);
        m_player.isOnGround = ground.normal.z >= kMinGroundNormalZ;
        if (m_player.isOnGround) {
            m_player.waterJumpTime = {};
            if (!isSwimming() && !ground.startsSolid && !ground.isAllSolid) {
                m_player.origin = ground.end;
            }
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

    /// Jumps when the button is pressed, and remembers whether it is held.
    void readJump() {
        if (m_command.buttons.test(Button::Jump)) {
            jump();
        } else {
            m_player.isJumpHeld = false;
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
            return;
        }
        if (!m_player.isOnGround) {
            m_player.isJumpHeld = true;
            return;
        }
        if (m_player.isJumpHeld) {
            return; // Parity: no jump again until the button is released
        }
        m_player.isOnGround = false;
        limitBunnyHop();
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
                   .z = m_player.origin.z - kStandingHalfHeight};
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
        start.z += kStandingHalfHeight - kWaterJumpProbeHeight;
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
    float m_maxSpeed = 0.0F;
    float m_frameTime = 0.0F; ///< Seconds.
    nx::AngleVectors m_axes{};
};

} // namespace

auto simulateMovement(const MovementState& state, const UserCommand& command,
                      const MovementSettings& settings, const BrushHulls& world) -> MovementState {
    nx::expects(world.modelCount() > 0, "simulateMovement: the world is model 0 of its hulls");
    return PlayerMove{state, command, settings, world}.run();
}

} // namespace cs
