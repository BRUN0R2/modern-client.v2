// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Fog (docs/05-rendering.md, section 10): how much of what is drawn a fog hides, by how far it is
/// from the eye, as the original's OpenGL fog hides it, and a fog as it comes and goes.
///
/// A fog has the original's three curves (GL_LINEAR, GL_EXP, GL_EXP2), a color, a density, the
/// nearest distance it hides anything at and the farthest it thickens to, and the most of a color it
/// hides. Every renderer of the scene mixes the color of what it draws toward the fog's, by how much
/// the fog hides there (shaders/fog.slang); what is added to the scene (kRenderGlow, kRenderTransAdd,
/// streaks) it leaves as it is, as the original turns its fog off for them (GL_SetRenderMode). With its
/// nearest distance at 0, its farthest at infinity and all of a color hidden, a fog is the original's.
///
/// The legacy look measures how far a point is along the view, as the original's GL fog does on the
/// common cards; the enhanced look measures its distance from the eye, so that the fog stays where it
/// is as the view turns.
export module nx.render:fog;

import std;
import nx.core;
import nx.math;
import :camera;
import :gamma;

export namespace nx::render {

/// How a fog thickens with the distance past its start, d: the original's OpenGL fog modes.
enum class FogCurve : std::uint8_t {
    Linear,             ///< GL_LINEAR: from none at its start to all of it at its end, d over the two's.
    Exponential,        ///< GL_EXP: 1 - e^-(density d), the original's under water.
    ExponentialSquared, ///< GL_EXP2: 1 - e^-(density d)^2, a map's env_fog.
    Count,
};

/// A fog's color, as the original's screen shows it: each channel from 0 to 1.
struct FogColor {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;

    [[nodiscard]] inline auto operator==(const FogColor&) const noexcept -> bool = default;
};

/// The farthest a fog thickens to when it thickens all the way.
inline constexpr HammerUnits kEndlessFog{std::numeric_limits<float>::infinity()};

/// A fog: its color, how it thickens and how fast (its density, per unit, for the exponential curves),
/// the nearest distance it hides anything at (start: nearer, nothing), the farthest it thickens to (end:
/// farther, it hides as much as there), and the most of a color it hides, from 0 to 1.
struct Fog {
    FogColor color{};
    FogCurve curve = FogCurve::ExponentialSquared;
    float density = 0.0F;
    HammerUnits start{};
    HammerUnits end = kEndlessFog;
    float opacity = 1.0F;

    [[nodiscard]] inline auto operator==(const Fog&) const noexcept -> bool = default;
};

/// Whether a renderer takes `fog`: its color's channels and its opacity from 0 to 1, its curve one of
/// FogCurve, its density finite and not negative, its start finite and not negative and its end past
/// it (infinite only for an exponential curve, a linear one ending somewhere).
[[nodiscard]] auto isValid(const Fog& fog) noexcept -> bool;

/// How much of a color `distance` from the eye `fog` hides, from 0 to its opacity: its curve at the
/// distance past its start, held from its end on. As the shaders find it (shaders/fog.slang).
[[nodiscard]] auto fogAmountAt(const Fog& fog, HammerUnits distance) noexcept -> float;

/// How thick a fog is where it reaches (fogReachOf), in shares of as thick as it gets.
inline constexpr float kFogReachThickness = 0.99F;

/// How far from the eye `fog` reaches: where it is kFogReachThickness of as thick as it gets, or its
/// end, the nearer; its start when it never thickens (a density of 0).
[[nodiscard]] auto fogReachOf(const Fog& fog) noexcept -> HammerUnits;

/// A fog as the shaders read it (shaders/fog.slang's Fog), in a renderer's frame: rgb its color as the
/// target takes it (a display color, or linear light), a its opacity, 0 for no fog; its curve, density,
/// start and end; the eye it is seen from, w 1 when it is measured from the eye rather than along the
/// view; and the view's forward.
struct FogBlock {
    Vec4 color{};
    Vec4 shape{};
    Vec4 eye{};
    Vec4 forward{};
};

/// The block of `fog`, as `camera` sees it drawn to a target of `encoding`; none, a block that hides
/// nothing. Requires a valid fog.
[[nodiscard]] auto fogBlockOf(const std::optional<Fog>& fog, const Camera& camera,
                              ColorEncoding encoding) noexcept -> FogBlock;

/// A fog shown as it comes and goes, so that it never appears or vanishes at once: turned on, it comes
/// in from as far as it reaches to its start, from hiding nothing to its opacity; turned off, it goes
/// back out as far as it reaches and is gone; changed while it shows, its color, density, opacity,
/// start and end go to the other's (its curve and an endless end at once). Each over the time it is
/// given, slowly first and last.
class FogTransition {
public:
    /// Shows `fog` (valid), from what shows now, in `duration` (none or more): none turns it off.
    void show(std::optional<Fog> fog, Seconds duration) noexcept;
    /// Moves it on by `elapsed` (none or more).
    void advance(Seconds elapsed) noexcept;
    /// The fog shown now; none when none shows.
    [[nodiscard]] auto shown() const noexcept -> std::optional<Fog>;
    /// The fog it goes to; none when it is turned off.
    [[nodiscard]] inline auto target() const noexcept -> std::optional<Fog> { return m_target; }

private:
    std::optional<Fog> m_target{};
    std::optional<Fog> m_from{}; ///< As it showed when it was given its target; none before any.
    std::optional<Fog> m_to{};   ///< Where it goes: its target, or the off form of what showed.
    Seconds m_duration{};
    Seconds m_elapsed{};
};

} // namespace nx::render
