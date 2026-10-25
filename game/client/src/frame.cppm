// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a frame of the client is drawn, the same in next and in nxview (docs/05-rendering.md, section
/// 7.1): the scene of the level played in the look it was made in, then the screen's pictures (the HUD,
/// the console's window) over it, in one order of passes and their targets.
export module cs.client:frame;

import std;
import nx.core;
import nx.rhi;
import nx.render;
import :level;
import :scene;

export namespace cs {

/// Why what draws frames could not be made, or a frame drawn.
enum class FrameError : std::uint8_t {
    ShadersUnreadable, ///< A shader of the frame could not be read (cause: the FileError).
    CreateFailed, ///< The GPU refused a renderer or a target (cause: the renderer's or the device's error).
    DrawFailed,   ///< The GPU refused what a frame needed (cause: the renderer's error).
};

/// The shaders frames are drawn with besides the scene's: SPIR-V, as the build compiles them.
struct FrameShaders {
    std::vector<std::byte> screen; ///< shaders/screen.slang's (bin/shaders/screen.spv).
    std::vector<std::byte> post;   ///< shaders/post.slang's, the enhanced look's.
};

/// The shaders of frames in `folder`: screen.spv and post.spv. Errors: ShadersUnreadable.
[[nodiscard]] auto readFrameShaders(const std::filesystem::path& folder)
    -> nx::Result<FrameShaders, FrameError>;

/// The scene a frame shows: `scene`, of `level`, the level it was made of, as `view` sees it, at the
/// aspect of the frame's target.
struct FrameScene {
    Scene& scene;
    const Level& level;
    SceneView view{};
};

/// What draws the client's frames into targets of one format: the screen's renderer, the depth the
/// passes share, and the enhanced look's post renderer, made with the first frame of a scene of that
/// look.
class FrameRenderer {
public:
    /// The depth a frame's passes test and share.
    static constexpr nx::rhi::Format kDepthFormat = nx::rhi::Format::Depth32Float;

    /// A renderer of frames for targets of `outputFormat`, drawn with `shaders`, the screen's pictures
    /// in the original's picture `gamma`. Errors: CreateFailed.
    [[nodiscard]] static auto create(nx::rhi::Device& device, FrameShaders shaders,
                                     nx::rhi::Format outputFormat, const nx::render::GammaSettings& gamma)
        -> nx::Result<FrameRenderer, FrameError>;

    /// The screen's renderer, which the HUD's pictures and the console's atlas are put on the GPU with.
    [[nodiscard]] inline auto screen() noexcept -> nx::render::ScreenRenderer& { return m_screen; }

    /// The format a scene made in `look` draws to (Scene::create's `format`): the targets' in the legacy
    /// look, the post renderer's scene target's in the enhanced one.
    [[nodiscard]] auto sceneFormatOf(nx::render::StudioLook look) const noexcept -> nx::rhi::Format;
    /// The format a scene made in `look` draws its effects to (Scene::create's `effectsFormat`).
    [[nodiscard]] auto effectsFormatOf(nx::render::StudioLook look) const noexcept -> nx::rhi::Format;

    /// Records a frame into `target`, of `extent`, from its Undefined state: the void, or `scene` in the
    /// look its scene was made in; then `quads` over it. In the legacy look the scene, its effects added
    /// over it, to `target`; in the enhanced look the next of its reflection probes baked, the scene to
    /// the post renderer's target, its effects to its layer, the bloom, and the tone curve to `target`.
    /// The screen's pictures are drawn in the target's pass, which has the frame's depth. `target` is
    /// left in RenderTarget, for the caller to present or copy. When the extent changes, the GPU is
    /// waited for: the frames in flight may still read the old targets. Errors: CreateFailed (a target of
    /// the extent, or the post renderer), DrawFailed.
    [[nodiscard]] auto record(nx::rhi::CommandList& list, nx::rhi::TextureHandle target,
                              nx::rhi::Extent2d extent, const std::optional<FrameScene>& scene,
                              std::span<const nx::render::ScreenQuad> quads) -> nx::Result<void, FrameError>;

private:
    FrameRenderer(nx::rhi::Device& device, FrameShaders shaders, nx::rhi::Format outputFormat,
                  nx::render::ScreenRenderer screen) noexcept;

    /// The post renderer, made the first time. Errors: CreateFailed.
    [[nodiscard]] auto makePost() -> nx::Result<void, FrameError>;
    /// The depth target, and the post renderer's targets, of `extent`. Errors: CreateFailed.
    [[nodiscard]] auto fitTargets(nx::rhi::Extent2d extent) -> nx::Result<void, FrameError>;
    /// Records `scene` in the enhanced look into the post renderer's targets, and their bloom.
    /// Errors: DrawFailed.
    [[nodiscard]] auto recordEnhancedScene(nx::rhi::CommandList& list, nx::rhi::Extent2d extent,
                                           const FrameScene& scene) -> nx::Result<void, FrameError>;

    nx::rhi::Device* m_device;
    FrameShaders m_shaders;
    nx::rhi::Format m_outputFormat;
    nx::render::ScreenRenderer m_screen;
    std::optional<nx::render::PostRenderer> m_post; ///< Made with the first enhanced scene.
    std::optional<nx::rhi::OwnedTexture> m_depth;
    nx::rhi::Extent2d m_depthExtent{};
    nx::rhi::Extent2d m_postExtent{}; ///< The extent m_post's targets were made for.
};

} // namespace cs
