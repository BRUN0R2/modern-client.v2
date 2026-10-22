// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.ui — the interface's toolkit (docs/12-ui-hud-and-console.md, section 2; ADR-0020): retained
/// panels in VGUI2's model, laid out by VGUI2's layouts (.res), styled by its schemes, and drawn by
/// nx.render:screen, so that the original's own files are read as they are.
///
/// Today: panels and their layout, placed as a layout places them and following their parent as
/// it changes size; and fonts, the TrueType faces the schemes name, sized and drawn as Windows sized
/// and drew them. The schemes' styles, text on the screen, input and the controls (the console
/// first) come next (TODO.md).
export module nx.ui;

export import :panel;
export import :font;
