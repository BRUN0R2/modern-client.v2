// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.collision;
import nx.render;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers the blood draws from.
constexpr std::string_view kBloodStream = "enhanced blood";
/// Up, which a spray of no way bursts toward.
constexpr nx::Vec3 kUp{.z = 1.0F};

/// `vector` made of length one; up for none.
[[nodiscard]] auto wayOf(nx::Vec3 vector) noexcept -> nx::Vec3 {
    const float length = nx::length(vector);
    return length > 0.0F ? vector * (1.0F / length) : kUp;
}

/// How far `now` is from `start` to `end`, from 0 to 1.
[[nodiscard]] auto shareOf(nx::Seconds now, nx::Seconds start, nx::Seconds end) noexcept -> float {
    const float span = (end - start).count();
    return span > 0.0F ? std::clamp((now - start).count() / span, 0.0F, 1.0F) : 1.0F;
}

} // namespace

BloodEffects::BloodEffects(std::uint64_t seed) : random{seed, kBloodStream} {}

void BloodEffects::spray(const BloodSpray& spray, const BloodEnhancedLook& look, BloodShade shade,
                         nx::Seconds now) {
    flying.reserve(look.mostDroplets);
    drifting.reserve(look.mostMists);
    shade.light = std::max(shade.light, look.leastLight);
    const BloodBurst& burst = spray.hitGroup == HitGroup::Head ? look.head : look.body;
    const nx::Vec3 way = wayOf(spray.direction);
    const auto count =
        static_cast<std::uint32_t>(std::lround(burst.dropletsPerSize * static_cast<float>(spray.size)));
    throwDroplets(spray.position, way, burst, count, burst.backShare, 1.0F, shade, look, now);
    throwMists(spray.position, way, burst, burst.mists, burst.backShare, 1.0F, shade, look, now);
    if (burst.gushPulses > 0) {
        gushing.push_back(BloodGush{.position = spray.position,
                                    .way = way,
                                    .shade = shade,
                                    .burst = burst,
                                    .pulsesLeft = burst.gushPulses,
                                    .nextPulseAt = now + burst.gushInterval,
                                    .speedShare = burst.gushSlowing});
    }
}

auto BloodEffects::thrownWayOf(nx::Vec3 way, float spread, float backShare) -> nx::Vec3 {
    const bool isBack = random.nextUnit() < static_cast<double>(backShare);
    const nx::Vec3 turn{.x = random.between(-spread, spread),
                        .y = random.between(-spread, spread),
                        .z = random.between(-spread, spread)};
    return (isBack ? way * -1.0F : way) + turn;
}

void BloodEffects::throwDroplets(nx::Vec3 from, nx::Vec3 way, const BloodBurst& burst, std::uint32_t count,
                                 float backShare, float speedShare, BloodShade shade,
                                 const BloodEnhancedLook& look, nx::Seconds now) {
    for (std::uint32_t droplet = 0; droplet < count && flying.size() < look.mostDroplets; ++droplet) {
        // Its draws in their order: its way, its speed, its rise and its radius.
        const nx::Vec3 thrown = thrownWayOf(way, burst.spread, backShare);
        const float speed = random.between(burst.leastSpeed.value(), burst.mostSpeed.value()) * speedShare;
        const float rise = random.between(0.0F, burst.rise.value());
        const float radius = random.between(burst.leastRadius.value(), burst.mostRadius.value());
        flying.push_back(BloodDroplet{.position = from,
                                      .velocity = (thrown * speed) + (kUp * rise),
                                      .radius = nx::HammerUnits{radius},
                                      .shade = shade,
                                      .goesAt = now + look.dropletLife});
    }
}

void BloodEffects::throwMists(nx::Vec3 from, nx::Vec3 way, const BloodBurst& burst, std::uint32_t count,
                              float backShare, float speedShare, BloodShade shade,
                              const BloodEnhancedLook& look, nx::Seconds now) {
    for (std::uint32_t mist = 0; mist < count && drifting.size() < look.mostMists; ++mist) {
        // Its draws in their order: its way, its speed, its radius and its smoke.
        const nx::Vec3 thrown = thrownWayOf(way, burst.spread, backShare);
        const float speed = random.between(0.0F, burst.mistSpeed.value()) * speedShare;
        const float radius = random.between(burst.leastMistRadius.value(), burst.mostMistRadius.value());
        drifting.push_back(BloodMist{.position = from,
                                     .velocity = thrown * speed,
                                     .radius = nx::HammerUnits{radius},
                                     .growth = burst.mistGrowth,
                                     .shade = shade,
                                     .opacity = burst.mistOpacity,
                                     .madeAt = now,
                                     .goesAt = now + burst.mistLife,
                                     .seed = static_cast<float>(random.nextUnit())});
    }
}

