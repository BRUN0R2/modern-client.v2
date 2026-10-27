// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
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

auto PlayerModels::posed(const PlayerGpuModels& models, std::span<const ShownPlayer> players,
                         const nx::render::WorldLight& light, const nx::render::SkyLight& sky,
                         const nx::render::LightStyleValues& styles) const -> PosedModels {
    PosedModels posed;
    struct Drawn {
        const nx::render::StudioModel* model = nullptr;
        nx::Vec3 origin{};
        nx::render::ModelLight light{};
    };
    std::vector<Drawn> drawn;
    for (const ShownPlayer& player : players) {
        const nx::assets::MdlView* const view = characterModel(player.character);
        if (view == nullptr || player.character.value >= models.characters.size() ||
            !models.characters[player.character.value]) {
            continue; // one whose model could not be read, which was listed
        }
        const AnimatedModel& character = *m_characters[player.character.value];
        const nx::Mat3x4 placement = placementOf(player.origin, player.angles);
        std::vector<nx::Mat3x4> bones = posePlayerBones(*view, character.animations, player.sequence,
                                                        PlayerPoseQuery{.frame = player.frame,
                                                                        .gaitSequence = player.gaitSequence,
                                                                        .gaitFrame = player.gaitFrame,
                                                                        .yawBlend = player.yawBlend,
                                                                        .pitchBlend = player.pitchBlend,
                                                                        .placement = placement});
        const nx::render::ModelLight lit = light.modelLightAt({.origin = player.origin, .sky = sky}, styles);
        if (player.weapon && player.weapon->value < m_weapons.size() && m_weapons[player.weapon->value] &&
            !m_weapons[player.weapon->value]->animations.empty() &&
            player.weapon->value < models.weapons.size() && models.weapons[player.weapon->value]) {
            const AnimatedModel& weapon = *m_weapons[player.weapon->value];
            posed.bones.push_back(mergeBones(*weapon.files->view, weapon.animations.front(),
                                             skeletonOf(*view), bones, placement));
            drawn.push_back(Drawn{
                .model = &*models.weapons[player.weapon->value], .origin = player.origin, .light = lit});
        }
        posed.bones.push_back(std::move(bones));
        drawn.push_back(Drawn{
            .model = &*models.characters[player.character.value], .origin = player.origin, .light = lit});
    }
    // The instances point into the bones, which no longer move.
    for (std::size_t index = 0; index < drawn.size(); ++index) {
        posed.instances.push_back(nx::render::StudioInstance{.model = *drawn[index].model,
                                                             .bones = posed.bones[index],
                                                             .light = drawn[index].light,
                                                             .origin = drawn[index].origin});
    }
    return posed;
}

} // namespace cs
