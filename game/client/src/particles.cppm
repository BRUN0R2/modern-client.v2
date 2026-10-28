// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The client's particles (docs/05-rendering.md, section 10): dots of a palette entry's color, and
/// streaks of light, the game's effects make, moving on their own, as the original's engine makes them
/// (R_AllocParticle, CL_ThinkParticle, R_AllocTracer, CL_DrawTracers, as Xash3D-FWGS reconstructs them;
/// GPL, consulted: docs/20 section 3). Today the stream of blood a hit makes a head shed
/// (cs::BloodStream, TE_BLOODSTREAM), as R_BloodStream makes it, and the sparks a hit strikes off a
/// helmet (cs::StreakSplash, TE_STREAK_SPLASH), as R_StreakSplash makes them. Each moves by its
/// velocity, through what is solid, as the original's do: a dot falls at its share of the world's
/// gravity, a streak at all of it, slowing across the ground. Dots and streaks share the original's most
/// (max_particles), none made past it.
export module cs.client:particles;

import std;
import nx.core;
import nx.math;
import nx.render;
import cs.shared;

export namespace cs {

/// The most particles and streaks a client shows at once, the original's (max_particles): none is made
/// past them.
inline constexpr std::size_t kMostParticles = 4096;

/// The sprite streaks are drawn with: the engine's dot (REF_DOT_SPRITE).
inline constexpr std::string_view kStreakSprite = "sprites/dot.spr";
/// Half the width a streak is drawn (gTracerSize's, of a streak that falls).
inline constexpr nx::HammerUnits kStreakHalfWidth{0.5F};

/// A particle: where it is and how it moves (the share of the world's gravity it falls at), the palette
/// entry of its color, and when it goes, in the time of the map.
struct Particle {
    nx::Vec3 origin{};
    nx::Vec3 velocity{};
    nx::Scale gravity{};
    std::uint8_t color = 0;
    nx::Seconds goesAt{};
};

/// A streak: where it starts and how it moves, its color, and when it goes, in the time of the map.
struct Streak {
    nx::Vec3 origin{};
    nx::Vec3 velocity{};
    nx::render::RenderColor color{};
    nx::Seconds goesAt{};
};

/// How opaque `particle` is at `now`, of 255: wholly until its last sixteenth of a second, then fading out
/// as it goes (CL_DrawParticles).
[[nodiscard]] auto amountOf(const Particle& particle, nx::Seconds now) noexcept -> std::uint8_t;
/// How opaque `streak` is at `now`, of 255: wholly until its last half second, then fading out as it goes
/// (CL_DrawTracers).
[[nodiscard]] auto amountOf(const Streak& streak, nx::Seconds now) noexcept -> std::uint8_t;
/// Where `streak` ends at `now`: along its velocity, as far as it moves in the time it has left, a tenth
/// of a second of it at most (CL_DrawTracers).
[[nodiscard]] auto endOf(const Streak& streak, nx::Seconds now) noexcept -> nx::Vec3;

/// The color of streak color `index` (gTracerColors): white, red, green, blue, the tracer cvars' (their
/// defaults: tracerred 0.8, tracergreen 0.8 and tracerblue 0.4, times traceralpha 0.5), sparks' yellow
/// and orange, then the Gargantua's and the Egon's; the tracer cvars' for an index past them.
[[nodiscard]] auto streakColorOf(std::uint8_t index) noexcept -> nx::render::RenderColor;

/// The particles and streaks of a map's client.
class Particles {
public:
    /// None yet, their chances drawn from a stream of `seed`.
    explicit Particles(std::uint64_t seed);

    /// Shows `stream` from `now` as `look` makes it (R_BloodStream): its stream and its splash of
    /// particles from where it met the head, along its way made of length one (none for a stream of no
    /// way, whose particles but dip). Those past kMostParticles are not made.
    void streamBlood(const BloodStream& stream, const BloodStreamLook& look, nx::Seconds now);
    /// Shows `splash` from `now` as R_StreakSplash makes it: its streaks from its point, each along its
    /// way times its speed, drawn up to its spread faster or slower, as much on each axis, of its color,
    /// gone after a tenth to half a second. Those past kMostParticles are not made.
    void splashStreaks(const StreakSplash& splash, nx::Seconds now);

    /// Moves them over `elapsed` up to `now`, the world's gravity `gravity`: each moves by its velocity,
    /// then a particle falls its share of the gravity (CL_ThinkParticle), a streak all of it, slowing
    /// across the ground by 0.9 of its speed a second (CL_DrawTracers). Those gone at `now` are forgotten
    /// first.
    void update(nx::Seconds now, nx::Seconds elapsed, nx::UnitsPerSecondSquared gravity);

    /// The particles shown, in the order they came.
    [[nodiscard]] inline auto shown() const noexcept -> std::span<const Particle> { return particles; }
    /// The streaks shown, in the order they came.
    [[nodiscard]] inline auto shownStreaks() const noexcept -> std::span<const Streak> { return streaks; }

private:
    /// Whether another particle or streak is made: fewer than kMostParticles are shown.
    [[nodiscard]] auto hasRoom() const noexcept -> bool;
    /// Shows `particle` where there is room: whether it is shown.
    [[nodiscard]] auto add(const Particle& particle) -> bool;

    std::vector<Particle> particles;
    std::vector<Streak> streaks;
    nx::Random random;
};

} // namespace cs
