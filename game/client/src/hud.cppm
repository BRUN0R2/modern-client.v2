// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The classic HUD (docs/12-ui-hud-and-console.md, section 3): the player's health, armor, money and
/// ammo, in the pictures sprites/hud.txt names and the kinds of ammo's own (cs::AmmoType::picture),
/// and the crosshair, where Counter-Strike 1.6 draws them and
/// as it draws them, measured on the original (docs/12, section 3.2). Its pictures keep their size at every
/// resolution, each drawn as the original draws a HUD sprite (nx::render::spriteQuad), added in the
/// HUD's color; its elements keep to the screen's edges. Its numbers have no ceiling (docs/12,
/// section 3.4): a number of more digits than the original's room for it widens it, and pushes away
/// what is beside it.
///
/// Today: health, armor, money and ammo as they rest, the round's time, and the crosshair of a player
/// standing, with the original's cvars. The status icons, the radar, the weapon menu, the crosshair
/// opening as the player moves, the timer's color as the round runs out, and the flash of a number
/// that changes, come next (TODO.md).
export module cs.client:hud;

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;

export namespace cs {

/// Why the HUD's pictures could not be read or put on the GPU.
enum class HudError : std::uint8_t {
    ListUnreadable,   ///< sprites/hud.txt is missing or invalid (cause).
    PictureMissing,   ///< A list has no entry for screens 640 wide of a picture the HUD draws (detail).
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
    Stopwatch, ///< `stopwatch`, before the round's time.
    Count,
};

/// Where each of the HUD's pictures is: its entry of hud.txt for screens 640 wide, which the original
/// draws at every resolution from 640 wide up.
using HudSheet = nx::EnumArray<HudPicture, nx::assets::SpriteListEntry>;

/// The HUD's pictures among `entries`, those of a sprite list: the first entry for screens 640 wide
/// named as each. Errors: PictureMissing.
[[nodiscard]] auto hudSheetOf(std::span<const nx::assets::SpriteListEntry> entries)
    -> nx::Result<HudSheet, HudError>;

/// The ammo the HUD shows: of the weapon in hand, one that fires ammo.
struct HudAmmo {
    std::string_view ammo{}; ///< Its kind, whose picture is drawn.
    std::uint32_t clip{};
    std::uint32_t reserve{}; ///< The rounds of its kind the player carries besides the clips.

    [[nodiscard]] inline auto operator==(const HudAmmo&) const noexcept -> bool = default;
};

/// The ammo the HUD shows of `inventory`, whose weapons are of `catalog`: of the weapon it holds, when
/// it fires ammo; none.
[[nodiscard]] auto hudAmmoOf(const Inventory& inventory, const WeaponCatalog& catalog)
    -> std::optional<HudAmmo>;

/// What the HUD shows of the player.
struct HudView {
    nx::HitPoints health{};
    nx::ArmorPoints armor{};
    nx::Money money{};
    std::optional<HudAmmo> ammo{}; ///< None without a weapon in hand that fires ammo.
    /// How far from the middle the crosshair of the weapon in hand is (cs::kKnifeCrosshairDistance);
    /// none without a crosshair.
    std::optional<std::uint32_t> crosshairDistance{};
    /// The whole seconds the round's timer shows (cs::RoundTimer); none without a round.
    std::optional<std::uint32_t> roundTime{};

    [[nodiscard]] inline auto operator==(const HudView&) const noexcept -> bool = default;
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

    [[nodiscard]] inline auto operator==(const CrosshairStyle&) const noexcept -> bool = default;
};

/// The HUD's cvars, by the original's names, archived, with Counter-Strike 1.6's values:
/// `cl_crosshair_color` (50 250 50), `cl_crosshair_size` (auto) and `cl_crosshair_translucent` (1).
struct HudCvars {
    nx::console::Cvar<std::string> crosshairColor;
    nx::console::Cvar<std::string> crosshairSize;
    nx::console::Cvar<bool> crosshairTranslucent;
    nx::console::Cvar<bool> rightHand; ///< `cl_righthand`: the weapons in the right hand, else the left.

    /// Registers them in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<HudCvars, nx::console::CvarError>;

