// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.platform — the operating system as the engine sees it (docs/01-arquitetura.md, layer 2).
///
/// Today: real-time clocks and the precise wait that paces frames and ticks. The window, events,
/// input and raw mouse (SDL3) come with the swapchain of the Vulkan backend (M2), their first
/// consumer; OS threads (names, affinity, priority) with theirs. The decision is in TODO.md.
export module nx.platform;

export import :clock;
