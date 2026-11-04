// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.agents;
import std;
import nx.core;
import nx.math;
import nx.navigation;

namespace nx::agents {
namespace {
[[nodiscard]] auto isPositive(float value) noexcept -> bool {
    return std::isfinite(value) && value > 0.0F;
}
[[nodiscard]] auto isValid(const TimeRange& range) noexcept -> bool {
    return isPositive(range.shortest.count()) && std::isfinite(range.longest.count()) &&
           range.shortest <= range.longest;
}
[[nodiscard]] auto distanceAcross(Vec3 first, Vec3 second) noexcept -> float {
    return std::hypot(first.x - second.x, first.y - second.y);
}
/// How much of its stuck speed a body crouching is held to: it goes at a third of its speed, and gains it far
/// slower, at 83 units a second each second, so that one turning back there went 5 units in the half a second
/// the stuck speed of 20 asks 10 of.
constexpr float kCrouchedStuckShare = 1.0F / 3.0F;
} // namespace

auto isValid(const StuckRules& rules) noexcept -> bool {
    return isPositive(rules.sampleInterval.count()) && rules.samples > 0 &&
           isPositive(rules.stuckSpeed.value()) && isPositive(rules.unstuckDistance.value()) &&
           isValid(rules.wiggle) && std::isfinite(rules.firstJump.count()) && rules.firstJump >= Seconds{} &&
           isValid(rules.jumpInterval) && isPositive(rules.giveUpTime.count()) &&
           std::isfinite(rules.avoidedCost) && rules.avoidedCost >= 1.0F;
}

StuckWatch::StuckWatch(StuckRules settings, Random drawn) : rules{settings}, random{drawn} {
    expects(isValid(rules), "StuckWatch: valid rules");
    sampled.reserve(std::size_t{rules.samples} + 1);
}

auto StuckWatch::watch(Intent& intent, Vec3 feet, UnitsPerSecond maxSpeed, bool moves, Seconds elapsed)
    -> Progress {
    if (!moves || maxSpeed <= rules.stuckSpeed) {
        reset();
        return Progress::Moving;
    }
    if (!stuckAt) {
        sample(feet, elapsed);
        if (!isTooSlow(intent)) {
            return Progress::Moving;
        }
        // Stuck: where, and its first jump soon (StuckCheck's m_stuckJumpTimestamp).
        stuckAt = feet;
        lastStuckAt = feet;
        stuckFor = {};
        wiggleLeft = {};
        jumpIn = Seconds{random.between(0.0F, rules.firstJump.count())};
    } else if (length(feet - *stuckAt) > rules.unstuckDistance.value()) {
        reset();
        return Progress::Moving;
    } else {
        stuckFor += elapsed;
        if (stuckFor >= rules.giveUpTime) {
            reset();
            return Progress::GivesUp;
        }
    }
    wiggle(intent, elapsed);
    return Progress::Stuck;
}

auto StuckWatch::stuckPlace() const noexcept -> navigation::AvoidedPlace {
    return {.position = lastStuckAt, .radius = rules.unstuckDistance, .cost = rules.avoidedCost};
}

void StuckWatch::reset() noexcept {
    sampled.clear();
    sinceSample = {};
    stuckAt.reset();
    stuckFor = {};
}

void StuckWatch::sample(Vec3 feet, Seconds elapsed) {
    sinceSample += elapsed;
    if (!sampled.empty() && sinceSample < rules.sampleInterval) {
        return;
    }
    sinceSample = sampled.empty() ? Seconds{} : sinceSample - rules.sampleInterval;
    if (sampled.size() > rules.samples) {
        sampled.erase(sampled.begin());
    }
    sampled.push_back(feet);
}

auto StuckWatch::isTooSlow(const Intent& intent) const noexcept -> bool {
    if (sampled.size() <= rules.samples) {
        return false;
    }
    const float span = rules.sampleInterval.count() * static_cast<float>(rules.samples);
    const float gone = intent.climb ? length(sampled.back() - sampled.front())
                                    : distanceAcross(sampled.front(), sampled.back());
    const float speed = rules.stuckSpeed.value() * (intent.crouches ? kCrouchedStuckShare : 1.0F);
    return gone < speed * span;
}

void StuckWatch::wiggle(Intent& intent, Seconds elapsed) {
    // A way drawn for a wiggle's time, as Wiggle draws m_wiggleDirection.
    wiggleLeft -= elapsed;
    if (wiggleLeft <= Seconds{}) {
        way = static_cast<Way>(random.below(std::to_underlying(Way::Count)));
        wiggleLeft = drawn(rules.wiggle);
    }
    intent.direction = turnedTo(way, intent.ahead);
    intent.drive.reset();
    intent.steered.reset();
    intent.climb.reset(); // off what it climbs, the way a jump pushes it
    intent.speedShare = 1.0F;
    jumpIn -= elapsed;
    // No jump crouching: there is no room over it, and what a jump costs for a second a body's third of its
    // speed cannot make up: it crawled at 10 units a second, stuck all the more.
    if (jumpIn <= Seconds{} && !intent.crouches) {
        intent.jumps = true;
        jumpIn = drawn(rules.jumpInterval);
    }
}

auto StuckWatch::turnedTo(Way way, Vec3 ahead) noexcept -> Vec3 {
    switch (way) {
    case Way::Ahead:
        return ahead;
    case Way::Back:
        return -ahead;
    case Way::Left:
        return Vec3{.x = -ahead.y, .y = ahead.x};
    case Way::Right:
        return Vec3{.x = ahead.y, .y = -ahead.x};
    case Way::Count:
        break;
    }
    return ahead;
}

auto StuckWatch::drawn(const TimeRange& range) noexcept -> Seconds {
    return Seconds{random.between(range.shortest.count(), range.longest.count())};
}

} // namespace nx::agents
