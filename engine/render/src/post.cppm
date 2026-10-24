// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The enhanced look's last passes (docs/adr/0025-enhanced-rendering.md): the scene, drawn to a
/// target of half floats that keeps light beyond 1, bloomed and mapped to the screen
/// (shaders/post.slang). The bloom is a chain of images of a half, a quarter... of the screen's size,
/// each reduced from the one before and enlarged back into it, added. The accumulated levels are
/// normalized before mixing so that constant colors keep their energy; the tone curve compresses
/// the brightest channel and scales every channel together, preserving chromaticity.
///
/// The scene and every intermediate image hold linear radiance: surfaces convert legacy colors
/// before blending, while enhanced materials write their radiance directly. Only the tone pass
/// encodes for presentation. The knee retains its display-domain meaning and the neutral curve's
/// brightness response; the resulting gain applies to the linear RGB together (ADR-0025).
///
/// The additive effects (a weapon's muzzle flashes) are drawn apart, to a layer of display colors,
/// which the tone pass adds over the scene once it is mapped to the screen, each channel stopping at
/// white, as the original adds them: an effect authored for that addition shows as it was made (an
/// overexposed flame turns yellow, then white, where a common gain would keep it orange). The layer's
/// own light goes to the bloom too, a glow about each effect as about any light.
export module nx.render:post;

import std;
import nx.core;
import nx.rhi;

export namespace nx::render {

/// Why the post passes could not be put on the GPU.
enum class PostError : std::uint8_t {
    GpuFailed, ///< The GPU refused a resource (cause: the RhiError).
};

/// The images of the bloom's chain, from half the screen's size, each half the one before.
inline constexpr std::size_t kBloomLevels = 5;
/// How much of the bloom the tone pass mixes into the scene.
inline constexpr float kBloomStrength = 0.04F;
/// Below this display-domain peak, the tone curve keeps the light as it is.
inline constexpr float kToneKnee = 0.8F;

struct PostRendererDesc {
    /// shaders/post.slang, compiled for the device (bin/shaders/post.spv).
    std::span<const std::byte> shaderCode{};
    rhi::Format outputFormat = rhi::Format::Undefined; ///< The screen's.
};

/// The scene's target, its bloom and its tone curve.
class PostRenderer {
public:
    /// Errors: GpuFailed.
    [[nodiscard]] static auto create(rhi::Device& device, const PostRendererDesc& desc)
        -> Result<PostRenderer, PostError>;

    PostRenderer(PostRenderer&& other) noexcept = default;
    auto operator=(PostRenderer&& other) -> PostRenderer& = delete;
    PostRenderer(const PostRenderer&) = delete;
    auto operator=(const PostRenderer&) -> PostRenderer& = delete;
    /// Its resources go with it, which the device keeps until the GPU is done with them.
    ~PostRenderer() = default;

    /// The format of the scene's linear-radiance target, which the scene's pipelines draw to.
    [[nodiscard]] static constexpr auto sceneFormat() noexcept -> rhi::Format {
        return rhi::Format::Rgba16Float;
    }
    /// The format of the effects' layer, which the effects' pipelines draw to: display colors, added
    /// beyond 1 until the tone pass stops each channel at white.
    [[nodiscard]] static constexpr auto effectsFormat() noexcept -> rhi::Format {
        return rhi::Format::Rgba16Float;
    }

    /// Makes the scene's target, the effects' layer and the bloom's images for a screen of `size`, unless
    /// they are of it already: the GPU must be done with the frames that used the old ones. Errors:
    /// GpuFailed.
    [[nodiscard]] auto resize(rhi::Extent2d size) -> Result<void, PostError>;
    /// The linear-radiance target the scene draws to, of the size given last, read in ShaderRead.
    [[nodiscard]] auto sceneTarget() const noexcept -> rhi::TextureHandle;
    /// The layer of display colors the additive effects draw to, cleared to black each frame, of the
    /// size given last, read in ShaderRead.
    [[nodiscard]] auto effectsTarget() const noexcept -> rhi::TextureHandle;

    /// Records the bloom, outside a render pass: the scene's target reduced into the chain, the effects'
    /// light added to its first image, and the chain enlarged back into it.
    void bloom(rhi::CommandList& commands) const;
    /// Records the scene with its bloom through the tone curve and the effects added over it, each
    /// channel stopping at white: a triangle covering the target, in the render pass under way, whose
    /// target is of PostRendererDesc::outputFormat.
    void drawToneMapped(rhi::CommandList& commands) const;

private:
    /// A pass of the chain: the image it draws to, and the bind group it reads its source by.
    struct BloomPass {
        rhi::TextureHandle target{};
        rhi::Extent2d size{};
        rhi::OwnedBindGroup group{};
    };

    /// What the tone pass reads besides the scene: the bloom, and the effects' layer.
    struct ToneSources {
        rhi::TextureHandle bloom{};
        rhi::TextureHandle effects{};
    };

    inline explicit PostRenderer(rhi::Device& device) noexcept : m_device{&device} {}

    /// The bind group reading `source`, one over `sourceSize` apart, through the uniform at `slot`,
    /// with `tone`'s too for the tone pass. Errors: those of the device.
    [[nodiscard]] auto passGroup(std::size_t slot, rhi::TextureHandle source, rhi::Extent2d sourceSize,
                                 std::optional<ToneSources> tone)
        -> Result<rhi::OwnedBindGroup, rhi::RhiError>;

    rhi::Device* m_device;
    rhi::OwnedShader m_shader;
    rhi::OwnedSampler m_sampler;
    rhi::OwnedBindGroupLayout m_bloomLayout; ///< A pass of the chain's: its uniform, source and sampler.
    rhi::OwnedBindGroupLayout m_toneLayout;  ///< The tone pass's: and the bloom.
    rhi::OwnedRenderPipeline m_reduceFirst;
    rhi::OwnedRenderPipeline m_reduceEffects; ///< Adds the effects' light to what the first holds.
    rhi::OwnedRenderPipeline m_reduce;
    rhi::OwnedRenderPipeline m_enlarge; ///< Adds to what its target holds.
    rhi::OwnedRenderPipeline m_tone;
    rhi::Format m_bloomFormat = rhi::Format::Undefined;
    rhi::OwnedBuffer m_uniforms; ///< Each pass's, at its slot.

    rhi::Extent2d m_size{};
    rhi::OwnedTexture m_scene;
    rhi::OwnedTexture m_effects;
    std::vector<rhi::OwnedTexture> m_levels;
    std::vector<BloomPass> m_reductions;   ///< Level by level, the first from the scene.
    BloomPass m_effectsReduction;          ///< The effects' light into the first level.
    std::vector<BloomPass> m_enlargements; ///< From the smallest level into the one before, up to the first.
    rhi::OwnedBindGroup m_toneGroup;
};

} // namespace nx::render
