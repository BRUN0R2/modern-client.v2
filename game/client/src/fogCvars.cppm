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

/// Why the fog cvars cannot make a fog together.
enum class FogError : std::uint8_t {
    LinearEndRequired, ///< A linear fog has no finite end.
    EndNotPastStart,   ///< A finite end is not past the start.
};

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

    /// The fog they ask for: none while fog_enable is 0. Errors: LinearEndRequired, EndNotPastStart.
    /// The handles must have the rules installed by registerIn; an observed violation returns no fog.
    [[nodiscard]] auto fog() const -> nx::Result<std::optional<nx::render::Fog>, FogError>;
    /// How long the fog takes to come, go or change.
    [[nodiscard]] auto fadeTime() const -> nx::Seconds;
};

/// The curve `text` names: "linear", "exp" or "exp2"; none for other text.
[[nodiscard]] auto fogCurveNamed(std::string_view text) noexcept -> std::optional<nx::render::FogCurve>;

/// The fog a frame shows, as the console asks for it.
class FogControl {
public:
    /// Moves the fog on by `elapsed`, toward what `cvars` ask for now: a fog they ask for anew comes or
    /// changes over their fade; one they make none of is refused with a rate-limited warning, the fog
    /// shown going on as it was. Unchanged inputs are not parsed or validated again.
    void update(const FogCvars& cvars, nx::Seconds elapsed);
    /// The fog shown now; none when none shows.
    [[nodiscard]] inline auto shown() const noexcept -> std::optional<nx::render::Fog> {
        return transition.shown();
    }

private:
    static constexpr auto kWarningInterval = std::chrono::seconds{1};

    /// The inputs that determine the requested fog; fade is read only when its target changes.
    class FogValues {
    public:
        explicit FogValues(const FogCvars& cvars);
        [[nodiscard]] auto matches(const FogCvars& cvars) const noexcept -> bool;

    private:
        bool isEnabled;
        std::string color;
        std::string curve;
        float density;
        float start;
        float end;
        float opacity;
    };

    nx::render::FogTransition transition;
    std::optional<nx::render::Fog> requestedFog;
    std::optional<FogValues> processed;
    std::optional<nx::Failure<FogError>> pendingFailure;
    bool hasPendingWarning = false;
    nx::log::RateLimit warningLimit{kWarningInterval};
};

} // namespace cs
