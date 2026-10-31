// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The enhanced look's blood on the client (docs/10-counter-strike-gameplay.md, section 7.4), the
/// project's own: what a hit's spray (cs::BloodSpray) makes, as data/cstrike/blood.toml's enhancedLook
/// tells, which nx.render:blood draws. A burst where the hit met the body, of its part of the body: a
/// cloud of blood smoke and droplets flying off the way the hit went, some back the way it came, which the
/// air soon stops, so that the spray hangs a moment; a hit in the head bursts more, all at once, as the
/// hit. A droplet fades out in the air unless the world stops it first, where some
/// leave a splat flat on what they met, which lies a while and fades out. Their room is bounded: droplets and
/// mists past their most are not made, and a new splat takes the oldest's place. Each is of its spray's
/// color, lit by the light where the hit met the body, once, as it is made, at least as much as the look's
/// least light.
export module cs.client:bloodEffects;

import std;
import nx.core;
import nx.math;
import nx.collision;
import nx.render;
import cs.shared;

export namespace cs {

/// What a spray's blood is drawn in: its color, its palette entry's, and the light where it met the body,
/// 1 its color as it is (Scene::bloodLightAt).
struct BloodShade {
    nx::render::RenderColor color{};
    float light = 1.0F;
};

/// A droplet: where it is and how it flies, its radius, its shade, and when it goes unless it lands, in the
/// time of the map.
struct BloodDroplet {
    nx::Vec3 position{};
    nx::Vec3 velocity{};
    nx::HammerUnits radius{};
    BloodShade shade{};
    nx::Seconds goesAt{};
};

/// A mist: where it is and how it flies, the radius it grows to and the share of it it was made at, its
/// shade, how opaque it was made, when it was made and goes, and the seed of its smoke.
struct BloodMist {
    nx::Vec3 position{};
    nx::Vec3 velocity{};
    nx::HammerUnits radius{};
    float startScale = 1.0F;
    BloodShade shade{};
    float opacity = 0.0F;
    nx::Seconds madeAt{};
    nx::Seconds goesAt{};
    float seed = 0.0F;
};

/// A splat: where it lies and the way what it lies on faces, its radius, its shade, when it starts fading
/// out and goes, and the seed of its lobes.
struct BloodSplat {
    nx::Vec3 position{};
    nx::Vec3 normal{};
    nx::HammerUnits radius{};
    BloodShade shade{};
    nx::Seconds fadesAt{};
    nx::Seconds goesAt{};
    float seed = 0.0F;
};

/// The enhanced look's blood of a map's client.
class BloodEffects {
public:
    /// None yet, their chances drawn from a stream of `seed`.
    explicit BloodEffects(std::uint64_t seed);

    /// Shows `spray` from `now` in `shade`, lit look.leastLight at least, as `look` makes it: the burst of
    /// its part of the body, the
    /// head's if it met the head, else the body's, from where it met it the way the hit went, as many
    /// droplets as its size times the burst's dropletsPerSize, rounded, and its mists splashing out of it,
    /// each flying off as a droplet does but turned by up to the burst's mistSpread. Droplets past
    /// look.mostDroplets, and mists past
    /// look.mostMists, are not made.
    void spray(const BloodSpray& spray, const BloodEnhancedLook& look, BloodShade shade, nx::Seconds now);
    /// Moves them over `elapsed` up to `now`, the world's gravity `gravity`, as `look` moves them: each
    /// droplet moves by its velocity unless the world of `hulls` stops it where a line from where it
    /// was meets it, there going, and leaving a splat flat on what it met, for look.splatShare of them;
    /// else it falls at look.gravity of `gravity`; each mist moves by its velocity, through what is solid;
    /// and the air slows both, their speed falling to e^-look.drag of itself a second, the same over a
    /// frame of any length. Those gone at `now` are forgotten first.
    void update(nx::Seconds now, nx::Seconds elapsed, const nx::collision::BrushHulls& hulls,
                nx::UnitsPerSecondSquared gravity, const BloodEnhancedLook& look);
    /// What to draw at `now`: the splats, fully opaque until look.splatFade before they go, then fading
    /// out; the droplets, along their flight, fading out over their last look.dropletFade; then the mists,
    /// look.mistLight times as lit, grown from their start scale to their whole radius by the square of
    /// the share of their life gone, faster as they go, wholly opaque until look.mistFadeFrom of their
    /// life, then fading out, easing in and out. Kept until the next call.
    [[nodiscard]] auto shown(nx::Seconds now, const BloodEnhancedLook& look)
        -> std::span<const nx::render::BloodInstance>;

    /// The droplets flying, in the order they came.
    [[nodiscard]] inline auto droplets() const noexcept -> std::span<const BloodDroplet> { return flying; }
    /// The mists, in the order they came.
    [[nodiscard]] inline auto mists() const noexcept -> std::span<const BloodMist> { return drifting; }
    /// The splats lying, the oldest first.
    [[nodiscard]] inline auto splats() const noexcept -> std::span<const BloodSplat> { return lying; }

private:
    /// A way a burst throws a drop off `way`: back against it for `backShare` of them, turned by up to
    /// `spread` on each axis, drawn in that order.
    [[nodiscard]] auto thrownWayOf(nx::Vec3 way, float spread, float backShare) -> nx::Vec3;
    /// Throws `count` droplets of `burst` in `shade` from `from`, the burst's backShare of them back against
    /// `way`, gone after look.dropletLife from `now`; none past look.mostDroplets.
    void throwDroplets(nx::Vec3 from, nx::Vec3 way, const BloodBurst& burst, std::uint32_t count,
                       BloodShade shade, const BloodEnhancedLook& look, nx::Seconds now);
    /// Throws the burst's mists in `shade` from `from`, its backShare of them back against `way`, made at
    /// `now`; none past look.mostMists.
    void throwMists(nx::Vec3 from, nx::Vec3 way, const BloodBurst& burst, BloodShade shade,
                    const BloodEnhancedLook& look, nx::Seconds now);
    /// Lays a splat of `droplet` where `met` stopped it, flat on what it met, its radius grown as `look`
    /// grows it, lying from `now`; the oldest going first when there is no room.
    void laySplat(const BloodDroplet& droplet, const nx::collision::Trace& met, const BloodEnhancedLook& look,
                  nx::Seconds now);

    std::vector<BloodDroplet> flying;
    std::vector<BloodMist> drifting;
    std::vector<BloodSplat> lying;
    std::vector<nx::render::BloodInstance> instances;
    nx::Random random;
};

} // namespace cs
