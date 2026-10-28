// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The sprites the client's effects show (docs/05-rendering.md, section 9): the muzzle flashes', the
/// blood's; each read once by its path, before a frame shows it, and put on the GPU of the renderer
/// that draws it.
export module cs.client:effectSprites;

import std;
import nx.core;
import nx.assets;
import nx.rhi;
import nx.render;

export namespace cs {

/// The sprites an effect shows, each read once by its path, before a frame shows it.
class EffectSprites {
public:
    EffectSprites() = default;
    EffectSprites(EffectSprites&& other) noexcept = default;
    auto operator=(EffectSprites&& other) noexcept -> EffectSprites& = default;
    EffectSprites(const EffectSprites&) = delete;
    auto operator=(const EffectSprites&) -> EffectSprites& = delete;
    ~EffectSprites() = default;

    /// Reads from `vfs` each sprite of `paths` not read yet. Returns those missing or unreadable, with
    /// why: they are left out, and what shows them not shown.
    auto read(const nx::assets::Vfs& vfs, std::span<const std::string> paths) -> std::vector<std::string>;

    /// The place of the sprite `path` names among those read; none for one not read.
    [[nodiscard]] auto indexOf(std::string_view path) const noexcept -> std::optional<std::uint32_t>;
    /// How many it read, those left out among them.
    [[nodiscard]] inline auto count() const noexcept -> std::size_t { return m_files.size(); }
    /// How many frames the sprite at `index` has; none for one left out.
    [[nodiscard]] auto frameCountOf(std::uint32_t index) const noexcept -> std::optional<std::uint32_t>;

    /// Each sprite from the one at `first` on the GPU of `renderer`, its uploads recorded in
    /// `commands`; none for one left out. Errors: those of SpriteRenderer::load.
    [[nodiscard]] auto load(nx::render::SpriteRenderer& renderer, nx::rhi::CommandList& commands,
                            std::size_t first) const
        -> nx::Result<std::vector<std::optional<nx::render::SpriteModel>>, nx::render::SpriteError>;

private:
    /// A sprite file: its path, its bytes, which its view points into; no view for one left out.
    struct File {
        std::string path{};
        std::vector<std::byte> bytes{};
        std::optional<nx::assets::SpriteView> view{};
    };

    std::vector<std::unique_ptr<File>> m_files; ///< Pointers: the views stay as more are read.
};

} // namespace cs
