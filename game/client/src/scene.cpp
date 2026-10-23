// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.platform;
import nx.assets;
import nx.rhi;
import nx.render;

namespace cs {

namespace {

/// A muzzle flash is added in full (R_MuzzleFlash's renderamt).
constexpr std::uint8_t kFullAmount = 255;
/// The stream of random numbers the muzzle flashes draw from.
constexpr std::string_view kFlashStream = "muzzleFlash";
/// The most decals on the faces of the world and of its brush entities: the original's r_decals, which
/// a multiplayer game holds to mp_decals' default, until the cvars come.
constexpr std::size_t kMultiplayerDecals = 300;

} // namespace

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
             std::vector<nx::render::StudioModel> weapons,
             std::vector<std::optional<nx::render::SpriteModel>> flashModels) noexcept
    : m_world{std::move(world)}, m_light{std::move(light)}, m_sprites{std::move(sprites)},
      m_spriteModels{std::move(spriteModels)}, m_studio{std::move(studio)}, m_models{std::move(models)},
      m_viewModels{std::move(viewModels)}, m_weapons{std::move(weapons)},
      m_flashModels{std::move(flashModels)} {}

auto Scene::create(nx::rhi::Device& device, nx::rhi::CommandList& commands, const Level& level,
                   const SceneShaders& shaders, nx::rhi::Format format,
                   const nx::render::GammaSettings& gamma) -> nx::Result<Scene, SceneError> {
    auto world = nx::render::BspWorld::create(device, commands,
                                              {.map = level.map(),
                                               .wads = level.wads(),
                                               .shaderCode = shaders.world,
                                               .colorFormat = format,
                                               .gamma = gamma,
                                               .sky = level.sky(),
                                               .decals = level.decals().images(),
                                               .decalLimit = kMultiplayerDecals});
    if (!world) {
        return nx::fail(SceneError::CreateFailed, "the world", std::move(world.error()));
    }
    // The decals the map stamps for good as its game starts, as the original's client stamps the static
    // decals the server sends as it connects.
    for (const MapDecal& decal : level.mapDecals()) {
        std::ignore =
            world->shootDecal(decal.decal, decal.origin, decal.on, nx::render::DecalLife::Permanent);
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
    std::vector<nx::render::StudioModel> weapons;
    for (const ViewModel& viewModel : level.viewModels()) {
        auto loaded = viewModel.load(*viewModels, commands);
        if (!loaded) {
            return nx::fail(SceneError::CreateFailed, "a weapon's view model", std::move(loaded.error()));
        }
        weapons.push_back(std::move(*loaded));
    }
    auto flashModels = level.muzzleFlashes().load(*sprites, commands);
    if (!flashModels) {
        return nx::fail(SceneError::CreateFailed, "the muzzle flashes", std::move(flashModels.error()));
    }
    return Scene{std::move(*world),      nx::render::WorldLight{level.map()},
                 std::move(*sprites),    std::move(*spriteModels),
                 std::move(*studio),     std::move(*models),
                 std::move(*viewModels), std::move(weapons),
                 std::move(*flashModels)};
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
    // The view model's bones first: its client events show what the sprites draw.
    std::vector<nx::Mat3x4> bones;
    if (view.viewModel) {
        nx::expects(view.viewModel->weapon.value < m_weapons.size(), "Scene::draw: a weapon of the level's");
        bones = level.viewModels()[view.viewModel->weapon.value].pose(view.camera, view.viewModel->sequence,
                                                                      view.viewModel->intoIt);
    }
    flash(level, view.viewModel, view.clientEvents, bones, view.time);
    std::vector<nx::render::SpriteInstance> sprites = level.sprites().instances(m_spriteModels, view.time);
    for (nx::render::SpriteInstance& sprite : sprites) {
        if (sprite.mode == nx::render::RenderMode::Glow) {
            sprite.isHidden = level.glowBlockers().hides(level.hulls(), view.placements, view.camera.position,
                                                         sprite.origin);
        }
    }
    for (const MuzzleFlash& shown : m_flashes) {
        if (const std::optional<nx::render::SpriteModel>& model = m_flashModels[shown.sprite]) {
            sprites.push_back(nx::render::SpriteInstance{.model = *model,
                                                         .origin = shown.origin,
                                                         .angles = {.roll = shown.roll},
                                                         .frame = shown.frame,
                                                         .scale = shown.scale,
                                                         .mode = nx::render::RenderMode::TransAdd,
                                                         .amount = kFullAmount});
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
    if (!view.viewModel) {
        return {};
    }
    const std::uint32_t weapon = view.viewModel->weapon.value;
    const std::array held{nx::render::StudioInstance{
        .model = m_weapons[weapon],
        .bones = bones,
        .light = m_light.modelLightAt({.origin = view.camera.position, .sky = level.skyLight()}, styles),
        .depth = nx::render::StudioDepth::ViewModel,
        .isMirrored = level.viewModels()[weapon].isMirrored()}};
    if (auto drawn =
            m_viewModels.draw(list, {.camera = view.camera, .aspect = view.aspect, .instances = held});
        !drawn) {
        return nx::fail(SceneError::DrawFailed, "the weapon in hand", std::move(drawn.error()));
    }
    return {};
}

auto Scene::shootDecal(std::uint32_t decal, nx::Vec3 position, const nx::render::DecalTarget& on)
    -> std::uint32_t {
    return m_world.shootDecal(decal, position, on);
}

void Scene::flash(const Level& level, const std::optional<ViewModelPose>& pose,
                  std::span<const nx::assets::StudioEvent* const> events, std::span<const nx::Mat3x4> bones,
                  nx::Seconds time) {
    std::erase_if(m_flashes, [time](const MuzzleFlash& shown) { return shown.until < time; });
    if (!pose) {
        return;
    }
    const ViewModel& viewModel = level.viewModels()[pose->weapon.value];
    std::array<std::size_t, MuzzleFlashSprites::kCount> frameCounts{};
    for (std::size_t index = 0; index < frameCounts.size(); ++index) {
        frameCounts[index] = m_flashModels[index] ? m_flashModels[index]->frameCount() : 0;
    }
    for (const nx::assets::StudioEvent* const event : events) {
        const std::optional<std::uint32_t> attachment = muzzleFlashAttachmentOf(event->event);
        const std::optional<nx::Vec3> origin =
            attachment ? viewModel.attachmentAt(bones, *attachment) : std::nullopt;
        if (origin) {
            nx::Random random{m_flashCount++, kFlashStream};
            m_flashes.push_back(muzzleFlashOf(event->options, *origin, time, frameCounts, random));
        }
    }
}

} // namespace cs
