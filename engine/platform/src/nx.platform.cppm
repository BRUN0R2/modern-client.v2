// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.platform — the operating system as the engine sees it (docs/01-architecture.md, layer 2).
///
/// Today: real-time clocks and the precise wait that paces frames and ticks, the native file
/// system under the VFS, where Steam lives, the window with its input and the sound output (SDL3).
/// OS threads (names, affinity, priority) come with their first consumer.
export module nx.platform;

export import :audio;
export import :clock;
export import :fileSystem;
export import :steam;
export import :window;
