// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The muzzle flashes of the weapon in the player's view (docs/05-rendering.md, section 9): what the
/// client events of its view model's sequences show as they play (ViewModelEvents); a muzzle
/// flash's, 5001, 5011, 5021 and 5031, at the view model's first to fourth attachment (as
/// GoldSrc's model events are documented): one of sprites/muzzleflash1.spr to 3, chosen and sized by
/// the event's number, added in full, at a frame and rolled at random, shown a hundredth of a second
/// (R_MuzzleFlash).
///
/// Today: the muzzle flashes; the light they cast and the shells come with what draws them (TODO.md).
export module cs.client:muzzleFlash;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;

export namespace cs {

/// A muzzle flash shows this long.
inline constexpr nx::Seconds kMuzzleFlashTime{0.01F};

/// The attachment the muzzle flash event `event` shows its flash at: 5001 the first, 5011 the second,
/// 5021 the third, 5031 the fourth; none for any other event.
[[nodiscard]] auto muzzleFlashAttachmentOf(std::int32_t event) noexcept -> std::optional<std::uint32_t>;

/// A muzzle flash shown: where, which sprite, how big, its roll and frame, and until when.
struct MuzzleFlash {
    nx::Vec3 origin{};
    std::uint32_t sprite{}; ///< Of sprites/muzzleflash1.spr to 3.
    float scale{};
    nx::Degrees roll{};
    float frame{};
    nx::Seconds until{}; ///< The map's time it shows until.
};

/// The muzzle flash an event whose options are `options` shows at `origin` at `time`, the frames of
/// each sprite `frameCounts`: the sprite of the number's units, modulo 3; a tenth of its tens in size,
/// half when none; a frame at random; rolled at random, by 20 degrees at most for the first sprite, a
/// rifle's (R_MuzzleFlash). Options that are not a whole number of 0 or more are 0.
[[nodiscard]] auto muzzleFlashOf(std::string_view options, nx::Vec3 origin, nx::Seconds time,
                                 std::span<const std::size_t> frameCounts, nx::Random& random) -> MuzzleFlash;

/// sprites/muzzleflash1.spr to 3, which the muzzle flashes show.
class MuzzleFlashSprites {
public:
    static constexpr std::size_t kCount = 3;

    /// Reads them from `vfs`; one missing or unreadable is left out, its flashes not shown, and listed.
    [[nodiscard]] static auto read(const nx::assets::Vfs& vfs) -> MuzzleFlashSprites;

    MuzzleFlashSprites(MuzzleFlashSprites&& other) noexcept = default;
    auto operator=(MuzzleFlashSprites&& other) noexcept -> MuzzleFlashSprites& = default;
    MuzzleFlashSprites(const MuzzleFlashSprites&) = delete;
    auto operator=(const MuzzleFlashSprites&) -> MuzzleFlashSprites& = delete;
    ~MuzzleFlashSprites() = default;

    /// The files left out, with why.
    [[nodiscard]] inline auto missing() const noexcept -> std::span<const std::string> { return m_missing; }

    /// Each sprite on the GPU of `renderer`, its uploads recorded in `commands`; none for one left
    /// out. Errors: those of SpriteRenderer::load.
    [[nodiscard]] auto load(nx::render::SpriteRenderer& renderer, nx::rhi::CommandList& commands) const
        -> nx::Result<std::vector<std::optional<nx::render::SpriteModel>>, nx::render::SpriteError>;

private:
    /// A sprite file: its bytes, which its view points into.
    struct File {
        std::vector<std::byte> bytes{};
        std::optional<nx::assets::SpriteView> view{};
    };

    MuzzleFlashSprites() = default;

    std::array<std::unique_ptr<File>, kCount> m_files; ///< None for one left out.
    std::vector<std::string> m_missing;
};

} // namespace cs
