// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.assets — content as the engine loads it (docs/06-assets-and-resources.md, layer 3).
///
/// Today: the VFS over mounted directories. Content sources and profiles, the GoldSrc readers,
/// asset handles and the loading pipeline come next, in the order of TODO.md (M2).
export module nx.assets;

export import :vfs;
