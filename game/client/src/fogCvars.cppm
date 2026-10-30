// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The fog the console asks for (docs/05-rendering.md, section 10): its cvars, and the fog a frame shows
/// as they change, coming in, going out and changing over `fog_fade` (nx::render::FogTransition), so that
/// it never appears, vanishes or turns another color at once.
export module cs.client:fogCvars;

import std;
import nx.core;
import nx.console;
import nx.render;

export namespace cs {

/// The fog's cvars, none of them archived: a map's or a mod's fog, not the player's.
struct FogCvars {
    nx::console::Cvar<bool> enabled;      ///< `fog_enable`: whether the fog shows.
    nx::console::Cvar<std::string> color; ///< `fog_color`: red, green and blue, from 0 to 255.
    nx::console::Cvar<std::string> curve; ///< `fog_curve`: linear, exp or exp2.
    nx::console::Cvar<float> density;     ///< `fog_density`: per unit, for exp and exp2.
    nx::console::Cvar<float> start;       ///< `fog_start`: the nearest it hides anything at.
    nx::console::Cvar<float> end;         ///< `fog_end`: the farthest it thickens to; 0, without end.
    nx::console::Cvar<float> opacity;     ///< `fog_opacity`: the most of a color it hides, 0 to 1.
    nx::console::Cvar<float> fade;        ///< `fog_fade`: the seconds it takes to come, go or change.

    /// Registers them in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<FogCvars, nx::console::CvarError>;

    /// The fog they ask for: none while fog_enable is 0; why when they make none (an end not past the
    /// start, a linear fog without an end).
    [[nodiscard]] auto fog() const -> std::expected<std::optional<nx::render::Fog>, std::string_view>;
    /// How long the fog takes to come, go or change.
    [[nodiscard]] auto fadeTime() const -> nx::Seconds;
};

/// The curve `text` names: "linear", "exp" or "exp2"; none for other text.
[[nodiscard]] auto fogCurveNamed(std::string_view text) noexcept -> std::optional<nx::render::FogCurve>;

/// The fog a frame shows, as the console asks for it.
class FogControl {
public:
    /// Moves the fog on by `elapsed`, toward what `cvars` ask for now: a fog they ask for anew comes or
    /// changes over their fade; one they make none of is refused with a warning, once, the fog shown
    /// going on as it was.
    void update(const FogCvars& cvars, nx::Seconds elapsed);
    /// The fog shown now; none when none shows.
    [[nodiscard]] inline auto shown() const noexcept -> std::optional<nx::render::Fog> {
        return m_transition.shown();
    }

private:
    nx::render::FogTransition m_transition;
    std::optional<nx::render::Fog> m_asked{};
    std::optional<std::string_view> m_refused{};
};

} // namespace cs
