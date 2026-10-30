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

/// How a drop of blood is lit, as a flat-shaded model is (R_StudioLighting, shaders/studio.slang): 0.8 of
/// its shade on its ambient light, 255 at most, indexing the light curve four steps to a level, its top
/// 1023.
constexpr float kFlatShadeShare = 0.8F;
constexpr float kMostModelLight = 255.0F;
constexpr float kLightCurveSteps =
    static_cast<float>(nx::render::kLightTableSize) / static_cast<float>(nx::render::kChannelValues);
constexpr float kLightCurveTop = static_cast<float>(nx::render::kLightTableSize - 1);

} // namespace

auto readSceneShaders(const std::filesystem::path& folder) -> nx::Result<SceneShaders, SceneError> {
    SceneShaders shaders;
    for (const auto& [file, code] :
         {std::pair{"world.spv", &shaders.world}, std::pair{"studio.spv", &shaders.studio},
          std::pair{"sprite.spv", &shaders.sprite}, std::pair{"particle.spv", &shaders.particle},
          std::pair{"blood.spv", &shaders.blood}, std::pair{"probe.spv", &shaders.probe},
          std::pair{"overlay.spv", &shaders.overlay}}) {
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
             nx::render::SpriteRenderer viewSprites, nx::render::ParticleRenderer particles,
             nx::render::BloodRenderer blood, nx::render::OverlayRenderer overlay) noexcept
    : m_world{std::move(world)}, m_light{std::move(light)}, m_sprites{std::move(sprites)},
      m_spriteModels{std::move(spriteModels)}, m_studio{std::move(studio)}, m_models{std::move(models)},
      m_viewModels{std::move(viewModels)}, m_viewSprites{std::move(viewSprites)},
      particleRenderer{std::move(particles)}, bloodRenderer{std::move(blood)},
      overlayRenderer{std::move(overlay)} {}

auto Scene::create(nx::rhi::Device& device, nx::rhi::CommandList& commands, const Level& level,
                   const SceneShaders& shaders, nx::rhi::Format format, nx::rhi::Format effectsFormat,
                   const nx::render::GammaSettings& gamma, nx::render::StudioLook look,
                   std::size_t decalLimit) -> nx::Result<Scene, SceneError> {
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
                                      .decalLimit = decalLimit,
                                      .viewsPerFrame = isEnhanced ? nx::render::kBakingViewsPerFrame : 1U,
                                      .encoding = encoding});
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
        device, {.shaderCode = shaders.sprite, .colorFormat = format, .gamma = gamma, .encoding = encoding});
    if (!sprites) {
        return nx::fail(SceneError::CreateFailed, "the sprites", std::move(sprites.error()));
    }
    // The effects are display colors in either look, added as the original adds them.
    auto viewSprites =
        nx::render::SpriteRenderer::create(device, {.shaderCode = shaders.sprite,
                                                    .colorFormat = effectsFormat,
                                                    .gamma = gamma,
                                                    .encoding = nx::render::ColorEncoding::Display});
    if (!viewSprites) {
        return nx::fail(SceneError::CreateFailed, "the muzzle flashes", std::move(viewSprites.error()));
    }
    auto particles = nx::render::ParticleRenderer::create(
        device, commands,
        {.shaderCode = shaders.particle, .colorFormat = format, .gamma = gamma, .encoding = encoding});
    if (!particles) {
        return nx::fail(SceneError::CreateFailed, "the particles", std::move(particles.error()));
    }
    // The streaks' sprite, which the level read, or logged it could not.
    if (const std::optional<std::uint32_t> streak = level.streakSprites().indexOf(kStreakSprite)) {
        if (const nx::assets::SpriteView* const sprite = level.streakSprites().viewOf(*streak)) {
            if (auto loaded = particles->loadStreakSprite(commands, *sprite); !loaded) {
                return nx::fail(SceneError::CreateFailed, "the streaks' sprite", std::move(loaded.error()));
            }
        }
    }
    auto blood = nx::render::BloodRenderer::create(
        device, {.shaderCode = shaders.blood, .colorFormat = format, .encoding = encoding});
    if (!blood) {
        return nx::fail(SceneError::CreateFailed, "the blood", std::move(blood.error()));
    }
    auto overlay = nx::render::OverlayRenderer::create(
        device, {.shaderCode = shaders.overlay, .colorFormat = format, .encoding = encoding});
    if (!overlay) {
        return nx::fail(SceneError::CreateFailed, "the overlay", std::move(overlay.error()));
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
    auto playerModels = level.players().load(*studio, commands);
    if (!playerModels) {
        return nx::fail(SceneError::CreateFailed, "the players' models", std::move(playerModels.error()));
    }
    Scene scene{std::move(*world),      nx::render::WorldLight{level.map()},
                std::move(*sprites),    std::move(*spriteModels),
                std::move(*studio),     std::move(*models),
                std::move(*viewModels), std::move(*viewSprites),
                std::move(*particles),  std::move(*blood),
                std::move(*overlay)};
    scene.m_look = look;
    scene.lightCurve = nx::render::lightGammaTable(gamma);
    scene.m_playerModels = std::move(*playerModels);
    auto bloodModels = level.bloodSprites().load(scene.m_sprites, commands, 0);
    if (!bloodModels) {
        return nx::fail(SceneError::CreateFailed, "the blood's sprites", std::move(bloodModels.error()));
    }
    scene.bloodModels = std::move(*bloodModels);
    for (const TempSprite sprite : nx::EnumArray<TempSprite, bool>::keys()) {
        scene.tempSpriteIndices[sprite] =
            level.bloodSprites().indexOf(pathOf(sprite, level.blood().sprayLook));
    }
    if (auto loaded = scene.loadWeapons(commands, level, level.weapons()); !loaded) {
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
    const nx::render::LightStyleValues& styles = view.lightStyles;
    m_world.prepare({.camera = view.camera,
                     .aspect = view.aspect,
                     .time = view.time,
                     .lightStyles = styles,
                     .placements = view.placements,
                     .fog = view.fog});
    const PosedModels models =
        level.models().posed(m_models, view.time, view.hostages, m_light, level.skyLight(), styles);
    const PosedModels players =
        level.players().posed(m_playerModels, view.players, m_light, level.skyLight(), styles);
    const PosedModels entities =
        entityModels.posed(level.entityCatalog(), view.entities, m_light, level.skyLight(), styles);
    std::vector<nx::render::StudioInstance> instances = models.instances;
    std::ranges::copy(players.instances, std::back_inserter(instances));
    std::ranges::copy(entities.instances, std::back_inserter(instances));
    if (auto prepared = m_studio.prepare(
            {.camera = view.camera, .aspect = view.aspect, .instances = instances, .fog = view.fog});
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
        bones = viewModel->pose(view.camera, view.viewModel->sequence, view.viewModel->intoIt, view.hand);
    }
    const std::optional<WeaponIndex> inHand =
        bones.empty() ? std::nullopt : std::optional{view.viewModel->weapon};
    flash(level, viewModel, inHand, view.clientEvents, bones, view.time);
    // A flash shows where its attachment is in this frame's pose: it follows the muzzle.
    const auto whereIs = [viewModel, &bones](const MuzzleFlashAnchor& anchor) {
        return viewModel != nullptr ? viewModel->attachmentAt(bones, anchor.attachment) : std::nullopt;
    };
    std::vector<nx::render::SpriteInstance> sprites = level.sprites().instances(m_spriteModels, view.time);
    for (nx::render::SpriteInstance& sprite : sprites) {
        if (sprite.mode == nx::render::RenderMode::Glow) {
            sprite.isHidden = level.glowBlockers().hides(level.hulls(), view.placements, view.camera.position,
                                                         sprite.origin);
        }
    }
    // The temporary entities: their textures, as translucent as they are (kRenderTransTexture).
    for (const TempEntity& entity : view.tempEntities) {
        const std::optional<std::uint32_t> index = tempSpriteIndices[entity.sprite];
        if (!index || !bloodModels[*index]) {
            continue; // the level could not read its sprite, as it logged
        }
        sprites.push_back(nx::render::SpriteInstance{.model = *bloodModels[*index],
                                                     .origin = entity.origin,
                                                     .angles = {.roll = entity.roll},
                                                     .frame = entity.frame,
                                                     .scale = entity.scale,
                                                     .mode = nx::render::RenderMode::TransTexture,
                                                     .amount = entity.amount,
                                                     .color = entity.color});
    }
    if (auto prepared = m_sprites.prepare({.camera = view.camera,
                                           .aspect = view.aspect,
                                           .time = view.time,
                                           .lightStyles = styles,
                                           .instances = sprites,
                                           .fog = view.fog},
                                          m_light);
        !prepared) {
        return nx::fail(SceneError::DrawFailed, "the sprites", std::move(prepared.error()));
    }
    nx::render::drawInOrder(list, m_world, m_studio, m_sprites);
    // The particles and the streaks, over everything blended (CL_DrawEFX's last pass): each particle of
    // its entry of the engine's palette, none without it.
    std::vector<nx::render::ParticleInstance> particles;
    if (const std::optional<nx::assets::Palette>& palette = level.palette()) {
        particles.reserve(view.particles.size());
        for (const Particle& particle : view.particles) {
            const nx::assets::PaletteColor color = palette->color(particle.color);
            particles.push_back(nx::render::ParticleInstance{
                .origin = particle.origin,
                .color = {.red = color.red, .green = color.green, .blue = color.blue},
                .amount = amountOf(particle, view.time)});
        }
    }
    std::vector<nx::render::StreakInstance> streaks;
    streaks.reserve(view.streaks.size());
    for (const Streak& streak : view.streaks) {
        streaks.push_back(nx::render::StreakInstance{.start = streak.origin,
                                                     .end = endOf(streak, view.time),
                                                     .halfWidth = kStreakHalfWidth,
                                                     .color = streak.color,
                                                     .amount = amountOf(streak, view.time)});
    }
    if (auto prepared = particleRenderer.prepare({.camera = view.camera,
                                                  .aspect = view.aspect,
                                                  .particles = particles,
                                                  .streaks = streaks,
                                                  .fog = view.fog});
        !prepared) {
        return nx::fail(SceneError::DrawFailed, "the particles", std::move(prepared.error()));
    }
    particleRenderer.drawPrepared(list);
    // The enhanced look's blood, over everything blended too.
    if (auto drawn = bloodRenderer.draw(list, {.camera = view.camera,
                                               .aspect = view.aspect,
                                               .drops = view.blood,
                                               .streakTime = level.blood().enhancedLook.streakTime,
                                               .fog = view.fog});
        !drawn) {
        return nx::fail(SceneError::DrawFailed, "the blood", std::move(drawn.error()));
    }
    if (auto drawn = overlayRenderer.draw(list, {.camera = view.camera,
                                                 .aspect = view.aspect,
                                                 .isMeshShown = view.overlay.isMeshShown,
                                                 .arrows = view.overlay.arrows,
                                                 .arrowStyle = view.overlay.arrowStyle});
        !drawn) {
        return nx::fail(SceneError::DrawFailed, "the overlay", std::move(drawn.error()));
    }
    // The weapon's flashes, for drawEffects, after it, in its depths: added over what of it is behind
    // them, hidden by what of it is in front, and never by a wall the muzzle is pushed against.
    std::vector<nx::render::SpriteInstance> flashes;
    for (const MuzzleFlash& shown : m_flashes.shown()) {
        const std::optional<float> frame = shown.frameAt(view.time);
        const std::optional<nx::Vec3> origin = whereIs(shown.anchor);
        if (!frame || !origin) {
            continue; // only its light is left
        }
        flashes.push_back(nx::render::SpriteInstance{.model = *m_flashModels[shown.sprite],
                                                     .origin = *origin,
                                                     .angles = {.roll = shown.rollAt(view.time)},
                                                     .frame = *frame,
                                                     .scale = shown.scale,
                                                     .mode = nx::render::RenderMode::TransAdd,
                                                     .amount = kFullAmount,
                                                     .brightness = shown.brightness});
    }
    if (auto prepared = m_viewSprites.prepare({.camera = view.camera,
                                               .aspect = view.aspect,
                                               .time = view.time,
                                               .lightStyles = styles,
                                               .instances = flashes,
                                               .depth = nx::render::DepthLayer::ViewModel,
                                               .fog = view.fog},
                                              m_light);
        !prepared) {
        return nx::fail(SceneError::DrawFailed, "the weapon's muzzle flashes", std::move(prepared.error()));
    }
    if (bones.empty()) {
        return {};
    }
    // The weapon in hand is lit as modern shooters light theirs, readable whatever the map: a key light
    // above it and behind the eye, carried with the view, first; then the light of its flashes, one a
    // muzzle, the nearest; then the map's lights nearest it, in the room left.
    std::vector<nx::render::StudioLight> lights;
    if (m_look == nx::render::StudioLook::Enhanced) {
        lights.push_back(viewModelLightOf(view.camera));
        std::ranges::copy(m_flashes.lightsAt(view.time, whereIs), std::back_inserter(lights));
        const std::size_t room =
            nx::render::kMostStudioLights - std::min(lights.size(), nx::render::kMostStudioLights);
        std::ranges::copy(nearestLights(level.lights(), view.camera.position, room),
                          std::back_inserter(lights));
    }
    const std::array held{nx::render::StudioInstance{
        .model = *weaponModel,
        .bones = bones,
        .light = m_light.modelLightAt({.origin = view.camera.position, .sky = level.skyLight()}, styles),
        .skin = viewModel->skin(),
        .bodyChoices = viewModel->bodyChoices(),
        .depth = nx::render::DepthLayer::ViewModel,
        .isMirrored = viewModel->isMirroredIn(view.hand),
        .lights = lights,
        .probes = m_probes ? m_probes->blendAt(view.camera.position) : std::nullopt}};
    if (auto drawn = m_viewModels.draw(
            list, {.camera = view.camera, .aspect = view.aspect, .instances = held, .fog = view.fog});
        !drawn) {
        return nx::fail(SceneError::DrawFailed, "the weapon in hand", std::move(drawn.error()));
    }
    return {};
}

void Scene::drawEffects(nx::rhi::CommandList& list) const {
    nx::render::drawInOrder(list, m_viewSprites);
}

auto Scene::bloodLightAt(const Level& level, nx::Vec3 position,
                         const nx::render::LightStyleValues& styles) const -> float {
    const nx::render::ModelLight light =
        m_light.modelLightAt({.origin = position, .sky = level.skyLight()}, styles);
    const float summed = std::min(light.ambient + (light.shade * kFlatShadeShare), kMostModelLight);
    const auto index = std::min(static_cast<std::size_t>(summed * kLightCurveSteps), lightCurve.size() - 1);
    return static_cast<float>(lightCurve[index]) / kLightCurveTop;
}

auto Scene::keepOverlay(nx::rhi::CommandList& commands, const nx::render::OverlayMesh& mesh)
    -> nx::Result<void, SceneError> {
    if (auto kept = overlayRenderer.keep(commands, mesh); !kept) {
        return nx::fail(SceneError::CreateFailed, "the overlay's mesh", std::move(kept.error()));
    }
    return {};
}

void Scene::bakeReflections(nx::rhi::CommandList& list, const SceneView& view) {
    if (!m_probes || m_probes->isBaked()) {
        return;
    }
    m_probes->bakeNext(list, m_world,
                       {.time = view.time, .lightStyles = view.lightStyles, .placements = view.placements});
}

auto Scene::shootDecal(nx::render::DecalImageIndex decal, nx::Vec3 position,
                       const nx::render::DecalTarget& on) -> std::uint32_t {
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
        if (auto loaded = level.players().loadWeapon(m_studio, commands, weapon, m_playerModels); !loaded) {
            return nx::fail(SceneError::CreateFailed, "a weapon's player model", std::move(loaded.error()));
        }
    }
    auto flashModels = level.muzzleFlashes().load(m_viewSprites, commands, m_flashModels.size());
    if (!flashModels) {
        return nx::fail(SceneError::CreateFailed, "the muzzle flashes", std::move(flashModels.error()));
    }
    std::ranges::move(*flashModels, std::back_inserter(m_flashModels));
    return {};
}

