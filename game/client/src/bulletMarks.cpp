// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.assets;
import nx.render;

namespace cs {

namespace {

/// The breakables, whose "material" chooses their mark first.
constexpr std::string_view kBreakableClass = "func_breakable";
/// The materials of a breakable, as the game numbers them (Materials): glass, its default, and
/// unbreakable glass; one it does not number is wood, which leaves the mark of how it is drawn.
constexpr int kGlass = 0;
constexpr int kUnbreakableGlass = 7;

} // namespace

auto bulletDecals() -> std::vector<std::string_view> {
    std::vector<std::string_view> decals{kGunshotDecals.begin(), kGunshotDecals.end()};
    decals.insert(decals.end(), kBrokenGlassDecals.begin(), kBrokenGlassDecals.end());
    decals.push_back(kBulletproofDecal);
    return decals;
}

auto bulletMarkOf(const nx::assets::BspEntity& entity) -> BulletMark {
    if (entity.find("classname") == kBreakableClass) {
        switch (entity.findInteger("material").value_or(kGlass)) {
        case kGlass:
            return BulletMark::BrokenGlass;
        case kUnbreakableGlass:
            return BulletMark::Bulletproof;
        default:
            break;
        }
    }
    // The mode as the game keeps it, any number: one the renderer does not know is not normal either.
    const int mode =
        entity.findInteger("rendermode").value_or(std::to_underlying(nx::render::RenderMode::Normal));
    if (mode == std::to_underlying(nx::render::RenderMode::TransAlpha)) {
        return BulletMark::None;
    }
    return mode == std::to_underlying(nx::render::RenderMode::Normal) ? BulletMark::Gunshot
                                                                      : BulletMark::Bulletproof;
}

auto bulletDecalOf(BulletMark mark, nx::Random& random) -> std::optional<std::string_view> {
    switch (mark) {
    case BulletMark::None:
        return std::nullopt;
    case BulletMark::Gunshot:
        return kGunshotDecals.at(random.below(kGunshotDecals.size()));
    case BulletMark::Bulletproof:
        return kBulletproofDecal;
    case BulletMark::BrokenGlass:
        return kBrokenGlassDecals.at(random.below(kBrokenGlassDecals.size()));
    }
    return std::nullopt;
}

} // namespace cs
