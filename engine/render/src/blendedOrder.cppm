// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The order a frame draws in (docs/05-rendering.md, section 7.1), as the original's
/// R_DrawEntitiesOnList draws its entities: the world and the solid entities first, then every
/// blended one, brush entity, studio model or sprite alike, in one list from the farthest
/// (R_TransEntityCompare; Xash3D, GPL, consulted: docs/20-legal-and-licensing.md, section 3).
///
/// Each renderer is prepared for the frame first, then drawInOrder records them together: the solid
/// things of each in turn, then the blended things of all of them in their one order.
export module nx.render:blendedOrder;

import std;
import nx.rhi;
import :renderMode;

export namespace nx::render {

/// The distance of a blended thing drawn before every other: a brush entity drawn TransAlpha, as the
/// original sorts it.
inline constexpr float kFarthest = std::numeric_limits<float>::max();

/// Something a renderer draws blended this frame, ordered among everything drawn blended in it.
struct BlendedDraw {
    float distance = 0.0F; ///< Squared, from the camera to the middle of its bounds; or kFarthest.
    RenderMode mode = RenderMode::Normal;
    std::uint32_t index = 0; ///< Which, to the renderer that draws it.
};

/// The turn of a blended thing: the renderer that draws it, by its place among those drawn together,
/// and its index there.
struct BlendedTurn {
    std::size_t renderer = 0;
    std::uint32_t index = 0;

    [[nodiscard]] inline auto operator==(const BlendedTurn&) const noexcept -> bool = default;
};

/// Whether `first` is drawn before `second` (R_TransEntityCompare): the farther first; at one distance,
/// by mode, the others before TransTexture, TransAdd and Glow, in that order (R_RankForRenderMode).
[[nodiscard]] auto isDrawnBefore(const BlendedDraw& first, const BlendedDraw& second) noexcept -> bool;

/// The blended things of `renderers`, each the list of one renderer, in the order they are drawn:
/// isDrawnBefore's first; those at one distance and of one mode in the order of their renderers, then
/// in their own.
[[nodiscard]] auto blendedTurnsOf(std::span<const std::span<const BlendedDraw>> renderers)
    -> std::vector<BlendedTurn>;

/// A renderer prepared for a frame, which records its solid things at once and each of its blended
/// things when its turn comes.
template <class Renderer>
concept OrderedRenderer =
    requires(const Renderer& renderer, rhi::CommandList& commands, std::uint32_t index) {
        { renderer.blendedDraws() } -> std::convertible_to<std::span<const BlendedDraw>>;
        renderer.drawSolid(commands);
        renderer.drawBlended(commands, index);
    };

/// Records `renderers`, each prepared for this frame, inside its render pass: the solid things of each
/// in the order given, then the blended things of all of them in the order of blendedTurnsOf.
template <OrderedRenderer... Renderers>
inline void drawInOrder(rhi::CommandList& commands, const Renderers&... renderers) {
    (renderers.drawSolid(commands), ...);
    const std::array<std::span<const BlendedDraw>, sizeof...(Renderers)> blended{renderers.blendedDraws()...};
    const std::vector<BlendedTurn> turns = blendedTurnsOf(blended);
    // By index: GCC 16 mangles an iterator comparison instantiated from a module interface so that it
    // conflicts with the same comparison in an importer.
    for (std::size_t turn = 0; turn < turns.size(); ++turn) {
        std::size_t place = 0;
        ((place++ == turns[turn].renderer ? renderers.drawBlended(commands, turns[turn].index) : void()),
         ...);
    }
}

} // namespace nx::render
