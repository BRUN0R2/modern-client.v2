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

/// The enhanced look's key light of the weapon in hand: where it is from the eye (above, to the left
/// and behind), how far it reaches, and its light, warm white, in linear light.
constexpr float kKeyLightAbove = 48.0F;
constexpr float kKeyLightLeft = 40.0F;
constexpr float kKeyLightBehind = 24.0F;
constexpr float kKeyLightReach = 256.0F;
constexpr nx::Vec3 kKeyLightColor{.x = 4200.0F, .y = 3900.0F, .z = 3500.0F};

/// The key light of the weapon in hand of a player whose eye and view are `camera`'s.
[[nodiscard]] auto viewModelLightOf(const nx::render::Camera& camera) -> nx::render::StudioLight {
    const nx::AngleVectors axes = nx::angleVectors(camera.angles);
    return nx::render::StudioLight{.position = camera.position + (axes.up * kKeyLightAbove) -
                                               (axes.right * kKeyLightLeft) -
                                               (axes.forward * kKeyLightBehind),
                                   .reach = kKeyLightReach,
                                   .color = kKeyLightColor};
}
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
          std::pair{"sprite.spv", &shaders.sprite}, std::pair{"probe.spv", &shaders.probe}}) {
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
             std::vector<std::optional<nx::render::StudioModel>> weapons,
             std::vector<std::optional<nx::render::SpriteModel>> flashModels) noexcept
    : m_world{std::move(world)}, m_light{std::move(light)}, m_sprites{std::move(sprites)},
      m_spriteModels{std::move(spriteModels)}, m_studio{std::move(studio)}, m_models{std::move(models)},
      m_viewModels{std::move(viewModels)}, m_weapons{std::move(weapons)},
      m_flashModels{std::move(flashModels)} {}

auto Scene::create(nx::rhi::Device& device, nx::rhi::CommandList& commands, const Level& level,
                   const SceneShaders& shaders, nx::rhi::Format format,
                   const nx::render::GammaSettings& gamma, nx::render::StudioLook look)
    -> nx::Result<Scene, SceneError> {
    // The enhanced look draws linear light, to a target of half floats its post pass encodes (ADR-0025),
    // and bakes the map's reflection probes, a view of their sides a frame besides the player's.
    const bool isEnhanced = look == nx::render::StudioLook::Enhanced;
    const nx::render::ColorEncoding encoding =
        isEnhanced ? nx::render::ColorEncoding::Linear : nx::render::ColorEncoding::Display;
    auto world =
        nx::render::BspWorld::create(device, commands,
                                     {.map = level.map(),
                                      .wads = level.wads(),
                                      .shaderCode = shaders.world,
                                      .colorFormat = format,
                                      .gamma = gamma,
                                      .sky = level.sky(),
                                      .decals = level.decals().images(),
                                      .decalLimit = kMultiplayerDecals,
                                      .viewsPerFrame = isEnhanced ? nx::render::kBakingViewsPerFrame : 1U,
                                      .encoding = encoding});
    if (!world) {
        return nx::fail(SceneError::CreateFailed, "the world", std::move(world.error()));
    }
    auto sprites = nx::render::SpriteRenderer::create(
        device, {.shaderCode = shaders.sprite, .colorFormat = format, .gamma = gamma, .encoding = encoding});
    if (!sprites) {
        return nx::fail(SceneError::CreateFailed, "the sprites", std::move(sprites.error()));
    }
    auto spriteModels = level.sprites().load(*sprites, commands);
    if (!spriteModels) {
        return nx::fail(SceneError::CreateFailed, "the sprites", std::move(spriteModels.error()));
    }
    const nx::render::StudioRendererDesc studioDesc{.shaderCode = shaders.studio,
                                                    .colorFormat = format,
                                                    .gamma = gamma,
                                                    .look = look,
                                                    .encoding = encoding};
    auto studio = nx::render::StudioRenderer::create(device, commands, studioDesc);
    if (!studio) {
        return nx::fail(SceneError::CreateFailed, "the map's models", std::move(studio.error()));
    }
    auto viewModels = nx::render::StudioRenderer::create(device, commands, studioDesc);
    if (!viewModels) {
        return nx::fail(SceneError::CreateFailed, "the view's model", std::move(viewModels.error()));
    }
    // The enhanced look's metals reflect the map's sky.
    if (isEnhanced) {
        for (nx::render::StudioRenderer* const renderer : {&*studio, &*viewModels}) {
            if (auto set = renderer->setEnvironment(commands, level.sky()); !set) {
                return nx::fail(SceneError::CreateFailed, "the sky models reflect", std::move(set.error()));
            }
        }
    }
    auto models = level.models().load(*studio, commands);
    if (!models) {
        return nx::fail(SceneError::CreateFailed, "the map's models", std::move(models.error()));
    }
    std::vector<std::optional<nx::render::StudioModel>> weapons;
    auto flashModels = level.muzzleFlashes().load(*sprites, commands);
    if (!flashModels) {
        return nx::fail(SceneError::CreateFailed, "the muzzle flashes", std::move(flashModels.error()));
    }
    Scene scene{std::move(*world),      nx::render::WorldLight{level.map()},
                std::move(*sprites),    std::move(*spriteModels),
                std::move(*studio),     std::move(*models),
                std::move(*viewModels), std::move(weapons),
                std::move(*flashModels)};
    const std::vector<WeaponIndex> all =
        std::views::iota(std::uint32_t{0}, nx::narrow<std::uint32_t>(level.weaponCount())) |
        std::views::transform([](std::uint32_t index) { return WeaponIndex{index}; }) |
        std::ranges::to<std::vector>();
    if (auto loaded = scene.loadWeapons(commands, level, all); !loaded) {
        return std::unexpected{std::move(loaded.error())};
    }
    // The enhanced look's reflection probes, baked as the map is played; the models reflect them.
    if (isEnhanced) {
        auto probes = nx::render::ReflectionProbes::create(
            device, commands,
            {.shaderCode = shaders.probe, .captureFormat = format, .positions = level.probePositions()});
        if (!probes) {
            return nx::fail(SceneError::CreateFailed, "the reflection probes", std::move(probes.error()));
        }
        scene.m_probes.emplace(std::move(*probes));
        for (nx::render::StudioRenderer* const renderer : {&scene.m_studio, &scene.m_viewModels}) {
            renderer->setProbes(scene.m_probes->texture(), scene.m_probes->mipLevels());
        }
    }
    return scene;
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
    // The view model's bones first: its client events show what the sprites draw. A weapon the level
    // has no view model of is not seen in hand.
    const ViewModel* const viewModel = view.viewModel ? level.viewModel(view.viewModel->weapon) : nullptr;
    const nx::render::StudioModel* const weaponModel =
        view.viewModel && view.viewModel->weapon.value < m_weapons.size() &&
                m_weapons[view.viewModel->weapon.value]
            ? &*m_weapons[view.viewModel->weapon.value]
            : nullptr;
    std::vector<nx::Mat3x4> bones;
    if (viewModel != nullptr && weaponModel != nullptr) {
        bones = viewModel->pose(view.camera, view.viewModel->sequence, view.viewModel->intoIt);
    }
    flash(viewModel, view.clientEvents, bones, view.time);
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
    if (bones.empty()) {
        return {};
    }
    // The weapon in hand is lit as modern shooters light theirs, readable whatever the map: a key light
    // above it and behind the eye, carried with the view, first; then the map's lights nearest it.
    std::vector<nx::render::StudioLight> lights{viewModelLightOf(view.camera)};
    std::ranges::copy(nearestLights(level.lights(), view.camera.position, nx::render::kMostStudioLights - 1),
                      std::back_inserter(lights));
    const std::array held{nx::render::StudioInstance{
        .model = *weaponModel,
        .bones = bones,
        .light = m_light.modelLightAt({.origin = view.camera.position, .sky = level.skyLight()}, styles),
        .skin = viewModel->skin(),
        .bodyChoices = viewModel->bodyChoices(),
        .depth = nx::render::StudioDepth::ViewModel,
        .isMirrored = viewModel->isMirrored(),
        .lights = lights,
        .probe = m_probes ? m_probes->nearest(view.camera.position) : std::nullopt}};
    if (auto drawn =
            m_viewModels.draw(list, {.camera = view.camera, .aspect = view.aspect, .instances = held});
        !drawn) {
        return nx::fail(SceneError::DrawFailed, "the weapon in hand", std::move(drawn.error()));
    }
    return {};
}

