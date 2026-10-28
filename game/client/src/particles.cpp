// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.render;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers the particles draw from.
constexpr std::string_view kParticleStream = "particles";
/// How fast a particle fades out as it goes, of its whole opacity a second: over its last sixteenth of
/// one (CL_DrawParticles); a streak, over its last half second (CL_DrawTracers).
constexpr float kFadePerSecond = 16.0F;
constexpr float kStreakFadePerSecond = 2.0F;
constexpr float kOpaque = 255.0F;

/// A streak of a splash goes after kLeastStreakLife to kMostStreakLife (R_StreakSplash); it is drawn as
/// long as it moves in kStreakLength times the time it has left, kLongestStreakTime of it at most
/// (R_StreakSplash's ramp, CL_DrawTracers' attenuation).
constexpr nx::Seconds kLeastStreakLife{0.1F};
constexpr nx::Seconds kMostStreakLife{0.5F};
constexpr float kStreakLength = 1.0F;
constexpr nx::Seconds kLongestStreakTime{0.1F};
/// How much of its speed across the ground a streak loses a second (CL_DrawTracers).
constexpr float kStreakSlowing = 0.9F;

/// The streak colors (gTracerColors), by their index.
constexpr std::array<nx::render::RenderColor, 12> kStreakColors{{
    {.red = 255, .green = 255, .blue = 255}, // white
    {.red = 255, .green = 0, .blue = 0},     // red
    {.red = 0, .green = 255, .blue = 0},     // green
    {.red = 0, .green = 0, .blue = 255},     // blue
    {.red = 102, .green = 102, .blue = 51},  // the tracer cvars' defaults
    {.red = 255, .green = 167, .blue = 17},  // sparks, yellow and orange
    {.red = 255, .green = 130, .blue = 90},  // the Gargantua's yellowish streaks
    {.red = 55, .green = 60, .blue = 144},   // the Egon's blue streak
    {.red = 255, .green = 130, .blue = 90},  // more of the Gargantua's
    {.red = 255, .green = 140, .blue = 90},
    {.red = 200, .green = 130, .blue = 90}, // its redder
    {.red = 255, .green = 120, .blue = 70}, // its darker red
}};
/// The index of the tracer cvars' color: the one of an index past the others (TRACER_COLORINDEX_DEFAULT).
constexpr std::size_t kTracerCvarsColor = 4;

/// How opaque something that fades out over its last 1 / `fadePerSecond` of a second is, `left` before
/// it goes: int(255 * left * fadePerSecond), held to a byte.
[[nodiscard]] auto fadedAmount(nx::Seconds left, float fadePerSecond) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(kOpaque * left.count() * fadePerSecond, 0.0F, kOpaque));
}

} // namespace

auto amountOf(const Particle& particle, nx::Seconds now) noexcept -> std::uint8_t {
    return fadedAmount(particle.goesAt - now, kFadePerSecond);
}

auto amountOf(const Streak& streak, nx::Seconds now) noexcept -> std::uint8_t {
    return fadedAmount(streak.goesAt - now, kStreakFadePerSecond);
}

auto endOf(const Streak& streak, nx::Seconds now) noexcept -> nx::Vec3 {
    const nx::Seconds drawn = std::min(streak.goesAt - now, kLongestStreakTime);
    return streak.origin + (streak.velocity * (kStreakLength * drawn.count()));
}

auto streakColorOf(std::uint8_t index) noexcept -> nx::render::RenderColor {
    return kStreakColors[index < kStreakColors.size() ? index : kTracerCvarsColor];
}

Particles::Particles(std::uint64_t seed) : random{seed, kParticleStream} {}

auto Particles::hasRoom() const noexcept -> bool {
    return particles.size() + streaks.size() < kMostParticles;
}

auto Particles::add(const Particle& particle) -> bool {
    if (!hasRoom()) {
        return false;
    }
    particles.push_back(particle);
    return true;
}

void Particles::splashStreaks(const StreakSplash& splash, nx::Seconds now) {
    const nx::Vec3 velocity = splash.direction * splash.speed.value();
    const nx::render::RenderColor color = streakColorOf(splash.color);
    const float spread = splash.spread.value();
    for (std::uint32_t streak = 0; streak < splash.count && hasRoom(); ++streak) {
        // VectorAddScalar: one draw, as much on each axis.
        const float off = random.between(-spread, spread);
        const float life = random.between(kLeastStreakLife.count(), kMostStreakLife.count());
        streaks.push_back(Streak{.origin = splash.position,
                                 .velocity = velocity + nx::Vec3{.x = off, .y = off, .z = off},
                                 .color = color,
                                 .goesAt = now + nx::Seconds{life}});
    }
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
    std::erase_if(streaks, [now](const Streak& streak) { return now >= streak.goesAt; });
    const float seconds = elapsed.count();
    for (Particle& particle : particles) {
        particle.origin = particle.origin + (particle.velocity * seconds);
        particle.velocity.z -= gravity.value() * particle.gravity.value() * seconds;
    }
    const float kept = std::max(1.0F - (seconds * kStreakSlowing), 0.0F);
    for (Streak& streak : streaks) {
        streak.origin = streak.origin + (streak.velocity * seconds);
        streak.velocity.x *= kept;
        streak.velocity.y *= kept;
        streak.velocity.z -= gravity.value() * seconds;
    }
}

} // namespace cs
