// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The enhanced look's blood on the client (docs/10-counter-strike-gameplay.md, section 7.4), the
/// project's own: what a hit's spray (cs::BloodSpray) makes, as data/cstrike/blood.toml's enhancedLook
/// tells, which nx.render:blood draws. A burst where the hit met the body, of its part of the body: droplets
/// flying off the way the hit went, some back the way it came, and a short mist; a hit in the head
/// bursts more, then gushes for a moment, in pulses, as a wound does. A droplet falls and slows until the
/// world stops it, where some leave a splat flat on what they met, which lies a while and fades out.
/// Their room is bounded: droplets past their most are not made, and a new splat takes the oldest's place.
/// Each is of its spray's color, lit by the light where the hit met the body, once, as it is made.
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

/// A mist: where it is, its radius as it was made and how fast it grows, its shade, how opaque it was
/// made, and when it was made and goes.
struct BloodMist {
    nx::Vec3 position{};
    nx::HammerUnits radius{};
    nx::UnitsPerSecond growth{};
    BloodShade shade{};
    float opacity = 0.0F;
    nx::Seconds madeAt{};
    nx::Seconds goesAt{};
};

/// A splat: where it lies and the way what it lies on faces, its radius, its shade, and when it starts
/// fading out and goes.
struct BloodSplat {
    nx::Vec3 position{};
    nx::Vec3 normal{};
    nx::HammerUnits radius{};
    BloodShade shade{};
    nx::Seconds fadesAt{};
    nx::Seconds goesAt{};
};

/// A wound gushing: where, the way it gushes, its shade, the burst it gushes as, how many pulses it has
/// left and when the next comes, and how fast the next is, of the burst's speeds.
struct BloodGush {
    nx::Vec3 position{};
    nx::Vec3 way{};
    BloodShade shade{};
    BloodBurst burst{};
    std::uint32_t pulsesLeft = 0;
    nx::Seconds nextPulseAt{};
    float speedShare = 1.0F;
};

/// The enhanced look's blood of a map's client.
class BloodEffects {
public:
    /// None yet, their chances drawn from a stream of `seed`.
    explicit BloodEffects(std::uint64_t seed);

    /// Shows `spray` from `now` in `shade` as `look` makes it: the burst of its part of the body, the
    /// head's if it met the head, else the body's, from where it met it the way the hit went, as many
    /// droplets as its size times the burst's dropletsPerSize, rounded; and the burst's gush after it.
    /// Droplets past look.mostDroplets are not made.
    void spray(const BloodSpray& spray, const BloodEnhancedLook& look, BloodShade shade, nx::Seconds now);
    /// Moves them over `elapsed` up to `now`, the world's gravity `gravity`, as `look` moves them: each gush
    /// pulses as many times as came due, its droplets the way it gushes, none back, each pulse slower;
    /// each droplet moves by its velocity unless the world of `hulls` stops it where a line from where it
    /// was meets it, there going, and leaving a splat flat on what it met, for look.splatShare of them;
    /// else it falls at look.gravity of `gravity` and loses look.drag of its speed a second. Those gone at
    /// `now`, and gushes done, are forgotten first.
    void update(nx::Seconds now, nx::Seconds elapsed, const nx::collision::BrushHulls& hulls,
                nx::UnitsPerSecondSquared gravity, const BloodEnhancedLook& look);
    /// What to draw at `now`: the splats, fully opaque until look.splatFade before they go, then fading
    /// out; the droplets, along their flight; then the mists, grown since they were made and fading out
    /// as they go. Kept until the next call.
    [[nodiscard]] auto shown(nx::Seconds now, const BloodEnhancedLook& look)
        -> std::span<const nx::render::BloodInstance>;

    /// The droplets flying, in the order they came.
    [[nodiscard]] inline auto droplets() const noexcept -> std::span<const BloodDroplet> { return flying; }
    /// The mists, in the order they came.
    [[nodiscard]] inline auto mists() const noexcept -> std::span<const BloodMist> { return drifting; }
    /// The splats lying, the oldest first.
    [[nodiscard]] inline auto splats() const noexcept -> std::span<const BloodSplat> { return lying; }
    /// The wounds gushing.
    [[nodiscard]] inline auto gushes() const noexcept -> std::span<const BloodGush> { return gushing; }

private:
    /// Throws `count` droplets of `burst` in `shade` from `from`, `backShare` of them back against `way`,
    /// as fast as `speedShare` of the burst's speeds, gone after look.dropletLife from `now`; none past
    /// look.mostDroplets.
    void throwDroplets(nx::Vec3 from, nx::Vec3 way, const BloodBurst& burst, std::uint32_t count,
                       float backShare, float speedShare, BloodShade shade, const BloodEnhancedLook& look,
                       nx::Seconds now);
    /// Lays a splat of `droplet` where `met` stopped it, flat on what it met, its radius grown as `look`
    /// grows it, lying from `now`; the oldest going first when there is no room.
    void laySplat(const BloodDroplet& droplet, const nx::collision::Trace& met, const BloodEnhancedLook& look,
                  nx::Seconds now);

    std::vector<BloodDroplet> flying;
    std::vector<BloodMist> drifting;
    std::vector<BloodSplat> lying;
    std::vector<BloodGush> gushing;
    std::vector<nx::render::BloodInstance> instances;
    nx::Random random;
};

} // namespace cs