void Scene::bakeReflections(nx::rhi::CommandList& list, const Level& level, nx::Seconds time,
                            std::span<const nx::render::ModelPlacement> placements) {
    if (!m_probes || m_probes->isBaked()) {
        return;
    }
    m_probes->bakeNext(
        list, m_world,
        {.time = time, .lightStyles = level.lightStyles().valuesAt(time), .placements = placements});
}

auto Scene::shootDecal(std::uint32_t decal, nx::Vec3 position, const nx::render::DecalTarget& on)
    -> std::uint32_t {
    return m_world.shootDecal(decal, position, on);
}

auto Scene::loadWeapons(nx::rhi::CommandList& commands, const Level& level,
                        std::span<const WeaponIndex> weapons) -> nx::Result<void, SceneError> {
    for (const WeaponIndex weapon : weapons) {
        if (weapon.value >= m_weapons.size()) {
            m_weapons.resize(weapon.value + 1);
        }
        std::optional<nx::render::StudioModel>& model = m_weapons[weapon.value];
        model.reset();
        if (const ViewModel* const viewModel = level.viewModel(weapon)) {
            auto loaded = viewModel->load(m_viewModels, commands);
            if (!loaded) {
                return nx::fail(SceneError::CreateFailed, "a weapon's view model", std::move(loaded.error()));
            }
            model.emplace(std::move(*loaded));
        }
    }
    return {};
}

void Scene::flash(const ViewModel* viewModel, std::span<const nx::assets::StudioEvent* const> events,
                  std::span<const nx::Mat3x4> bones, nx::Seconds time) {
    std::erase_if(m_flashes, [time](const MuzzleFlash& shown) { return shown.until < time; });
    if (viewModel == nullptr || bones.empty()) {
        return;
    }
    std::array<std::size_t, MuzzleFlashSprites::kCount> frameCounts{};
    for (std::size_t index = 0; index < frameCounts.size(); ++index) {
        frameCounts[index] = m_flashModels[index] ? m_flashModels[index]->frameCount() : 0;
    }
    for (const nx::assets::StudioEvent* const event : events) {
        const std::optional<std::uint32_t> attachment = muzzleFlashAttachmentOf(event->event);
        const std::optional<nx::Vec3> origin =
            attachment ? viewModel->attachmentAt(bones, *attachment) : std::nullopt;
        if (origin) {
            nx::Random random{m_flashCount++, kFlashStream};
            m_flashes.push_back(muzzleFlashOf(event->options, *origin, time, frameCounts, random));
        }
    }
}

} // namespace cs
