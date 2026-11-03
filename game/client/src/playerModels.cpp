// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.jobs;
import nx.assets;
import nx.rhi;
import nx.render;
import cs.shared;

namespace cs {

namespace {

/// The placement of a model at `origin`, turned by `angles`, as the engine's studio transform turns it:
/// the pitch negated (R_StudioSetUpTransform).
[[nodiscard]] auto placementOf(nx::Vec3 origin, nx::EulerAngles angles) -> nx::Mat3x4 {
    const nx::EulerAngles turn{.pitch = -angles.pitch, .yaw = angles.yaw, .roll = angles.roll};
    return nx::affine(nx::rotationMatrix(turn), origin);
}

/// The most models a player is drawn with: its character's and its weapon's.
constexpr std::size_t kModelsOfAPlayer = 2;

/// The first blending of a player's sequence of two to eight blends the client draws: the yaw's blend
/// cut to a whole one, as the server tells it (CalculateYawBlend's pev->blending[0]).
[[nodiscard]] auto blendingOf(float yawBlend) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(std::trunc(yawBlend));
}

} // namespace

auto PlayerModels::readFile(const nx::assets::Vfs& vfs, std::string_view path)
    -> std::optional<AnimatedModel> {
    auto model = readAnimatedModel(vfs, path, ModelTextures::Read);
    if (!model) {
        m_missing.push_back(std::format("{} ({})", path, nx::describe(model.error())));
        return std::nullopt;
    }
    return std::move(*model);
}

auto PlayerModels::read(const nx::assets::Vfs& vfs, const Characters& characters,
                        const WeaponCatalog& weapons) -> PlayerModels {
    PlayerModels read;
    for (const Character& character : characters.characters) {
        read.m_characters.push_back(read.readFile(vfs, modelOf(character)));
    }
    for (std::uint32_t index = 0; index < weapons.weapons().size(); ++index) {
        read.readWeapon(vfs, weapons, WeaponIndex{index});
    }
    return read;
}

void PlayerModels::readWeapon(const nx::assets::Vfs& vfs, const WeaponCatalog& weapons, WeaponIndex weapon) {
    if (weapon.value >= m_weapons.size()) {
        m_weapons.resize(weapon.value + 1);
    }
    m_weapons[weapon.value] =
        weapons.isComplete(weapon) ? readFile(vfs, weapons.weapon(weapon).models.player) : std::nullopt;
}

auto PlayerModels::characterModel(CharacterIndex character) const noexcept -> const nx::assets::MdlView* {
    if (character.value >= m_characters.size() || !m_characters[character.value]) {
        return nullptr;
    }
    return &*m_characters[character.value]->files->view;
}

