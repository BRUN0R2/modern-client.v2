// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Panels (ADR-0020): the rectangles the interface is made of, each in its parent, as VGUI2 places
/// them. A layout (.res) sets a panel's place and size, from its parent's right (`r`) or middle
/// (`c`) when it says so, and which of its edges follow its parent's as the parent changes size
/// (pinCorner, autoResize); the settings' meanings are those of the installation's own layouts.
export module nx.ui:panel;

import std;
import nx.core;
import nx.assets;
import nx.render;
import :painter;

export namespace nx::ui {

/// What a layout sets that cannot be read.
enum class PanelError : std::uint8_t {
    BadSetting, ///< A setting is not the number it must be; the detail names it.
};

/// The corner of its parent a panel keeps its distance to as the parent changes size (`pinCorner`).
enum class PinCorner : std::uint8_t {
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight,
    Count,
};

/// The edges of a panel that keep their distance to their parent's as the parent changes size, the
/// panel growing with it (`autoResize`).
enum class AutoResize : std::uint8_t {
    None,
    Right,
    Down,
    DownAndRight,
    Count,
};

/// A panel's place and size, in pixels from its parent's top left.
struct PanelBounds {
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::int32_t wide = 0;
    std::int32_t tall = 0;

    [[nodiscard]] inline auto operator==(const PanelBounds&) const noexcept -> bool = default;
};

/// A panel of the interface, its children over it in their order. It stays where it is made: its
/// children point to it. A control is a kind of panel that draws more than its background.
class Panel {
public:
    /// A panel named `name` at `bounds`, visible and enabled, pinned to its parent's top left.
    explicit Panel(std::string name, PanelBounds bounds = {});

    Panel(const Panel&) = delete;
    auto operator=(const Panel&) -> Panel& = delete;
    Panel(Panel&&) = delete;
    auto operator=(Panel&&) -> Panel& = delete;
    virtual ~Panel() = default;

    [[nodiscard]] inline auto name() const noexcept -> std::string_view { return m_name; }
    [[nodiscard]] inline auto bounds() const noexcept -> const PanelBounds& { return m_bounds; }
    [[nodiscard]] inline auto isVisible() const noexcept -> bool { return m_isVisible; }
    [[nodiscard]] inline auto isEnabled() const noexcept -> bool { return m_isEnabled; }
    [[nodiscard]] inline auto pinCorner() const noexcept -> PinCorner { return m_pinCorner; }
    [[nodiscard]] inline auto autoResize() const noexcept -> AutoResize { return m_autoResize; }
    /// Its place in the order the tab key moves through (`tabPosition`); 0 for none.
    [[nodiscard]] inline auto tabPosition() const noexcept -> std::int32_t { return m_tabPosition; }
    /// The panel it is in; null for one in none.
    [[nodiscard]] inline auto parent() const noexcept -> Panel* { return m_parent; }
    [[nodiscard]] inline auto children() const noexcept -> std::span<const std::unique_ptr<Panel>> {
        return m_children;
    }
    /// The color its background is filled with; none for no background.
    [[nodiscard]] inline auto background() const noexcept -> const std::optional<render::ScreenColor>& {
        return m_background;
    }

    /// Puts `child` in the panel, after its other children: over them. Returns it.
    auto add(std::unique_ptr<Panel> child) -> Panel&;
    /// The child named `name`, letters in any case; null when there is none.
    [[nodiscard]] auto find(std::string_view name) const noexcept -> Panel*;

    /// Moves and sizes the panel. Its children follow: each of their edges keeps its distance to the
    /// edge of the panel it follows.
    void setBounds(PanelBounds bounds);
    inline void setVisible(bool isVisible) noexcept { m_isVisible = isVisible; }
    inline void setEnabled(bool isEnabled) noexcept { m_isEnabled = isEnabled; }
    inline void setAnchors(PinCorner pinCorner, AutoResize autoResize) noexcept {
        m_pinCorner = pinCorner;
        m_autoResize = autoResize;
    }
    inline void setBackground(std::optional<render::ScreenColor> color) noexcept { m_background = color; }

    /// Draws the panel with `painter`, its parent's, then over it each of its visible children in
    /// their order, each kept within it. A hidden panel draws nothing, nor do its children.
    void paintTree(const PanelPainter& painter) const;

    /// Applies `settings`, the panel's block of a layout: `xpos` and `ypos` (from the parent's right or
    /// bottom with `r`, from its middle with `c`), `wide`, `tall`, `visible`, `enabled`, `pinCorner`,
    /// `autoResize` and `tabPosition`, their names in any case; a setting left out keeps its value.
    /// Errors: BadSetting.
    [[nodiscard]] auto applySettings(const assets::KeyValuesEntry& settings) -> Result<void, PanelError>;
    /// Applies each block of `layout`, a layout's root block, to the child its key names; a block that
    /// names no child is left. Errors: those of applySettings.
    [[nodiscard]] auto applyLayout(const assets::KeyValuesEntry& layout) -> Result<void, PanelError>;

protected:
    /// Draws what the panel itself shows, from its top left: its background. A control draws more.
    virtual void paint(PanelPainter& painter) const;

private:
    /// Moves the panel's edges that follow its parent's, which grew by `wider` and `taller`.
    void followParent(std::int32_t wider, std::int32_t taller);

    std::string m_name;
    PanelBounds m_bounds;
    bool m_isVisible = true;
    bool m_isEnabled = true;
    PinCorner m_pinCorner = PinCorner::TopLeft;
    AutoResize m_autoResize = AutoResize::None;
    std::int32_t m_tabPosition = 0;
    Panel* m_parent = nullptr;
    std::vector<std::unique_ptr<Panel>> m_children;
    std::optional<render::ScreenColor> m_background;
};

} // namespace nx::ui
