// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Studio models posed for a frame (docs/05-rendering.md, section 9): what the map's models and the other
/// players give the scene to draw.
export module cs.client:posedModels;

import std;
import nx.math;
import nx.render;

export namespace cs {

/// Studio models posed for a frame: their bones, which their instances point into.
struct PosedModels {
    std::vector<std::vector<nx::Mat3x4>> bones{};
    std::vector<nx::render::StudioInstance> instances{};
};

} // namespace cs
