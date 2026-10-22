// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The classic HUD (docs/12-ui-hud-and-console.md, section 3): the player's health, armor and money,
/// in the pictures sprites/hud.txt names, and the crosshair, where Counter-Strike 1.6 draws them and
/// as it draws them, measured on the original (docs/12, section 3.2). Its pictures keep their size at every
/// resolution, each drawn as the original draws a HUD sprite (nx::render::spriteQuad), added in the
/// HUD's color; its elements keep to the screen's edges. Its numbers have no ceiling (docs/12,
/// section 3.4): a number of more digits than the original's room for it widens it, and pushes away
/// what is beside it.
///
/// Today: health, armor and money as they rest, and the crosshair of a player standing, with the
/// original's cvars. The ammo, the round's time, the status icons, the radar, the crosshair opening
/// as the player moves, and the flash of a number that changes, come next (TODO.md).
export module cs.client:hud;

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;

export namespace cs {

/// Why the HUD's pictures could not be read or put on the GPU.
enum class HudError : std::uint8_t {
    ListUnreadable,   ///< sprites/hud.txt is missing or invalid (cause).
    PictureMissing,   ///< hud.txt has no entry for screens 640 wide of a picture the HUD draws (detail).
    SpriteUnreadable, ///< A sprite of the HUD's pictures is missing or invalid (cause).
    GpuFailed,        ///< The GPU refused a sprite (cause: the ScreenError).
};

/// The pictures of hud.txt the HUD draws.
enum class HudPicture : std::uint8_t {
    Number0, ///< `number_0`, and the digits after it in their order.
    Number1,
    Number2,
    Number3,
    Number4,
    Number5,
    Number6,
    Number7,
    Number8,
    Number9,
    Cross,     ///< `cross`, before the health.
    SuitEmpty, ///< `suit_empty`, before the armor.
    SuitFull,  ///< `suit_full`, over suit_empty as far up as the armor reaches.
    Dollar,    ///< `dollar`, before the money.
    Count,
};

/// Where each of the HUD's pictures is: its entry of hud.txt for screens 640 wide, which the original
/// draws at every resolution from 640 wide up.
using HudSheet = nx::EnumArray<HudPicture, nx::assets::SpriteListEntry>;

/// The HUD's pictures among `entries`, those of a sprite list: the first entry for screens 640 wide
/// named as each. Errors: PictureMissing.
[[nodiscard]] auto hudSheetOf(std::span<const nx::assets::SpriteListEntry> entries)
    -> nx::Result<HudSheet, HudError>;

/// What the HUD shows of the player.
struct HudView {
    nx::HitPoints health{};
    nx::ArmorPoints armor{};
    nx::Money money{};
    /// How far from the middle the crosshair of the weapon in hand is (cs::kKnifeCrosshairDistance);
    /// none without a crosshair.
    std::optional<std::uint32_t> crosshairDistance{};
};

/// The size of the crosshair, as `cl_crosshair_size` names it.
enum class CrosshairSize : std::uint8_t {
    Auto,   ///< `auto`: small on screens under 800 wide, medium under 1024, large from 1024.
    Small,  ///< `small`.
    Medium, ///< `medium`.
    Large,  ///< `large`.
    Count,
};

/// The size `text` names: "auto", "small", "medium" or "large"; none for other text.
[[nodiscard]] auto crosshairSizeNamed(std::string_view text) noexcept -> std::optional<CrosshairSize>;
/// The color `text` gives: three whole numbers from 0 to 255, red, green and blue, spaces between them
/// ("50 250 50"); none for other text.
[[nodiscard]] auto crosshairColorOf(std::string_view text) noexcept -> std::optional<nx::render::ScreenColor>;

/// How the crosshair is drawn, as its cvars say.
struct CrosshairStyle {
    nx::render::ScreenColor color{.red = 50, .green = 250, .blue = 50};
    CrosshairSize size = CrosshairSize::Auto;
    bool isTranslucent = true; ///< Added to what is behind it; else drawn over it as it is.
};

/// The HUD's cvars, by the original's names, archived, with Counter-Strike 1.6's values:
/// `cl_crosshair_color` (50 250 50), `cl_crosshair_size` (auto) and `cl_crosshair_translucent` (1).
struct HudCvars {
    nx::console::Cvar<std::string> crosshairColor;
    nx::console::Cvar<std::string> crosshairSize;
    nx::console::Cvar<bool> crosshairTranslucent;

