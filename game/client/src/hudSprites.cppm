// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The sprites of the HUD's pictures (docs/12-ui-hud-and-console.md, section 3.2): sprites/hud.txt, which
/// names each picture's rectangle of a sprite, and the sprites on the GPU, each once, for what draws them.
export module cs.client:hudSprites;

import std;
import nx.core;
import nx.assets;
import nx.rhi;
import nx.render;

export namespace cs {

/// Why the HUD's pictures could not be read or put on the GPU.
enum class HudError : std::uint8_t {
    ListUnreadable,   ///< sprites/hud.txt is missing or invalid (cause).
    PictureMissing,   ///< A list has no entry for screens 640 wide of a picture the HUD draws (detail).
    SpriteUnreadable, ///< A sprite of the HUD's pictures is missing or invalid (cause).
    GpuFailed,        ///< The GPU refused a sprite (cause: the ScreenError).
};

/// The screens the HUD's entries of sprites/hud.txt are for, the widths they start at, which the original
/// draws at every resolution from 640 wide up.
inline constexpr std::uint32_t kHudResolution = 640;

/// The entries of sprites/hud.txt of `vfs`. Errors: ListUnreadable.
[[nodiscard]] auto readHudList(const nx::assets::Vfs& vfs)
    -> nx::Result<std::vector<nx::assets::SpriteListEntry>, HudError>;

/// A picture of a sprite: its rectangle, and which of the images its sprite is.
struct HudSpritePicture {
    nx::assets::SpriteRect rect{};
    std::size_t image = 0;

    [[nodiscard]] inline auto operator==(const HudSpritePicture&) const noexcept -> bool = default;
};

/// The sprites pictures are in: each once, by its path, in the order they are first asked for.
class HudSprites {
public:
    /// Which of the images the sprite at path `file` is.
    [[nodiscard]] auto imageOf(std::string_view file) -> std::size_t;
    /// Which of the images the sprite of `entry` is: sprites/<sprite>.spr.
    [[nodiscard]] auto imageOf(const nx::assets::SpriteListEntry& entry) -> std::size_t;

    /// The first frame of each of the sprites of `vfs`, in the order of their images, put on the GPU by
    /// `screen` with their uploads recorded in `commands`. Errors: SpriteUnreadable, GpuFailed.
    [[nodiscard]] auto load(const nx::assets::Vfs& vfs, nx::render::ScreenRenderer& screen,
                            nx::rhi::CommandList& commands) const
        -> nx::Result<std::vector<nx::render::ScreenImage>, HudError>;

private:
    std::vector<std::string> files;
};

} // namespace cs
