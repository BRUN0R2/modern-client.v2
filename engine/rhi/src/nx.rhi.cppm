// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.rhi — the graphics hardware interface (docs/05-rendering.md, section 3; ADR-0004): explicit,
/// but only what DirectX 11 can also express. No GPU API's header crosses it; the backends
/// (nx.rhi.vulkan) implement it.
///
/// Today: the device, its frames and swapchain, generational texture handles, transitions and
/// render passes that clear. Buffers, textures of the renderer's own, bind groups and pipelines
/// come with the first shaders.
export module nx.rhi;

export import :types;
export import :handles;
export import :device;