    /// Registers them in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<HudCvars, nx::console::CvarError>;

    /// The crosshair they ask for.
    [[nodiscard]] auto crosshair() const -> CrosshairStyle;
};

/// Where the crosshair of a weapon `distance` from the middle is drawn in `crosshairSize` on a screen
/// of `size`: four rectangles a pixel thick, left of the middle, right of it, above it and under it,
/// each ⌊5·s·f⌋ long and ⌊distance·s·f⌋ from it, s the screen's width over 640 and f the size's factor
/// (small 5/8, medium 4/5, large 1): the left one ends `gap` before the middle's column, the right one
/// starts `gap` after it, and the top and bottom ones likewise.
[[nodiscard]] auto crosshairRects(CrosshairSize crosshairSize, std::uint32_t distance, nx::rhi::Extent2d size)
    -> std::array<nx::render::ScreenRect, 4>;

/// The digits the HUD shows of `health`, a finite health, as the game makes it a whole number for its
/// client (ReGameDLL_CS's CBasePlayer::UpdateClientData): its whole part, none below 0, and 1 above 0
/// up to 1, so that a player alive never shows 0; with no ceiling, the original's 255 gone.
[[nodiscard]] auto healthDigits(nx::HitPoints health) -> std::string;
/// The digits the HUD shows of `armor`, a finite armor: its whole part, none below 0.
[[nodiscard]] auto armorDigits(nx::ArmorPoints armor) -> std::string;
/// The digits the HUD shows of `money`: none below 0.
[[nodiscard]] auto moneyDigits(nx::Money money) -> std::string;

/// A picture the HUD draws: `picture`, but its first `cutTop` rows, with its top left at `x`, `y`.
struct HudMark {
    HudPicture picture = HudPicture::Number0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t cutTop = 0;

    [[nodiscard]] inline auto operator==(const HudMark&) const noexcept -> bool = default;
};

/// Where the HUD draws `view` on a screen of `size`, its pictures those of `sheet`, in their order:
/// - the health's cross 10 pixels from the left and 40 from the bottom, its digits after it from 36,
///   aligned to the right of room for three;
/// - the armor's suit a fifth of the way across, 37 from the bottom, suit_full over suit_empty as far
///   up as the armor is of 100, its digits right after the suit, aligned to the right of room for
///   three; a health of more digits pushes the suit right, as far from them as it is at 640 wide;
/// - the money's digits a row up, 75 from the bottom, aligned to the right of room for five 16 from
///   the right edge, its dollar right before that room.
[[nodiscard]] auto layoutHud(const HudView& view, const HudSheet& sheet, nx::rhi::Extent2d size)
    -> std::vector<HudMark>;

/// The HUD's pictures on the GPU.
class Hud {
public:
    /// Reads sprites/hud.txt from `vfs`, and the sprites of the HUD's pictures, put on the GPU by
    /// `screen` with their uploads recorded in `commands`. Errors: ListUnreadable, PictureMissing,
    /// SpriteUnreadable, GpuFailed.
    [[nodiscard]] static auto load(const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen,
                                   nx::rhi::CommandList& commands) -> nx::Result<Hud, HudError>;

    /// Appends to `quads` the HUD of `view` on a screen of `size` (layoutHud), each picture drawn as the
    /// original draws a HUD sprite, added in the HUD's color; then the crosshair in `crosshair`'s
    /// style (crosshairRects), added or drawn as it is. The quads point into the HUD.
    void append(std::vector<nx::render::ScreenQuad>& quads, const HudView& view,
                const CrosshairStyle& crosshair, nx::rhi::Extent2d size) const;

private:
    Hud(HudSheet sheet, std::vector<nx::render::ScreenImage> images,
        nx::EnumArray<HudPicture, std::size_t> imageOf) noexcept;

    HudSheet m_sheet;
    std::vector<nx::render::ScreenImage> m_images;    ///< The first frame of each sprite of the pictures.
    nx::EnumArray<HudPicture, std::size_t> m_imageOf; ///< Which of the images each picture is in.
};

} // namespace cs