    /// The crosshair they ask for.
    [[nodiscard]] auto crosshair() const -> CrosshairStyle;
    /// The hand the player holds its weapons in.
    [[nodiscard]] auto hand() const -> Hand;
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

/// Where the HUD draws the ammo of a weapon in hand.
struct AmmoLayout {
    std::vector<HudMark> digits{}; ///< The clip's, then the reserve's.
    nx::render::ScreenRect bar{};  ///< Between them, of the HUD's color, added.
    std::int32_t pictureX = 0;     ///< The top left corner of the picture of the ammo's kind.
    std::int32_t pictureY = 0;
};

/// Where the HUD draws `ammo` on a screen of `size`, its digits those of `sheet`: the picture of its
/// kind 42 pixels from the right edge and 40 from the bottom; the reserve's digits 37 from the
/// bottom, aligned to the right of room for three that ends at the picture; a bar two pixels wide
/// and the digits' height ten before that room, and the clip's digits aligned to the right of room for
/// three that ends ten before the bar. More digits widen a room to the left, pushing what is left of
/// it.
[[nodiscard]] auto layoutAmmo(const HudAmmo& ammo, const HudSheet& sheet, nx::rhi::Extent2d size)
    -> AmmoLayout;

/// Where the HUD draws the round's time.
struct TimerLayout {
    std::vector<HudMark> marks{}; ///< The stopwatch, the minutes' digits, then the seconds'.
    /// The colon between the minutes and the seconds: two squares, of the HUD's color at an alpha of
    /// kTimerColonAlpha, added.
    std::array<nx::render::ScreenRect, 2> colon{};
};

/// The alpha the colon of the round's time is added at, measured on the original: it adds (81, 50,
/// 0) where the digits add (100, 62, 0) at most.
inline constexpr std::uint8_t kTimerColonAlpha = 81;

/// Where the HUD draws `seconds` of the round's time on a screen of size `size`, its pictures those of
/// `sheet`, 37 pixels from the bottom (docs/12 section 3.2): its whole minutes aligned to the right
/// of room for two that ends in the middle, the stopwatch right before that room; the colon, two
/// squares two pixels wide, 2 right of the middle, 28 and 22 from the bottom; the seconds past the
/// whole minutes in two digits, 6 right of the middle. More minutes widen their room to the left,
/// pushing the stopwatch.
[[nodiscard]] auto layoutTimer(std::uint32_t seconds, const HudSheet& sheet, nx::rhi::Extent2d size)
    -> TimerLayout;

/// The HUD's pictures on the GPU.
class Hud {
public:
    /// Reads sprites/hud.txt from `vfs`, and the sprites of the HUD's pictures and of the pictures of
    /// the kinds of `ammo`, put on the GPU by `screen` with their uploads recorded in `commands`.
    /// Errors: ListUnreadable, PictureMissing, SpriteUnreadable, GpuFailed.
    [[nodiscard]] static auto load(const nx::assets::Vfs& vfs, std::span<const AmmoType> ammo,
                                   nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
        -> nx::Result<Hud, HudError>;

    /// Appends to `quads` the HUD of `view` on a screen of `size` (layoutHud, layoutAmmo,
    /// layoutTimer), each picture drawn as the original draws a HUD sprite, added in the HUD's color,
    /// the ammo's bar added in it, the picture of its kind when the HUD has one, and the timer's colon;
    /// then the crosshair in `crosshair`'s style (crosshairRects), added or drawn as it is. The quads
    /// point into the HUD. The HUD is laid out again only when what it shows changes, about once a
    /// second as the round's timer counts: the quads of the last call are appended again until then.
    void append(std::vector<nx::render::ScreenQuad>& quads, const HudView& view,
                const CrosshairStyle& crosshair, nx::rhi::Extent2d size);

private:
    /// What append shows: what it lays out.
    struct Shown {
        HudView view;
        CrosshairStyle crosshair;
        nx::rhi::Extent2d size;

        [[nodiscard]] inline auto operator==(const Shown&) const noexcept -> bool = default;
    };

    /// Appends to `quads` the HUD of `view`, laid out, as append describes.
    void layOut(std::vector<nx::render::ScreenQuad>& quads, const HudView& view,
                const CrosshairStyle& crosshair, nx::rhi::Extent2d size) const;

    /// A picture of a sprite: its rectangle, and which of the images its sprite is.
    struct Picture {
        nx::assets::SpriteRect rect{};
        std::size_t image = 0;
    };
    using AmmoPictures = std::map<std::string, Picture, std::less<>>;

    Hud(HudSheet sheet, std::vector<nx::render::ScreenImage> images,
        nx::EnumArray<HudPicture, std::size_t> imageOf, AmmoPictures ammoPictures) noexcept;

    HudSheet m_sheet;
    std::vector<nx::render::ScreenImage> m_images;    ///< The first frame of each sprite of the pictures.
    nx::EnumArray<HudPicture, std::size_t> m_imageOf; ///< Which of the images each picture is in.
    AmmoPictures m_ammoPictures;                      ///< Of each kind of ammo, by its id.
    nx::render::RetainedQuads<Shown> m_shown;
};

} // namespace cs