auto PlayerModels::load(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<PlayerGpuModels, nx::render::StudioError> {
    PlayerGpuModels models;
    for (const std::optional<AnimatedModel>& file : m_characters) {
        if (!file) {
            models.characters.emplace_back();
            continue;
        }
        auto model =
            renderer.load(commands, {.model = *file->files->view, .textures = file->files->texturesView()});
        if (!model) {
            return std::unexpected{std::move(model.error())};
        }
        models.characters.emplace_back(std::move(*model));
    }
    return models;
}

auto PlayerModels::loadWeapon(nx::render::StudioRenderer& renderer, nx::rhi::CommandList& commands,
                              WeaponIndex weapon, PlayerGpuModels& models) const
    -> nx::Result<void, nx::render::StudioError> {
    if (weapon.value >= models.weapons.size()) {
        models.weapons.resize(weapon.value + 1);
    }
    models.weapons[weapon.value].reset();
    if (weapon.value >= m_weapons.size() || !m_weapons[weapon.value]) {
        return {};
    }
    const AnimatedModel& file = *m_weapons[weapon.value];
    auto model =
        renderer.load(commands, {.model = *file.files->view, .textures = file.files->texturesView()});
    if (!model) {
        return std::unexpected{std::move(model.error())};
    }
    models.weapons[weapon.value].emplace(std::move(*model));
    return {};
}

void PlayerModels::pose(const PlayerGpuModels& models, std::span<const ShownPlayer> players,
                        const ModelFrame& frame, PosedModels& posed) const {
    posed.clear();
    // A player the frame may show, what it is drawn with, and where its bones are among them all.
    struct Posing {
        const ShownPlayer* player = nullptr;
        const AnimatedModel* character = nullptr;
        const nx::render::StudioModel* characterDrawn = nullptr;
        const AnimatedModel* weapon = nullptr; ///< None without one to draw in its hands.
        const nx::render::StudioModel* weaponDrawn = nullptr;
        std::size_t firstBone = 0; ///< Those of its character, then those of its weapon.
        nx::render::ModelLight light{};
    };
    std::vector<Posing> posings;
    posings.reserve(players.size());
    std::size_t boneCount = 0;
    for (const ShownPlayer& player : players) {
        const nx::assets::MdlView* const view = characterModel(player.character);
        if (view == nullptr || player.character.value >= models.characters.size() ||
            !models.characters[player.character.value]) {
            continue; // one whose model could not be read, which was listed
        }
        const std::span<const nx::assets::StudioSequence> sequences = view->sequences();
        const std::array bounds{
            sequences[player.sequence.value].bounds,
            sequences[player.gaitSequence.value < sequences.size() ? player.gaitSequence.value : 0].bounds};
        if (!frame.world.mayShow(shownBoxOf(player.origin, bounds))) {
            continue;
        }
        Posing& posing =
            posings.emplace_back(Posing{.player = &player,
                                        .character = &*m_characters[player.character.value],
                                        .characterDrawn = &*models.characters[player.character.value],
                                        .firstBone = boneCount});
        boneCount += view->bones().size();
        if (player.weapon && player.weapon->value < m_weapons.size() && m_weapons[player.weapon->value] &&
            !m_weapons[player.weapon->value]->animations.empty() &&
            player.weapon->value < models.weapons.size() && models.weapons[player.weapon->value]) {
            posing.weapon = &*m_weapons[player.weapon->value];
            posing.weaponDrawn = &*models.weapons[player.weapon->value];
            boneCount += posing.weapon->files->view->bones().size();
        }
    }
    posed.bones.resize(boneCount);
    const std::span<nx::Mat3x4> bones = posed.bones;
    poseAll(frame, {.label = "players.pose", .priority = nx::jobs::Priority::Critical}, posings.size(),
            [&posings, bones, &frame](std::size_t index) {
                Posing& posing = posings[index];
                const ShownPlayer& player = *posing.player;
                const nx::assets::MdlView& view = *posing.character->files->view;
                const nx::Mat3x4 placement = placementOf(player.origin, player.angles);
                const std::span<nx::Mat3x4> own = bones.subspan(posing.firstBone, view.bones().size());
                posePlayerBones(view, posing.character->animations, player.sequence,
                                PlayerPoseQuery{.frame = player.frame,
                                                .gaitSequence = player.gaitSequence,
                                                .gaitFrame = player.gaitFrame,
                                                .yawBlend = player.yawBlend,
                                                .pitchBlend = player.pitchBlend,
                                                .blending = blendingOf(player.yawBlend),
                                                .placement = placement},
                                own);
                posing.light =
                    frame.light.modelLightAt({.origin = player.origin, .sky = frame.sky}, frame.styles);
                if (posing.weapon != nullptr) {
                    const nx::assets::MdlView& weapon = *posing.weapon->files->view;
                    mergeBones(weapon, posing.weapon->animations.front(), skeletonOf(view), own, placement,
                               bones.subspan(posing.firstBone + own.size(), weapon.bones().size()));
                }
            });
    posed.instances.reserve(posings.size() * kModelsOfAPlayer);
    for (const Posing& posing : posings) {
        const ShownPlayer& player = *posing.player;
        const std::size_t own = posing.character->files->view->bones().size();
        const auto instance = [&](const nx::render::StudioModel& model, std::span<const nx::Mat3x4> of) {
            posed.instances.push_back(nx::render::StudioInstance{.model = model,
                                                                 .bones = of,
                                                                 .light = posing.light,
                                                                 .origin = player.origin,
                                                                 .mode = renderModeOf(player.amount),
                                                                 .amount = player.amount});
        };
        if (posing.weapon != nullptr) {
            instance(*posing.weaponDrawn,
                     bones.subspan(posing.firstBone + own, posing.weapon->files->view->bones().size()));
        }
        instance(*posing.characterDrawn, bones.subspan(posing.firstBone, own));
    }
}

} // namespace cs
