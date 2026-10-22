// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console's window as the client shows it (docs/12-ui-hud-and-console.md, section 5): its font,
/// the Default of the scheme of the installation's windows, from the system's fonts; the characters
/// its atlas holds; and where it goes on the screen.
export module cs.client:console;

import std;
import nx.core;
import nx.assets;
import nx.rhi;
import nx.ui;

export namespace cs {

/// The scheme of the installation's windows, the console's among them.
inline constexpr std::string_view kWindowScheme = "resource/TrackerScheme.res";
/// The font of that scheme the console writes in.
inline constexpr std::string_view kConsoleFont = "Default";

/// The characters the console's atlas holds: those of Windows' code page 1252 that print, which the
/// original's console shows.
[[nodiscard]] auto consoleCharacters() -> std::u32string;

/// The console's font for a screen `screenHeight` pixels tall: kConsoleFont of kWindowScheme in
/// `vfs`, its faces from `catalog`, which is given the system's fonts. None, the reason logged, when
/// the scheme, the font or a face of it is missing: the console then has no window.
[[nodiscard]] auto openConsoleFont(const nx::assets::Vfs& vfs, nx::ui::FontCatalog& catalog,
                                   std::uint32_t screenHeight) -> std::optional<nx::ui::SchemeFontSet>;

/// Where the console's window goes on a screen of `size`: in its middle, three quarters of it each
/// way, until the original's place is measured.
[[nodiscard]] auto consoleBounds(nx::rhi::Extent2d size) -> nx::ui::PanelBounds;

} // namespace cs
