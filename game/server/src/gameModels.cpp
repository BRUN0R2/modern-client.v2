// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;
import nx.assets;
import cs.shared;

namespace cs {

auto GameModels::read(const nx::assets::Vfs& vfs, const nx::assets::BspView& map) -> GameModels {
    GameModels read;
    std::map<std::string, std::optional<std::uint32_t>, std::less<>> files; // by path; none: left out
    const std::vector<MapHostage> hostages = mapHostagesOf(map);
    for (std::uint32_t spawn = 0; spawn < hostages.size(); ++spawn) {
        const MapHostage& hostage = hostages[spawn];
        auto [found, isNew] = files.try_emplace(hostage.model | std::views::transform(nx::toLowerAscii) |
                                                std::ranges::to<std::string>()); // the VFS ignores case
        if (isNew) {
            auto modelFiles = readModelFiles(vfs, hostage.model, ModelTextures::Skipped);
            if (!modelFiles) {
                read.m_missing.push_back(
                    std::format("{} ({})", hostage.model, nx::describe(modelFiles.error())));
                continue;
            }
            auto animations = animationsOf(vfs, **modelFiles);
            if (!animations) {
                read.m_missing.push_back(
                    std::format("{} ({})", hostage.model, nx::describe(animations.error())));
                continue;
            }
            found->second = nx::narrow<std::uint32_t>(read.m_files.size());
            read.m_files.push_back(std::make_unique<File>(
                File{.files = std::move(*modelFiles), .animations = std::move(*animations)}));
        }
        if (!found->second) {
            continue;
        }
        read.m_hostageSpawns.push_back(HostageSpawn{
            .spawn = spawn, .model = *found->second, .origin = hostage.origin, .angles = hostage.angles});
    }
    // The files no longer move: what the game plays points into them.
    read.m_played.reserve(read.m_files.size());
    for (const std::unique_ptr<File>& file : read.m_files) {
        read.m_played.push_back(PlayedModel{.view = &*file->files->view, .animations = file->animations});
    }
    return read;
}

} // namespace cs
