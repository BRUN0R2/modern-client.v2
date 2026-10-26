// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.assets;
import cs.shared;

namespace cs {

auto GameModels::fileOf(const nx::assets::Vfs& vfs, std::string_view path,
                        std::map<std::string, std::optional<std::uint32_t>, std::less<>>& files)
    -> std::optional<std::uint32_t> {
    auto [found, isNew] = files.try_emplace(path | std::views::transform(nx::toLowerAscii) |
                                            std::ranges::to<std::string>()); // the VFS ignores case
    if (!isNew) {
        return found->second;
    }
    auto modelFiles = readModelFiles(vfs, path, ModelTextures::Skipped);
    if (!modelFiles) {
        m_missing.push_back(std::format("{} ({})", path, nx::describe(modelFiles.error())));
        return std::nullopt;
    }
    auto animations = animationsOf(vfs, **modelFiles);
    if (!animations) {
        m_missing.push_back(std::format("{} ({})", path, nx::describe(animations.error())));
        return std::nullopt;
    }
    found->second = nx::narrow<std::uint32_t>(m_files.size());
    m_files.push_back(
        std::make_unique<File>(File{.files = std::move(*modelFiles), .animations = std::move(*animations)}));
    return found->second;
}

auto GameModels::read(const nx::assets::Vfs& vfs, const nx::assets::BspView& map,
                      const Characters& characters) -> GameModels {
    GameModels read;
    std::map<std::string, std::optional<std::uint32_t>, std::less<>> files; // by path; none: left out
    const std::vector<MapHostage> hostages = mapHostagesOf(map);
    for (std::uint32_t spawn = 0; spawn < hostages.size(); ++spawn) {
        const MapHostage& hostage = hostages[spawn];
        if (const std::optional<std::uint32_t> file = read.fileOf(vfs, hostage.model, files)) {
            read.m_hostageSpawns.push_back(HostageSpawn{
                .spawn = spawn, .model = *file, .origin = hostage.origin, .angles = hostage.angles});
        }
    }
    for (const Character& character : characters.characters) {
        read.m_characterFiles.push_back(read.fileOf(vfs, modelOf(character), files));
    }
    // The files no longer move: what the game plays points into them.
    read.m_played.reserve(read.m_files.size());
    for (const std::unique_ptr<File>& file : read.m_files) {
        read.m_played.push_back(PlayedModel{.view = &*file->files->view, .animations = file->animations});
    }
    return read;
}

auto GameModels::characterModel(CharacterIndex character) const noexcept -> const PlayedModel* {
    if (character.value >= m_characterFiles.size() || !m_characterFiles[character.value]) {
        return nullptr;
    }
    return &m_played[*m_characterFiles[character.value]];
}

} // namespace cs
