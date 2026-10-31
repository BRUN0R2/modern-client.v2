// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The step indicator (docs/12-ui-hud-and-console.md, section 3.2): a mark of footprints around the middle
/// of the screen for each entity's body heard stepping (cs::EntityStep), the way it is from where the
/// player looks, as data/cstrike/stepIndicator.toml makes it look (cs::StepIndicatorLook), moving every
/// frame by the time since its steps; apart from the HUD, the scene and how the bodies step, and shown
/// while hud_stepindicator is 1.
export module cs.client:stepIndicator;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.rhi;
import nx.render;
import nx.console;
import cs.shared;

export namespace cs {

/// `hud_stepindicator`: whether the steps of the entities' bodies show around the middle of the screen.
struct StepIndicatorCvars {
    nx::console::Cvar<bool> shown;

    /// Registers it in `console`, archived, 1 by default. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<StepIndicatorCvars, nx::console::CvarError>;
};

/// The way `source` is from `listener` looking along `yaw` across, as a turn clockwise from straight ahead
/// seen from above, in radians from -pi to pi: 0 ahead, pi/2 to the right, pi behind.
[[nodiscard]] auto bearingOf(nx::Vec3 listener, nx::Degrees yaw, nx::Vec3 source) noexcept -> float;

/// How loud a step `distance` away shows, from 0 to 1: wholly within the look's near distance, easing out
/// to none at its far one.
[[nodiscard]] auto hearingOf(const StepIndicatorLook& look, nx::HammerUnits distance) noexcept -> float;

/// How lit a mark is `since` its body's last step, as the look lights it, from 0 to 1: easing in from
/// `from`, what it showed as it stepped, over the attack; holding; then fading out over the release.
[[nodiscard]] auto stepLevelAt(const StepIndicatorLook& look, nx::Seconds since, float from) noexcept
    -> float;

/// The marks of the steps heard: a picture of footprints turned each of kStepMarkTurns ways, drawn once and
/// put on the GPU with the screen's pictures, and the bodies heard stepping, at most the look's most.
class StepIndicator {
public:
    /// The ways a mark is turned, a turn split evenly: the nearest shows the way a body is.
    static constexpr std::uint32_t kStepMarkTurns = 32;

    /// The indicator of `look`, which must outlive it, its picture drawn.
    explicit StepIndicator(const StepIndicatorLook& look);

    /// Puts its picture on the GPU with `screen`, its uploads recorded in `commands`. Errors: those of
    /// nx::render::ScreenRenderer::loadImage.
    [[nodiscard]] auto load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
        -> nx::Result<void, nx::render::ScreenError>;

    /// Hears `steps` at `now`, in their order: each lights its body's mark again where it stepped, from
    /// what it showed; a body not heard yet takes the place of the faintest once the look's most are kept.
    void hear(std::span<const EntityStep> steps, nx::Seconds now);
    /// Forgets the bodies heard, as a map ends.
    inline void clear() noexcept { m_heard.clear(); }
    /// How many bodies are heard.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return m_heard.size(); }

    /// Appends to `quads` the marks at `now`, once loaded, around the middle of a screen of `size`, for a
    /// player at `listener` looking along `yaw` across: the loudest first, at most the look's most, none
    /// too close to another, each as lit as its steps and as loud as its distance. The bodies no longer
    /// lit go, loaded or not.
    void append(std::vector<nx::render::ScreenQuad>& quads, nx::Vec3 listener, nx::Degrees yaw,
                nx::rhi::Extent2d size, nx::Seconds now);

private:
    /// A body heard: where it last stepped, when, and how lit its mark was then.
    struct HeardBody {
        nx::ecs::Entity entity{};
        nx::Vec3 position{};
        nx::Seconds steppedAt{};
        float from = 0.0F;
    };
    /// How long a mark shows after its body's last step.
    [[nodiscard]] auto lit() const noexcept -> nx::Seconds;

    const StepIndicatorLook* m_look;
    std::vector<std::byte> m_picture; ///< RGBA texels, each turn's mark a cell of its own, in a row.
    std::optional<nx::render::ScreenImage> m_image; ///< The picture on the GPU, once loaded.
    std::vector<HeardBody> m_heard;
};

} // namespace cs
