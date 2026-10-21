// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.rhi — the graphics hardware interface (docs/05-rendering.md, section 3; ADR-0004): explicit,
/// but only what DirectX 11 can also express. No GPU API's header crosses it; the backends
/// (nx.rhi.vulkan) implement it.
///
/// Today: the device, its frames and swapchain, generational handles of buffers, textures,
/// samplers, shaders, bind groups and render pipelines, transitions, copies, render passes and
/// draws. Queries, compute pipelines and dynamic uniform offsets come next (TODO.md).
export module nx.rhi;

export import :types;
export import :handles;
export import :device;
