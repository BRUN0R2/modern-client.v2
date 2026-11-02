// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.assets;
import nx.rhi;
import nx.render;

namespace cs {

namespace {

/// The bytes of the file at `file` of `vfs`.
[[nodiscard]] auto readFile(const nx::assets::Vfs& vfs, std::string_view file)
    -> nx::Result<std::vector<std::byte>, nx::assets::VfsError> {
    return nx::assets::VfsPath::parse(file).and_then(
        [&vfs](const nx::assets::VfsPath& path) { return vfs.readAll(path); });
}

} // namespace

auto readHudList(const nx::assets::Vfs& vfs)
    -> nx::Result<std::vector<nx::assets::SpriteListEntry>, HudError> {
    auto bytes = readFile(vfs, "sprites/hud.txt");
    if (!bytes) {
        return nx::fail(HudError::ListUnreadable, std::move(bytes.error()));
    }
    const std::string text = *bytes |
                             std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
                             std::ranges::to<std::string>();
    auto entries = nx::assets::parseSpriteList(text);
    if (!entries) {
        return nx::fail(HudError::ListUnreadable, "sprites/hud.txt", std::move(entries.error()));
    }
    return std::move(*entries);
}

auto HudSprites::imageOf(std::string_view file) -> std::size_t {
    const auto found = std::ranges::find(files, file);
    if (found == files.end()) {
        files.emplace_back(file);
        return files.size() - 1;
    }
    return nx::narrow<std::size_t>(found - files.begin());
}

auto HudSprites::imageOf(const nx::assets::SpriteListEntry& entry) -> std::size_t {
    return imageOf(std::format("sprites/{}.spr", entry.sprite));
}

auto HudSprites::load(const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen,
                      nx::rhi::CommandList& commands) const
    -> nx::Result<std::vector<nx::render::ScreenImage>, HudError> {
    std::vector<nx::render::ScreenImage> images;
    images.reserve(files.size());
    for (const std::string& file : files) {
        auto bytes = readFile(vfs, file);
        if (!bytes) {
            return nx::fail(HudError::SpriteUnreadable, file, std::move(bytes.error()));
        }
        auto view = nx::assets::SpriteView::parse(*bytes);
        if (!view) {
            return nx::fail(HudError::SpriteUnreadable, file, std::move(view.error()));
        }
        auto frames = screen.loadSprite(commands, *view, std::format("hud.{}", file));
        if (!frames) {
            return nx::fail(HudError::GpuFailed, file, std::move(frames.error()));
        }
        if (frames->empty()) {
            return nx::fail(HudError::SpriteUnreadable, std::format("{}: no frame", file));
        }
        images.push_back(std::move(frames->front()));
    }
    return images;
}

} // namespace cs
