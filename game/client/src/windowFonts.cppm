// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The fonts of the scheme of the installation's windows (docs/12-ui-hud-and-console.md, section 2.1):
/// the console's and cl_showfps's, read from the scheme once, their faces among the system's fonts.
export module cs.client:windowFonts;

import std;
import nx.core;
import nx.assets;
import nx.ui;

export namespace cs {

/// The scheme of the installation's windows: VGUI2's TrackerScheme.res.
inline constexpr std::string_view kWindowScheme = "resource/TrackerScheme.res";

/// Why a font of kWindowScheme cannot be had.
enum class WindowFontError : std::uint8_t {
    SchemeUnreadable, ///< kWindowScheme is missing, or is no scheme (cause).
    NoSuchFont,       ///< The scheme has no font of that name; the detail names it.
    Unusable,         ///< No face of the font opens for the screen, or its glyphs cannot be drawn (cause).
};

/// The fonts of kWindowScheme.
class WindowFonts {
public:
    /// kWindowScheme of `vfs`, its faces from `catalog`, which must outlive it, given the system's fonts
    /// (a folder of them that cannot be read is logged and left out). Errors: SchemeUnreadable.
    [[nodiscard]] static auto open(const nx::assets::Vfs& vfs, nx::ui::FontCatalog& catalog)
        -> nx::Result<WindowFonts, WindowFontError>;

    /// The font `name` made for a screen `screenHeight` pixels tall. Errors: NoSuchFont, Unusable.
    [[nodiscard]] auto font(std::string_view name, std::uint32_t screenHeight) const
        -> nx::Result<nx::ui::SchemeFontSet, WindowFontError>;

private:
    WindowFonts(nx::assets::Scheme scheme, nx::ui::FontCatalog& catalog) noexcept;

    nx::assets::Scheme m_scheme;
    nx::ui::FontCatalog* m_catalog;
};

} // namespace cs
