// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;

namespace cs {

namespace {

/// The muzzle flash events: the first attachment's, and the next attachment's this many after.
constexpr std::int32_t kFirstMuzzleFlashEvent = 5001;
constexpr std::int32_t kMuzzleFlashEventStep = 10;
constexpr std::uint32_t kMuzzleFlashAttachments = 4;
/// An event's number: its units choose the sprite, its tens are its size in tenths.
constexpr std::uint32_t kDecimalBase = 10;
constexpr float kScalePerTen = 0.1F;
constexpr float kScaleWithoutTens = 0.5F;
/// The roll of the first sprite's flash, a rifle's, and of the others', at most, in whole degrees.
constexpr std::uint64_t kRifleRolls = 21;
constexpr std::uint64_t kOtherRolls = 360;

} // namespace

auto muzzleFlashAttachmentOf(std::int32_t event) noexcept -> std::optional<std::uint32_t> {
    const std::int32_t step = event - kFirstMuzzleFlashEvent;
    if (step < 0 || step % kMuzzleFlashEventStep != 0) {
        return std::nullopt;
    }
    const auto attachment = static_cast<std::uint32_t>(step / kMuzzleFlashEventStep);
    return attachment < kMuzzleFlashAttachments ? std::optional{attachment} : std::nullopt;
}

auto muzzleFlashOf(std::string_view options, nx::Vec3 origin, nx::Seconds time,
                   std::span<const std::size_t> frameCounts, nx::Random& random) -> MuzzleFlash {
    std::uint32_t type = 0;
    const auto [end, error] = std::from_chars(options.data(), options.data() + options.size(), type);
    if (error != std::errc{} || end != options.data() + options.size()) {
        type = 0;
    }
    const std::uint32_t sprite = (type % kDecimalBase) % MuzzleFlashSprites::kCount;
    const float tens = static_cast<float>(type / kDecimalBase) * kScalePerTen;
    const std::size_t frames = sprite < frameCounts.size() ? frameCounts[sprite] : 0;
    const std::uint64_t rolls = sprite == 0 ? kRifleRolls : kOtherRolls;
    return MuzzleFlash{.origin = origin,
                       .sprite = sprite,
                       .scale = tens == 0.0F ? kScaleWithoutTens : tens,
                       .roll = nx::Degrees{static_cast<float>(random.below(rolls))},
                       .frame = static_cast<float>(random.below(frames)),
                       .until = time + kMuzzleFlashTime};
}

auto MuzzleFlashSprites::read(const nx::assets::Vfs& vfs) -> MuzzleFlashSprites {
    MuzzleFlashSprites sprites;
    for (std::size_t index = 0; index < kCount; ++index) {
        const std::string path = std::format("sprites/muzzleflash{}.spr", index + 1);
        auto bytes = nx::assets::VfsPath::parse(path).and_then(
            [&vfs](const nx::assets::VfsPath& at) { return vfs.readAll(at); });
        if (!bytes) {
            sprites.m_missing.push_back(std::format("{} ({})", path, nx::describe(bytes.error())));
            continue;
        }
        auto file = std::make_unique<File>(File{.bytes = std::move(*bytes)});
        auto view = nx::assets::SpriteView::parse(file->bytes);
        if (!view) {
            sprites.m_missing.push_back(std::format("{} ({})", path, nx::describe(view.error())));
            continue;
        }
        file->view = std::move(*view);
        sprites.m_files[index] = std::move(file);
    }
    return sprites;
}

auto MuzzleFlashSprites::load(nx::render::SpriteRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<std::vector<std::optional<nx::render::SpriteModel>>, nx::render::SpriteError> {
    std::vector<std::optional<nx::render::SpriteModel>> models;
    for (std::size_t index = 0; index < kCount; ++index) {
        if (!m_files[index]) {
            models.emplace_back();
            continue;
        }
        auto model = renderer.load(commands, *m_files[index]->view, std::format("muzzleflash{}", index + 1));
        if (!model) {
            return std::unexpected{std::move(model.error())};
        }
        models.emplace_back(std::move(*model));
    }
    return models;
}

} // namespace cs