void BloodEffects::update(nx::Seconds now, nx::Seconds elapsed, const nx::collision::BrushHulls& hulls,
                          nx::UnitsPerSecondSquared gravity, const BloodEnhancedLook& look) {
    const auto isGone = [now](const auto& each) {
        return now >= each.goesAt;
    };
    std::erase_if(flying, isGone);
    std::erase_if(drifting, isGone);
    std::erase_if(lying, isGone);
    for (BloodGush& gush : gushing) {
        while (gush.pulsesLeft > 0 && now >= gush.nextPulseAt) {
            throwDroplets(gush.position, gush.way, gush.burst, gush.burst.gushDroplets, 0.0F, gush.speedShare,
                          gush.shade, look, gush.nextPulseAt);
            throwMists(gush.position, gush.way, gush.burst, gush.burst.gushMists, 0.0F, gush.speedShare,
                       gush.shade, look, gush.nextPulseAt);
            gush.speedShare *= gush.burst.gushSlowing;
            gush.nextPulseAt += gush.burst.gushInterval;
            --gush.pulsesLeft;
        }
    }
    std::erase_if(gushing, [](const BloodGush& gush) { return gush.pulsesLeft == 0; });
    const float seconds = elapsed.count();
    const float fall = gravity.value() * look.gravity.value() * seconds;
    // e^-drag a second: the share of its speed a drop keeps over the frame, whatever its length.
    const float kept = std::exp(-look.drag * seconds);
    for (BloodMist& mist : drifting) {
        mist.position = mist.position + (mist.velocity * seconds);
        mist.velocity = mist.velocity * kept;
    }
    for (BloodDroplet& droplet : flying) {
        const nx::Vec3 moved = droplet.position + (droplet.velocity * seconds);
        const nx::collision::Trace met = hulls.trace({.start = droplet.position, .end = moved});
        if (met.startsSolid || met.isAllSolid) {
            droplet.goesAt = now; // made in what is solid: nothing to land on
            continue;
        }
        if (met.fraction < 1.0F) {
            if (random.nextUnit() < static_cast<double>(look.splatShare)) {
                laySplat(droplet, met, look, now);
            }
            droplet.goesAt = now;
            continue;
        }
        droplet.position = moved;
        droplet.velocity.z -= fall;
        droplet.velocity = droplet.velocity * kept;
    }
    std::erase_if(flying, isGone);
}

void BloodEffects::laySplat(const BloodDroplet& droplet, const nx::collision::Trace& met,
                            const BloodEnhancedLook& look, nx::Seconds now) {
    lying.reserve(look.mostSplats);
    if (lying.size() >= look.mostSplats) {
        lying.erase(lying.begin());
    }
    const float growth = random.between(look.leastSplatGrowth.value(), look.mostSplatGrowth.value());
    lying.push_back(BloodSplat{.position = met.end,
                               .normal = met.plane.normal,
                               .radius = nx::HammerUnits{droplet.radius.value() * growth},
                               .shade = droplet.shade,
                               .fadesAt = now + look.splatLife - look.splatFade,
                               .goesAt = now + look.splatLife,
                               .seed = static_cast<float>(random.nextUnit())});
}

auto BloodEffects::shown(nx::Seconds now, const BloodEnhancedLook& look)
    -> std::span<const nx::render::BloodInstance> {
    instances.clear();
    instances.reserve(std::size_t{look.mostDroplets} + look.mostSplats + look.mostMists);
    for (const BloodSplat& splat : lying) {
        instances.push_back(
            nx::render::BloodInstance{.position = splat.position,
                                      .radius = splat.radius,
                                      .color = splat.shade.color,
                                      .light = splat.shade.light,
                                      .opacity = 1.0F - shareOf(now, splat.fadesAt, splat.goesAt),
                                      .shape = nx::render::BloodShape::Splat,
                                      .normal = splat.normal,
                                      .seed = splat.seed});
    }
    for (const BloodDroplet& droplet : flying) {
        instances.push_back(nx::render::BloodInstance{
            .position = droplet.position,
            .velocity = droplet.velocity,
            .radius = droplet.radius,
            .color = droplet.shade.color,
            .light = droplet.shade.light,
            .opacity = 1.0F - shareOf(now, droplet.goesAt - look.dropletFade, droplet.goesAt)});
    }
    for (const BloodMist& mist : drifting) {
        const float age = std::max(0.0F, (now - mist.madeAt).count());
        // Holding most of its opacity, then fading out: by the square of the share of its life gone.
        const float gone = shareOf(now, mist.madeAt, mist.goesAt);
        instances.push_back(nx::render::BloodInstance{
            .position = mist.position,
            .radius = nx::HammerUnits{mist.radius.value() + (mist.growth.value() * age)},
            .color = mist.shade.color,
            .light = mist.shade.light * look.mistLight,
            .opacity = mist.opacity * (1.0F - (gone * gone)),
            .shape = nx::render::BloodShape::Mist,
            .seed = mist.seed});
    }
    return instances;
}

} // namespace cs
