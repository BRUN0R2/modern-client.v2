// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.agents;
import std;
import nx.core;
import nx.math;

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
} // namespace

auto isValid(const StuckRules& rules) noexcept -> bool {
    return isPositive(rules.sampleInterval.count()) && rules.samples > 0 &&
           isPositive(rules.stuckSpeed.value()) && isPositive(rules.unstuckDistance.value()) &&
           isValid(rules.wiggle) && std::isfinite(rules.firstJump.count()) && rules.firstJump >= Seconds{} &&
           isValid(rules.jumpInterval) && isPositive(rules.giveUpTime.count());
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
        if (!isTooSlow()) {
            return Progress::Moving;
        }
        // Stuck: where, and its first jump soon (StuckCheck's m_stuckJumpTimestamp).
        stuckAt = feet;
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

auto StuckWatch::isTooSlow() const noexcept -> bool {
    if (sampled.size() <= rules.samples) {
        return false;
    }
    const float span = rules.sampleInterval.count() * static_cast<float>(rules.samples);
    return distanceAcross(sampled.front(), sampled.back()) < rules.stuckSpeed.value() * span;
}

void StuckWatch::wiggle(Intent& intent, Seconds elapsed) {
    // A way drawn for a wiggle's time, as Wiggle draws m_wiggleDirection.
    wiggleLeft -= elapsed;
    if (wiggleLeft <= Seconds{}) {
        way = static_cast<Way>(random.below(std::to_underlying(Way::Count)));
        wiggleLeft = drawn(rules.wiggle);
    }
    intent.direction = turnedTo(way, intent.ahead);
    intent.speedShare = 1.0F;
    jumpIn -= elapsed;
    if (jumpIn <= Seconds{}) {
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
