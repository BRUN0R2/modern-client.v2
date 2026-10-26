// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.platform;
import nx.rhi;
import nx.render;

namespace cs {

namespace {

/// What no face covers: the void outside a map, seen through its leaks, and the screen without one.
constexpr nx::rhi::ClearColor kVoidColor{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 1.0F};
/// The effects' layer before the effects are drawn: nothing to add.
constexpr nx::rhi::ClearColor kNoEffect{.red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = 0.0F};
/// The depth a frame starts with: the reversed depth's farthest (nx.render:camera).
constexpr float kFarthestDepth = 0.0F;

/// `scene`'s view at the aspect of a frame of `extent`.
[[nodiscard]] auto viewIn(const FrameScene& scene, nx::rhi::Extent2d extent) -> SceneView {
    SceneView view = scene.view;
    view.aspect = static_cast<float>(extent.width) / static_cast<float>(extent.height);
    return view;
}

} // namespace

auto readFrameShaders(const std::filesystem::path& folder) -> nx::Result<FrameShaders, FrameError> {
    FrameShaders shaders;
    for (const auto& [code, file] :
         {std::pair{&shaders.screen, "screen.spv"}, std::pair{&shaders.post, "post.spv"}}) {
        auto read = nx::platform::readFile(folder / file);
        if (!read) {
            return nx::fail(FrameError::ShadersUnreadable, std::move(read.error()));
        }
        *code = std::move(*read);
    }
    return shaders;
}

FrameRenderer::FrameRenderer(nx::rhi::Device& device, FrameShaders shaders, nx::rhi::Format outputFormat,
                             nx::render::ScreenRenderer screen) noexcept
    : m_device{&device}, m_shaders{std::move(shaders)}, m_outputFormat{outputFormat},
      m_screen{std::move(screen)} {}

auto FrameRenderer::create(nx::rhi::Device& device, FrameShaders shaders, nx::rhi::Format outputFormat,
                           const nx::render::GammaSettings& gamma) -> nx::Result<FrameRenderer, FrameError> {
    auto screen = nx::render::ScreenRenderer::create(device, {.shaderCode = shaders.screen,
                                                              .colorFormat = outputFormat,
                                                              .depthFormat = kDepthFormat,
                                                              .gamma = gamma});
    if (!screen) {
        return nx::fail(FrameError::CreateFailed, "the screen", std::move(screen.error()));
    }
    return FrameRenderer{device, std::move(shaders), outputFormat, std::move(*screen)};
}

auto FrameRenderer::sceneFormatOf(nx::render::StudioLook look) const noexcept -> nx::rhi::Format {
    return look == nx::render::StudioLook::Enhanced ? nx::render::PostRenderer::sceneFormat()
                                                    : m_outputFormat;
}

auto FrameRenderer::effectsFormatOf(nx::render::StudioLook look) const noexcept -> nx::rhi::Format {
    return look == nx::render::StudioLook::Enhanced ? nx::render::PostRenderer::effectsFormat()
                                                    : m_outputFormat;
}

auto FrameRenderer::makePost() -> nx::Result<void, FrameError> {
    if (m_post) {
        return {};
    }
    auto post = nx::render::PostRenderer::create(
        *m_device,
        {.shaderCode = m_shaders.post, .outputFormat = m_outputFormat, .depthFormat = kDepthFormat});
    if (!post) {
        return nx::fail(FrameError::CreateFailed, "the post renderer", std::move(post.error()));
    }
    m_post.emplace(std::move(*post));
    m_postExtent = {};
    return {};
}

auto FrameRenderer::fitTargets(nx::rhi::Extent2d extent) -> nx::Result<void, FrameError> {
    if (!m_depth || m_depthExtent != extent) {
        m_device->waitIdle(); // the frames in flight may still test the old depth
        auto depth = m_device->createTexture({.label = "frame.depth",
                                              .format = kDepthFormat,
                                              .extent = extent,
                                              .usage = {nx::rhi::TextureUsage::DepthTarget}});
        if (!depth) {
            return nx::fail(FrameError::CreateFailed, "the depth", std::move(depth.error()));
        }
        m_depth = std::move(*depth); // the old one, if any, goes
        m_depthExtent = extent;
    }
    if (m_post && m_postExtent != extent) {
        m_device->waitIdle(); // the frames in flight may still read the old targets
        if (auto resized = m_post->resize(extent); !resized) {
            return nx::fail(FrameError::CreateFailed, "the post renderer's targets",
                            std::move(resized.error()));
        }
        m_postExtent = extent;
    }
    return {};
}

auto FrameRenderer::record(nx::rhi::CommandList& list, nx::rhi::TextureHandle target,
                           nx::rhi::Extent2d extent, const std::optional<FrameScene>& scene,
                           std::span<const nx::render::ScreenQuad> quads) -> nx::Result<void, FrameError> {
    const bool isEnhanced = scene && scene->scene.look() == nx::render::StudioLook::Enhanced;
    if (isEnhanced) {
        if (auto made = makePost(); !made) {
            return made;
        }
    }
    if (auto fitted = fitTargets(extent); !fitted) {
        return fitted;
    }
    const nx::rhi::TextureHandle depth = m_depth->get();
    list.transition(depth, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::DepthTarget);
    if (isEnhanced) {
        if (auto drawn = recordEnhancedScene(list, extent, *scene); !drawn) {
            return drawn;
        }
        // The scene through the tone curve to the target, the screen's pictures over it, in one pass that
        // has the frame's depth, as both their pipelines declare it: neither tests nor writes it.
        list.transition(target, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
        const std::array colors{
            nx::rhi::ColorAttachment{.texture = target, .load = nx::rhi::LoadOp::DontCare}};
        list.beginRendering(
            {.colors = colors,
             .depth = nx::rhi::DepthAttachment{.texture = depth, .load = nx::rhi::LoadOp::DontCare},
             .area = extent});
        m_post->drawToneMapped(list);
    } else {
        list.transition(target, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
        const std::array colors{nx::rhi::ColorAttachment{.texture = target, .clear = kVoidColor}};
        list.beginRendering({.colors = colors,
                             .depth = nx::rhi::DepthAttachment{.texture = depth, .clear = kFarthestDepth},
                             .area = extent});
        if (scene) {
            if (auto drawn = scene->scene.draw(list, scene->level, viewIn(*scene, extent)); !drawn) {
                return nx::fail(FrameError::DrawFailed, std::move(drawn.error()));
            }
            scene->scene.drawEffects(list); // over the scene, in the screen's colors
        }
    }
    if (!quads.empty()) {
        if (auto drawn = m_screen.draw(list, {.size = extent, .quads = quads}); !drawn) {
            return nx::fail(FrameError::DrawFailed, std::move(drawn.error()));
        }
    }
    list.endRendering();
    return {};
}

auto FrameRenderer::recordEnhancedScene(nx::rhi::CommandList& list, nx::rhi::Extent2d extent,
                                        const FrameScene& scene) -> nx::Result<void, FrameError> {
    // A reflection probe baked a frame, before the scene draws: the world prepares its views first.
    scene.scene.bakeReflections(list, scene.view);
    const nx::rhi::TextureHandle sceneTarget = m_post->sceneTarget();
    const nx::rhi::TextureHandle depth = m_depth->get();
    list.transition(sceneTarget, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
    const std::array colors{nx::rhi::ColorAttachment{.texture = sceneTarget, .clear = kVoidColor}};
    list.beginRendering({.colors = colors,
                         .depth = nx::rhi::DepthAttachment{.texture = depth,
                                                           .store = nx::rhi::StoreOp::Store,
                                                           .clear = kFarthestDepth},
                         .area = extent});
    if (auto drawn = scene.scene.draw(list, scene.level, viewIn(scene, extent)); !drawn) {
        return nx::fail(FrameError::DrawFailed, std::move(drawn.error()));
    }
    list.endRendering();
    // The effects to their layer, display colors over the scene's depths, which the tone pass adds as the
    // original adds them.
    const nx::rhi::TextureHandle effects = m_post->effectsTarget();
    list.transition(effects, nx::rhi::TextureState::Undefined, nx::rhi::TextureState::RenderTarget);
    list.transition(depth, nx::rhi::TextureState::DepthTarget, nx::rhi::TextureState::DepthTarget);
    const std::array effectsColors{nx::rhi::ColorAttachment{.texture = effects, .clear = kNoEffect}};
    list.beginRendering({.colors = effectsColors,
                         .depth = nx::rhi::DepthAttachment{.texture = depth, .load = nx::rhi::LoadOp::Load},
                         .area = extent});
    scene.scene.drawEffects(list);
    list.endRendering();
    list.transition(sceneTarget, nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::ShaderRead);
    list.transition(effects, nx::rhi::TextureState::RenderTarget, nx::rhi::TextureState::ShaderRead);
    m_post->bloom(list);
    return {};
}

} // namespace cs
