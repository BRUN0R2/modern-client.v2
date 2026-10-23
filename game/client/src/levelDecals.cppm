// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The decals a level shows (docs/05-rendering.md, section 8): those of the game's decals.wad it names,
/// read once, each found by its name as the original's decal list finds it, ignoring case.
export module cs.client:levelDecals;

import std;
import nx.core;
import nx.assets;

export namespace cs {

/// The decals of decals.wad a level shows: their images, and where each name is among them.
class LevelDecals {
public:
    /// The decals `names` of the game's decals.wad, read from `vfs`, each once whatever the case of its
    /// name; those it lacks or cannot read are left out and listed, all of them when decals.wad cannot
    /// be read.
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs, std::span<const std::string_view> names)
        -> LevelDecals;

    LevelDecals(LevelDecals&& other) noexcept = default;
    auto operator=(LevelDecals&& other) noexcept -> LevelDecals& = default;
    LevelDecals(const LevelDecals&) = delete;
    auto operator=(const LevelDecals&) -> LevelDecals& = delete;
    ~LevelDecals() = default;

    /// Their images, in the order of the names read: what nx::render::BspWorldDesc::decals takes.
    [[nodiscard]] inline auto images() const noexcept -> std::span<const nx::assets::MipTexture> {
        return m_images;
    }
    /// Where the decal `name` is among images(), its case ignored; none for one left out.
    [[nodiscard]] auto indexOf(std::string_view name) const noexcept -> std::optional<std::uint32_t>;
    /// The decals left out, each with why.
    [[nodiscard]] inline auto missing() const noexcept -> std::span<const std::string> { return m_missing; }

private:
    LevelDecals() = default;

    std::vector<std::byte> m_wadBytes;
    std::vector<nx::assets::MipTexture> m_images; ///< Views of m_wadBytes, which a move keeps in place.
    std::vector<std::string> m_names;             ///< Of each image, as it was asked for.
    std::vector<std::string> m_missing;
};

} // namespace cs
