// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.assets — content as the engine loads it (docs/06-assets-and-resources.md, layer 3).
///
/// Today: the VFS over mounted directories, the Counter-Strike 1.6 installation as a content
/// source, detected in the Steam libraries, the WAD3 (textures, pictures, console fonts), WAV,
/// BSP v30, SPR v2, MDL v10 and TGA readers, the HUD's sprite lists, VGUI2's KeyValues (its
/// schemes, layouts and localized strings) and its schemes, typed. Profiles, asset handles and the
/// loading pipeline come next, in the order of TODO.md (M2).
export module nx.assets;

export import :binary; // nothing exported: what the readers share
export import :vfs;
export import :content;
export import :steam;
export import :wad;
export import :wav;
export import :bsp;
export import :spr;
export import :spriteList;
export import :keyValues;
export import :scheme;
export import :localization;
export import :mdl;
export import :tga;
export import :bmp;
