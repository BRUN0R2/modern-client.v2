// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.assets;

namespace cs {

namespace {

/// The game's decals, which hold Half-Life's too.
constexpr std::string_view kDecalWad = "decals.wad";

} // namespace

auto LevelDecals::read(const nx::assets::Vfs& vfs, std::span<const std::string_view> names) -> LevelDecals {
    LevelDecals decals;
    std::vector<std::string_view> wanted;
    for (const std::string_view name : names) {
        if (std::ranges::none_of(
                wanted, [name](std::string_view had) { return nx::equalsIgnoringCase(had, name); })) {
            wanted.push_back(name);
        }
    }
    const auto leaveOut = [&decals](std::string_view name, std::string_view why) {
        decals.m_missing.push_back(std::format("{} ({})", name, why));
    };
    auto bytes = nx::assets::VfsPath::parse(kDecalWad).and_then(
        [&vfs](const nx::assets::VfsPath& path) { return vfs.readAll(path); });
    if (!bytes) {
        const std::string why = std::format("{}: {}", kDecalWad, nx::describe(bytes.error()));
        std::ranges::for_each(wanted, [&](std::string_view name) { leaveOut(name, why); });
        return decals;
    }
    decals.m_wadBytes = std::move(*bytes);
    const auto wad = nx::assets::WadView::parse(decals.m_wadBytes);
    if (!wad) {
        const std::string why = std::format("{}: {}", kDecalWad, nx::describe(wad.error()));
        std::ranges::for_each(wanted, [&](std::string_view name) { leaveOut(name, why); });
        return decals;
    }
    for (const std::string_view name : wanted) {
        const nx::assets::WadEntry* const entry = wad->find(name);
        if (entry == nullptr) {
            leaveOut(name, std::format("not in {}", kDecalWad));
            continue;
        }
        auto texture = nx::assets::readTexture(*entry);
        if (!texture) {
            leaveOut(name, nx::describe(texture.error()));
            continue;
        }
        decals.m_images.push_back(std::move(*texture));
        decals.m_names.emplace_back(name);
    }
    return decals;
}

auto LevelDecals::indexOf(std::string_view name) const noexcept -> std::optional<std::uint32_t> {
    const auto found = std::ranges::find_if(
        m_names, [name](std::string_view had) { return nx::equalsIgnoringCase(had, name); });
    if (found == m_names.end()) {
        return std::nullopt;
    }
    return nx::narrow<std::uint32_t>(found - m_names.begin());
}

} // namespace cs
