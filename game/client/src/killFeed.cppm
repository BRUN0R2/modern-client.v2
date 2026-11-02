// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The kill feed (docs/12-ui-hud-and-console.md, section 3.2): a row of each player's death the server
/// tells (cs::KillNotice) at the top right of the screen, the oldest first, for hud_deathnotice_time
/// seconds: its killer's name in its team's color, the picture of what killed it, a head's, and its own
/// name in its team's color; and a line of it in the console. Apart from the HUD and from how the game
/// plays a death. Where its rows go is as the Half-Life SDK's client places them, to be measured on the
/// original (TODO.md).
export module cs.client:killFeed;

import std;
import nx.core;
import nx.console;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.ui;
import cs.shared;
import :hudSprites;
import :windowFonts;

export namespace cs {

/// The most rows the feed shows: a new one past them makes the oldest go.
inline constexpr std::size_t kMostKillRows = 4;
/// How long a row shows, by default.
inline constexpr nx::Seconds kKillRowTime{6.0F};

/// `hud_deathnotice_time`: the seconds a row of the feed shows, 6 by default, as the original's.
struct KillFeedCvars {
    nx::console::Cvar<float> rowTime;

    /// Registers it in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<KillFeedCvars, nx::console::CvarError>;

    /// How long a row shows: none for a value that is not a time.
    [[nodiscard]] auto shownFor() const noexcept -> nx::Seconds;
};

/// The pictures of sprites/hud.txt a row of the feed shows: `d_<weapon>` of what killed, `d_skull` of what
/// the list has no picture of, and `d_headshot`.
class KillPictures {
public:
    /// The pictures among `entries`, those for screens kHudResolution wide, their sprites asked of
    /// `sprites`. Errors: PictureMissing, for `d_skull` or `d_headshot`.
    [[nodiscard]] static auto of(std::span<const nx::assets::SpriteListEntry> entries, HudSprites& sprites)
        -> nx::Result<KillPictures, HudError>;

    /// The picture of a kill by the weapon `weapon`, its id: `d_skull` for none or one the list has no
    /// picture of, as the original's client falls back to it.
    [[nodiscard]] auto icon(std::optional<std::string_view> weapon) const -> const HudSpritePicture&;
    /// The picture of a kill in the head.
    [[nodiscard]] inline auto head() const noexcept -> const HudSpritePicture& { return headPicture; }

private:
    using Pictures = std::map<std::string, HudSpritePicture, std::less<>>;

    KillPictures(Pictures pictures, HudSpritePicture skull, HudSpritePicture headShot) noexcept;

    Pictures weaponPictures; ///< Of each kill's name, `d_` left out.
    HudSpritePicture skullPicture;
    HudSpritePicture headPicture;
};

/// How wide each part of a row is drawn, in pixels: its killer's name, none for a row of no killer; the
/// picture of what killed; the head's, none but for a kill in the head; and its victim's name.
struct KillRowWidths {
    std::optional<std::int32_t> killer{};
    std::int32_t icon = 0;
    std::optional<std::int32_t> head{};
    std::int32_t victim = 0;
};

/// Where each part of a row is drawn: from where it starts across, and from where they all start down.
struct KillRowLayout {
    std::optional<std::int32_t> killerX{};
    std::int32_t iconX = 0;
    std::optional<std::int32_t> headX{};
    std::int32_t victimX = 0;
    std::int32_t y = 0;

    [[nodiscard]] inline auto operator==(const KillRowLayout&) const noexcept -> bool = default;
};

/// Where the row `row` of the feed, the first the oldest, of parts `widths` is drawn on a screen of `size`:
/// its victim's name ending at the right edge, the pictures right before it, and 5 pixels before them its
/// killer's name; from a fifteenth of the screen's height down and 2 pixels more, 20 pixels a row.
[[nodiscard]] auto layoutKillRow(const KillRowWidths& widths, std::size_t row,
                                 nx::rhi::Extent2d size) noexcept -> KillRowLayout;

/// The line the console shows of `notice`, killed by the weapon `weapon`, its id: "<killer> killed <victim>
/// with <weapon>", "<killer> killed his teammate <victim>", or for one of no killer "<victim> died".
[[nodiscard]] auto killLineOf(const KillNotice& notice, std::optional<std::string_view> weapon)
    -> std::string;

/// The feed: the glyphs of its names in kEngineFont made for a screen's height, its pictures on the GPU,
/// and the rows it shows.
class KillFeed {
public:
    /// The feed for a screen `screenHeight` pixels tall, in kEngineFont of `fonts`. Errors: those of
    /// WindowFonts::font, and Unusable (its glyphs cannot be drawn).
    [[nodiscard]] static auto open(const WindowFonts& fonts, std::uint32_t screenHeight)
        -> nx::Result<KillFeed, WindowFontError>;
    /// The feed writing its names from `glyphAtlas`.
    explicit KillFeed(nx::ui::GlyphAtlas glyphAtlas) noexcept;

    /// Puts its glyphs and pictures, those of `vfs`, on the GPU with `screen`, their uploads recorded in
    /// `commands`. Errors: those of readHudList, KillPictures::of and HudSprites::load, and GpuFailed.
    [[nodiscard]] auto load(const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen,
                            nx::rhi::CommandList& commands) -> nx::Result<void, HudError>;

    /// Shows a row of each of `notices`, in their order, from `now`, its weapon named by `catalog`; the
    /// oldest go once more than kMostKillRows show.
    void show(std::span<const KillNotice> notices, const WeaponCatalog& catalog, nx::Seconds now);
    /// Forgets the rows showing, as a map ends.
    void clear() noexcept;
    /// How many rows show.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return rows.size(); }

    /// Appends to `quads` the rows showing at `now` on a screen of `size`, once loaded, each in its place
    /// (layoutKillRow); those that showed for `rowTime` go, loaded or not. The rows are laid out again only
    /// when they or the size change: the quads of the last call are appended again until then.
    void append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size, nx::Seconds now,
                nx::Seconds rowTime);

private:
    /// A row: its players, its weapon's id, whether of a head, and when it started to show.
    struct Row {
        std::optional<NoticedPlayer> killer{};
        NoticedPlayer victim{};
        std::optional<std::string> weapon{};
        bool isHead = false;
        bool isTeamKill = false;
        nx::Seconds shownAt{};
    };

    /// What it draws on the GPU: the glyphs, and the pictures and their sprites' images.
    struct Loaded {
        nx::render::ScreenImage glyphs;
        std::vector<nx::render::ScreenImage> images;
        KillPictures pictures;
    };

    /// What append shows: which rows, as each change counts, on a screen of a size.
    struct Shown {
        std::uint64_t rowChanges = 0;
        nx::rhi::Extent2d size{};

        [[nodiscard]] inline auto operator==(const Shown&) const noexcept -> bool = default;
    };

    /// Appends to `quads` the rows as they show now on a screen of `size`, drawn from `onGpu`.
    void layOut(std::vector<nx::render::ScreenQuad>& quads, const Loaded& onGpu,
                nx::rhi::Extent2d size) const;

    nx::ui::GlyphAtlas atlas;
    std::optional<Loaded> loaded; ///< Once loaded.
    std::deque<Row> rows;         ///< The oldest first.
    std::uint64_t rowChanges = 0;
    nx::render::RetainedQuads<Shown> shown;
};

} // namespace cs
