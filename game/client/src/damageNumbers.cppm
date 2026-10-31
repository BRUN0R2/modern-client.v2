// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The damage numbers (docs/12-ui-hud-and-console.md, section 3.2): a number of each hit of the player's
/// (cs::DamageDealt) over the head of the body it met, over everything else, as
/// data/cstrike/damageNumbers.toml makes it look (cs::DamageNumberLook), moving every frame by the time it
/// has shown; apart from the HUD, the scene and how the hits are dealt, and shown while hud_damagenumbers
/// is 1.
export module cs.client:damageNumbers;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.render;
import nx.ui;
import nx.console;
import cs.shared;

export namespace cs {

/// Why the damage numbers cannot be shown.
enum class DamageNumbersError : std::uint8_t {
    NoFont,   ///< No face of the look's font's family is among the system's fonts; the detail names it.
    Unusable, ///< Its face does not open at the size, or its glyphs cannot be drawn (cause).
};

/// `hud_damagenumbers`: whether the damage of the player's hits shows.
struct DamageNumberCvars {
    nx::console::Cvar<bool> shown;

    /// Registers it in `console`, archived, 1 by default. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<DamageNumberCvars, nx::console::CvarError>;
};

/// How a number shows at an age: the share of its whole size it is drawn at, how opaque, from 0 to 1, and
/// the share of its rise it rose.
struct DamageNumberPose {
    float scale = 0.0F;
    float opacity = 0.0F;
    float rise = 0.0F;
};

/// How a number of `look` shows `age` after its hit, as DamageNumberLook says; none once its life is over.
[[nodiscard]] auto damageNumberPoseAt(const DamageNumberLook& look, nx::Seconds age) noexcept
    -> std::optional<DamageNumberPose>;

/// Where on a screen `screenHeight` pixels tall the middle of a number of `look`'s line is drawn, the top
/// of the head of the body its hit met drawn at `headTop`, as `pose` shows it, `side` of the look's spread
/// to a side (from -1, left, to 1): its digits' foot, `footBelowMiddle` pixels below its middle at its whole
/// size, the look's gap over the head, risen by the pose's share of the look's rise.
[[nodiscard]] auto damageNumberMiddleOf(const DamageNumberLook& look, const DamageNumberPose& pose,
                                        float side, nx::render::ScreenPoint headTop, float screenHeight,
                                        float footBelowMiddle) noexcept -> nx::render::ScreenPoint;

/// What a number says of `damage`: its whole points, to the nearest, 1 at least.
[[nodiscard]] auto damageText(nx::Damage damage) -> std::string;

/// The numbers the player's hits show: the glyphs of the digits in the look's font, put on the GPU with
/// the screen's pictures, and the numbers showing.
class DamageNumbers {
public:
    /// The numbers of `look`, which must outlive them, for a screen `screenHeight` pixels tall, in the
    /// look's font among `fonts`' faces. Errors: NoFont, Unusable.
    [[nodiscard]] static auto open(nx::ui::FontCatalog& fonts, const DamageNumberLook& look,
                                   std::uint32_t screenHeight)
        -> nx::Result<DamageNumbers, DamageNumbersError>;

    /// Puts the glyphs on the GPU with `screen`, its uploads recorded in `commands`. Errors: those of
    /// nx::render::ScreenRenderer::loadImage.
    [[nodiscard]] auto load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
        -> nx::Result<void, nx::render::ScreenError>;

    /// Shows a number of each of `dealt`, in their order, from `now`, each drawn to a side of its own; the
    /// oldest go once more than the look's most show.
    void show(std::span<const DamageDealt> dealt, nx::Seconds now);
    /// Forgets the numbers showing, as a map ends.
    inline void clear() noexcept { m_numbers.clear(); }
    /// How many numbers show.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return m_numbers.size(); }

    /// Appends to `quads` the numbers showing at `now`, once loaded, each over the head of the body its hit
    /// met (damageNumberMiddleOf) as `viewProjection` draws the map on a screen of `size`, the newest over
    /// the older, its outline under its digits; the numbers whose life is over go, loaded or not. One over a
    /// head behind the eye is not drawn.
    void append(std::vector<nx::render::ScreenQuad>& quads, const nx::Mat4& viewProjection,
                nx::rhi::Extent2d size, nx::Seconds now);

private:
    /// A number showing: over the head of the body its hit met, what it says, whether of a head, when it
    /// was hit, and the share of the look's spread it is drawn to a side, from -1 (left) to 1.
    struct Number {
        nx::Vec3 headTop{};
        std::string text{};
        bool isHead = false;
        nx::Seconds bornAt{};
        float side = 0.0F;
    };

    DamageNumbers(nx::ui::GlyphAtlas atlas, const DamageNumberLook& look, float wholeHeight) noexcept;

    nx::ui::GlyphAtlas m_atlas;                     ///< Of the digits, at the largest a number is drawn.
    const DamageNumberLook* m_look;                 ///< Of the game's content.
    float m_wholeHeight = 0.0F;                     ///< A number's line at its whole size, in pixels.
    std::optional<nx::render::ScreenImage> m_image; ///< The atlas on the GPU, once loaded.
    std::deque<Number> m_numbers;                   ///< Oldest first.
    nx::Random m_sides;                             ///< What draws each number's side.
};

} // namespace cs
