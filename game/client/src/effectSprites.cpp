// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.assets;
import nx.rhi;
import nx.render;

namespace cs {

auto EffectSprites::read(const nx::assets::Vfs& vfs, std::span<const std::string> paths)
    -> std::vector<std::string> {
    std::vector<std::string> missing;
    for (const std::string& path : paths) {
        if (indexOf(path)) {
            continue;
        }
        File& file = *m_files.emplace_back(std::make_unique<File>(File{.path = path}));
        auto bytes = nx::assets::VfsPath::parse(path).and_then(
            [&vfs](const nx::assets::VfsPath& at) { return vfs.readAll(at); });
        if (!bytes) {
            missing.push_back(std::format("{} ({})", path, nx::describe(bytes.error())));
            continue;
        }
        file.bytes = std::move(*bytes);
        auto view = nx::assets::SpriteView::parse(file.bytes);
        if (!view) {
            missing.push_back(std::format("{} ({})", path, nx::describe(view.error())));
            continue;
        }
        file.view = std::move(*view);
    }
    return missing;
}

auto EffectSprites::indexOf(std::string_view path) const noexcept -> std::optional<std::uint32_t> {
    const auto found = std::ranges::find_if(
        m_files, [path](const std::unique_ptr<File>& file) { return file->path == path; });
    if (found == m_files.end()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(found - m_files.begin());
}

auto EffectSprites::frameCountOf(std::uint32_t index) const noexcept -> std::optional<std::uint32_t> {
    nx::expects(index < m_files.size(), "EffectSprites::frameCountOf: a sprite it read");
    const File& file = *m_files[index];
    if (!file.view) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(file.view->frames.size());
}

auto EffectSprites::viewOf(std::uint32_t index) const noexcept -> const nx::assets::SpriteView* {
    nx::expects(index < m_files.size(), "EffectSprites::viewOf: a sprite it read");
    const File& file = *m_files[index];
    return file.view ? &*file.view : nullptr;
}

auto EffectSprites::load(nx::render::SpriteRenderer& renderer, nx::rhi::CommandList& commands,
                         std::size_t first) const
    -> nx::Result<std::vector<std::optional<nx::render::SpriteModel>>, nx::render::SpriteError> {
    std::vector<std::optional<nx::render::SpriteModel>> models;
    for (std::size_t index = first; index < m_files.size(); ++index) {
        const File& file = *m_files[index];
        if (!file.view) {
            models.emplace_back();
            continue;
        }
        auto model = renderer.load(commands, *file.view, file.path);
        if (!model) {
            return std::unexpected{std::move(model.error())};
        }
        models.emplace_back(std::move(*model));
    }
    return models;
}

} // namespace cs