auto Scene::loadEntities(nx::rhi::CommandList& commands, const EntityCatalog& catalog,
                         const nx::assets::Vfs& vfs) -> nx::Result<void, SceneError> {
    if (auto loaded = entityModels.load(catalog, m_studio, vfs, commands); !loaded) {
        return nx::fail(SceneError::CreateFailed, "an entity model", std::move(loaded.error()));
    }
    return {};
}

void Scene::flash(const Level& level, const ViewModel* viewModel, std::optional<WeaponIndex> inHand,
                  std::span<const PassedEvent> events, std::span<const nx::Mat3x4> bones, nx::Seconds time) {
    m_flashes.forget(time, inHand);
    if (viewModel == nullptr || !inHand) {
        return;
    }
    for (const PassedEvent& passed : events) {
        const nx::assets::StudioEvent& event = *passed.event;
        const std::optional<std::uint32_t> attachment = muzzleFlashAttachmentOf(event.event);
        if (!attachment || !viewModel->attachmentAt(bones, *attachment)) {
            continue; // a model without that attachment shows no flash
        }
        // Its sprite was read with the view model; one that could not be, logged then, shows nothing.
        const MuzzleFlashStyle style = viewModel->muzzleFlashStyleOf(event);
        const std::optional<std::uint32_t> sprite = level.muzzleFlashes().indexOf(style.sprite);
        if (!sprite || *sprite >= m_flashModels.size() || !m_flashModels[*sprite]) {
            continue;
        }
        nx::Random random{m_flashCount++, kFlashStream};
        if (std::optional<MuzzleFlash> shown = muzzleFlashOf(
                style, *sprite, m_flashModels[*sprite]->frameCount(),
                MuzzleFlashAnchor{.weapon = *inHand, .attachment = *attachment}, time, random)) {
            m_flashes.show(std::move(*shown));
        }
    }
}

} // namespace cs
