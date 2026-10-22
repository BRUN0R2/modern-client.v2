// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.ui — the interface's toolkit (docs/12-ui-hud-and-console.md, section 2; ADR-0020): retained
/// panels in VGUI2's model, laid out by VGUI2's layouts (.res), styled by its schemes, and drawn by
/// nx.render:screen, so that the original's own files are read as they are.
///
/// Today: panels and their layout, placed as a layout places them and following their parent as
/// it changes size; fonts, the TrueType faces the schemes name, found, sized and drawn as Windows
/// found, sized and drew them; and text on the screen, their glyphs packed into atlases and placed as Windows
/// placed them; panels drawn, their backgrounds and their children within them; labels, text
/// entries and rich text. The schemes' styles and the other controls (the console first) come next (TODO.md).
export module nx.ui;

export import :panel;
export import :painter;
export import :label;
export import :textEntry;
export import :richText;
export import :font;
export import :fontCatalog;
export import :schemeFont;
export import :text;
