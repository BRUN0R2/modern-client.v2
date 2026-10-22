// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.rhi;
import nx.render;

namespace cs {

auto readSceneShaders(const std::filesystem::path& folder) -> nx::Result<SceneShaders, SceneError> {
    SceneShaders shaders;
    for (const auto& [file, code] :
         {std::pair{"world.spv", &shaders.world}, std::pair{"studio.spv", &shaders.studio},
          std::pair{"sprite.spv", &shaders.sprite}}) {
        auto read = nx::platform::readFile(folder / file);
        if (!read) {
            return nx::fail(SceneError::ShadersUnreadable, std::move(read.error()));
        }
        *code = std::move(*read);
    }
    return shaders;
}

Scene::Scene(nx::render::BspWorld world, nx::render::WorldLight light, nx::render::SpriteRenderer sprites,
             std::vector<nx::render::SpriteModel> spriteModels, nx::render::StudioRenderer studio,
             std::vector<nx::render::StudioModel> models, nx::render::StudioRenderer viewModels,
             nx::render::StudioModel knife) noexcept
    : m_world{std::move(world)}, m_light{std::move(light)}, m_sprites{std::move(sprites)},
      m_spriteModels{std::move(spriteModels)}, m_studio{std::move(studio)}, m_models{std::move(models)},
      m_viewModels{std::move(viewModels)}, m_knife{std::move(knife)} {}

auto Scene::create(nx::rhi::Device& device, nx::rhi::CommandList& commands, const Level& level,
                   const SceneShaders& shaders, nx::rhi::Format format,
                   const nx::render::GammaSettings& gamma) -> nx::Result<Scene, SceneError> {
    auto world = nx::render::BspWorld::create(device, commands,
                                              {.map = level.map(),
                                               .wads = level.wads(),
                                               .shaderCode = shaders.world,
                                               .colorFormat = format,
                                               .gamma = gamma,
                                               .sky = level.sky()});
    if (!world) {
        return nx::fail(SceneError::CreateFailed, "the world", std::move(world.error()));
    }
    auto sprites = nx::render::SpriteRenderer::create(
        device, {.shaderCode = shaders.sprite, .colorFormat = format, .gamma = gamma});
    if (!sprites) {
        return nx::fail(SceneError::CreateFailed, "the sprites", std::move(sprites.error()));
    }
    auto spriteModels = level.sprites().load(*sprites, commands);
    if (!spriteModels) {
        return nx::fail(SceneError::CreateFailed, "the sprites", std::move(spriteModels.error()));
    }
    const nx::render::StudioRendererDesc studioDesc{
        .shaderCode = shaders.studio, .colorFormat = format, .gamma = gamma};
    auto studio = nx::render::StudioRenderer::create(device, commands, studioDesc);
    if (!studio) {
        return nx::fail(SceneError::CreateFailed, "the map's models", std::move(studio.error()));
    }
    auto viewModels = nx::render::StudioRenderer::create(device, commands, studioDesc);
    if (!viewModels) {
        return nx::fail(SceneError::CreateFailed, "the view's model", std::move(viewModels.error()));
    }
    auto models = level.models().load(*studio, commands);
    if (!models) {
        return nx::fail(SceneError::CreateFailed, "the map's models", std::move(models.error()));
    }
    auto knife = level.knife().load(*viewModels, commands);
    if (!knife) {
        return nx::fail(SceneError::CreateFailed, "the knife", std::move(knife.error()));
    }
    return Scene{std::move(*world),      nx::render::WorldLight{level.map()},
                 std::move(*sprites),    std::move(*spriteModels),
                 std::move(*studio),     std::move(*models),
                 std::move(*viewModels), std::move(*knife)};
}

auto Scene::draw(nx::rhi::CommandList& list, const Level& level, const SceneView& view)
    -> nx::Result<void, SceneError> {
    const nx::render::LightStyleValues styles = level.lightStyles().valuesAt(view.time);
    m_world.prepare({.camera = view.camera,
                     .aspect = view.aspect,
                     .time = view.time,
                     .lightStyles = styles,
                     .placements = view.placements});
    const PosedModels models = level.models().posed(m_models, view.time, m_light, level.skyLight(), styles);
    if (auto prepared =
            m_studio.prepare({.camera = view.camera, .aspect = view.aspect, .instances = models.instances});
        !prepared) {
        return nx::fail(SceneError::DrawFailed, "the map's models", std::move(prepared.error()));
    }
    std::vector<nx::render::SpriteInstance> sprites = level.sprites().instances(m_spriteModels, view.time);
    for (nx::render::SpriteInstance& sprite : sprites) {
        if (sprite.mode == nx::render::RenderMode::Glow) {
            sprite.isHidden = level.glowBlockers().hides(level.hulls(), view.placements, view.camera.position,
                                                         sprite.origin);
        }
    }
    if (auto prepared = m_sprites.prepare({.camera = view.camera,
                                           .aspect = view.aspect,
                                           .time = view.time,
                                           .lightStyles = styles,
                                           .instances = sprites},
                                          m_light);
        !prepared) {
        return nx::fail(SceneError::DrawFailed, "the sprites", std::move(prepared.error()));
    }
    nx::render::drawInOrder(list, m_world, m_studio, m_sprites);
    if (!view.sinceDeploy) {
        return {};
    }
    const std::vector<nx::Mat3x4> bones = level.knife().pose(view.camera, *view.sinceDeploy);
    const std::array knife{nx::render::StudioInstance{
        .model = m_knife,
        .bones = bones,
        .light = m_light.modelLightAt({.origin = view.camera.position, .sky = level.skyLight()}, styles),
        .depth = nx::render::StudioDepth::ViewModel}};
    if (auto drawn =
            m_viewModels.draw(list, {.camera = view.camera, .aspect = view.aspect, .instances = knife});
        !drawn) {
        return nx::fail(SceneError::DrawFailed, "the knife", std::move(drawn.error()));
    }
    return {};
}

} // namespace cs
