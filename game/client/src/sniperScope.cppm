// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The scope over the view of a weapon looked through its sights (docs/12-ui-hud-and-console.md, section 3):
/// the original's four quarters of a circle (sprites/scope_arc_nw.tga, _ne, _sw and scope_arc.tga, the
/// south-east's), as tall as the screen in its middle, black beside them, and its two lines across.
export module cs.client:sniperScope;

import std;
import nx.core;
import nx.rhi;
import nx.assets;
import nx.render;

export namespace cs {

class SniperScope {
public:
    /// Puts its quarters, read from `vfs`, on the GPU with `screen`, their uploads recorded in `commands`;
    /// one that cannot be read is logged, and the scope is drawn without the quarters.
    void load(const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands);
    [[nodiscard]] inline auto isLoaded() const noexcept -> bool { return isRead; }

    /// Appends to `quads` the scope over a view of `size`.
    void append(std::vector<nx::render::ScreenQuad>& quads, nx::rhi::Extent2d size) const;

private:
    bool isRead = false;
    /// North-west, north-east, south-west, south-east; none for one that could not be read.
    std::array<std::optional<nx::render::ScreenImage>, 4> quarters{};
};

} // namespace cs
