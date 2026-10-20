// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.assets — content as the engine loads it (docs/06-assets-and-resources.md, layer 3).
///
/// Today: the VFS over mounted directories, the Counter-Strike 1.6 installation as a content
/// source, detected in the Steam libraries, the WAD3 and WAV readers, and the geometry of BSP v30
/// maps. Profiles, the rest of the BSP and the other GoldSrc readers, asset handles and the loading
/// pipeline come next, in the order of TODO.md (M2).
export module nx.assets;

export import :vfs;
export import :content;
export import :steam;
export import :wad;
export import :wav;
export import :bsp;
