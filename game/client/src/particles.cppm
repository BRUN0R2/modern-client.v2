// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The client's particles (docs/05-rendering.md, section 10): dots of a palette entry's color the game's
/// effects make, moving on their own, as the original's engine makes them (R_AllocParticle,
/// CL_ThinkParticle, as Xash3D-FWGS reconstructs them; GPL, consulted: docs/20 section 3). Today the
/// stream of blood a hit makes a head shed (cs::BloodStream, TE_BLOODSTREAM), as R_BloodStream makes it.
/// Each moves by its velocity and falls at its share of the world's gravity, through what is solid, as
/// the original's do; none is made past the original's most (max_particles).
export module cs.client:particles;

import std;
import nx.core;
import nx.math;
import cs.shared;

export namespace cs {

/// The most particles a client shows at once, the original's (max_particles): none is made past them.
inline constexpr std::size_t kMostParticles = 4096;

/// A particle: where it is and how it moves (the share of the world's gravity it falls at), the palette
/// entry of its color, and when it goes, in the time of the map.
struct Particle {
    nx::Vec3 origin{};
    nx::Vec3 velocity{};
    nx::Scale gravity{};
    std::uint8_t color = 0;
    nx::Seconds goesAt{};
};

/// How opaque `particle` is at `now`, of 255: wholly until its last sixteenth of a second, then fading out
/// as it goes (CL_DrawParticles).
[[nodiscard]] auto amountOf(const Particle& particle, nx::Seconds now) noexcept -> std::uint8_t;

/// The particles of a map's client.
class Particles {
public:
    /// None yet, their chances drawn from a stream of `seed`.
    explicit Particles(std::uint64_t seed);

    /// Shows `stream` from `now` as `look` makes it (R_BloodStream): its stream and its splash of
    /// particles from where it met the head, along its way made of length one (none for a stream of no
    /// way, whose particles but dip). Those past kMostParticles are not made.
    void streamBlood(const BloodStream& stream, const BloodStreamLook& look, nx::Seconds now);

    /// Moves them over `elapsed` up to `now`, the world's gravity `gravity`: each moves by its velocity,
    /// then falls its share of the gravity (CL_ThinkParticle). Those gone at `now` are forgotten first.
    void update(nx::Seconds now, nx::Seconds elapsed, nx::UnitsPerSecondSquared gravity);

    /// Those shown, in the order they came.
    [[nodiscard]] inline auto shown() const noexcept -> std::span<const Particle> { return particles; }

private:
    /// Shows `particle`, unless kMostParticles are: whether it is shown.
    [[nodiscard]] auto add(const Particle& particle) -> bool;

    std::vector<Particle> particles;
    nx::Random random;
};

} // namespace cs
