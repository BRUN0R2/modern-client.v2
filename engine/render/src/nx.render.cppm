// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.render — what the engine draws, over nx.rhi (docs/05-rendering.md): no GPU API crosses it.
///
/// Today: the camera and its matrices, the original's picture (its gamma curves and light styles) and
/// the BSP world.
/// The render graph, studio models, sprites, effects and the HUD come with their consumers.
export module nx.render;

export import :camera;
export import :gamma;
export import :lightStyle;
export import :world;
