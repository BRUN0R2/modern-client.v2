// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.render — what the engine draws, over nx.rhi (docs/05-rendering.md): no GPU API crosses it.
///
/// Today: the camera and its matrices, the fog, the original's picture (its gamma curves and light
/// styles), the BSP world and its decals, the light it gives where things are, studio models, sprites and
/// particles, the order a frame draws them in, what the tools draw over them (a navigation mesh and the
/// agents' ways), the screen over them (the pictures and text of the HUD and the console), and the enhanced
/// look's reflection probes, bloom and tone curve. The render graph and effects come with their consumers.
export module nx.render;

export import :camera;
export import :fog;
export import :gamma;
export import :lightStyle;
export import :renderMode;
export import :blendedOrder;
export import :surface;
export import :upload;
export import :light;
export import :decals;
export import :world;
export import :studio;
export import :sprite;
export import :particle;
export import :overlay;
export import :screen;
export import :post;
export import :probes;
