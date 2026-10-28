// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers the particles draw from.
constexpr std::string_view kParticleStream = "particles";
/// How fast a particle fades out as it goes, of its whole opacity a second: over its last sixteenth of
/// one (CL_DrawParticles).
constexpr float kFadePerSecond = 16.0F;
constexpr float kOpaque = 255.0F;

} // namespace

auto amountOf(const Particle& particle, nx::Seconds now) noexcept -> std::uint8_t {
    const float left = (particle.goesAt - now).count();
    // int(255 * left * 16), held to a byte.
    return static_cast<std::uint8_t>(std::clamp(kOpaque * left * kFadePerSecond, 0.0F, kOpaque));
}

Particles::Particles(std::uint64_t seed) : random{seed, kParticleStream} {}

auto Particles::add(const Particle& particle) -> bool {
    if (particles.size() >= kMostParticles) {
        return false;
    }
    particles.push_back(particle);
    return true;
}

void Particles::streamBlood(const BloodStream& stream, const BloodStreamLook& look, nx::Seconds now) {
    // VectorNormalize2: a stream of no way has none.
    const nx::Vec3 way =
        nx::lengthSquared(stream.direction) > 0.0F ? nx::normalized(stream.direction) : nx::Vec3{};
    const auto colorOf = [&] {
        return nx::narrow<std::uint8_t>(std::uint64_t{stream.color} + random.below(look.colorRange));
    };
    const auto speed = static_cast<float>(stream.speed);
    // Its stream: each particle dipped less than the one before, and a step slower, none slower than
    // still. The dips add up as the original's floats do.
    float dip = look.firstDip;
    for (std::uint32_t index = 0; index < look.streamParticles; ++index) {
        const float particleSpeed =
            std::max(speed - (look.speedStep.value() * static_cast<float>(index)), 0.0F);
        if (!add(Particle{.origin = stream.position,
                          .velocity = (way - nx::Vec3{.z = dip}) * particleSpeed,
                          .gravity = look.streamGravity,
                          .color = colorOf(),
                          .goesAt = now + look.streamLife})) {
            return;
        }
        dip -= look.dipStep;
    }
    // Its splash: each particle at a share of the speed, cut to a whole number as the original's is, times
    // that share spread; its companions as fast, dipped a step less, each drawn a little off it.
    dip = look.firstSplashDip;
    const float jitter = look.companionJitter.value();
    for (std::uint32_t index = 0; index < stream.speed / look.splashPerSpeed; ++index) {
        const std::uint8_t color = colorOf();
        const float share = random.between(0.0F, 1.0F);
        const auto splashSpeed = static_cast<float>(nx::truncateAsX86(speed * share));
        const float scale = share * look.splashSpread * splashSpeed;
        if (!add(Particle{.origin = stream.position,
                          .velocity = (way - nx::Vec3{.z = dip}) * scale,
                          .gravity = look.splashGravity,
                          .color = color,
                          .goesAt = now + look.splashLife})) {
            return;
        }
        dip -= look.dipStep;
        for (std::uint32_t companion = 0; companion < look.splashCompanions; ++companion) {
            const std::uint8_t companionColor = colorOf();
            const float off = random.between(-jitter, jitter);
            if (!add(Particle{.origin = stream.position + nx::Vec3{.x = off, .y = off, .z = off},
                              .velocity = (way - nx::Vec3{.z = dip}) * scale,
                              .gravity = look.splashGravity,
                              .color = companionColor,
                              .goesAt = now + look.splashLife})) {
                return;
            }
        }
    }
}

void Particles::update(nx::Seconds now, nx::Seconds elapsed, nx::UnitsPerSecondSquared gravity) {
    std::erase_if(particles, [now](const Particle& particle) { return now >= particle.goesAt; });
    const float seconds = elapsed.count();
    for (Particle& particle : particles) {
        particle.origin = particle.origin + (particle.velocity * seconds);
        particle.velocity.z -= gravity.value() * particle.gravity.value() * seconds;
    }
}

} // namespace cs
