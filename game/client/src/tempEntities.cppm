// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The client's temporary entities (docs/05-rendering.md, section 9): sprites shown a while where the
/// game tells of an effect, moving on their own, as the original's client makes them of the server's
/// temporary-entity messages. Today the blood a hit sprays (cs::BloodSpray, TE_BLOODSPRITE), as
/// Xash3D-FWGS's R_BloodSprite makes it (GPL, consulted: docs/20 section 3): a spray that plays its
/// frames once, and drops that fly off, fall, stop where they meet the world and go. How the
/// original's client moves them (its HUD_TempEntUpdate is closed) is the project's own: each falls at
/// its share of the world's gravity, spins, and rests where a line from where it was meets the world.
/// kRenderFxClampMinScale, which the original sets on them and no GPL renderer reads, is left out until
/// it is measured on the original (TODO.md).
export module cs.client:tempEntities;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.collision;
import nx.render;
import cs.shared;
import :effectSprites;

export namespace cs {

/// The sprite a temporary entity shows, of an effect's.
enum class TempSprite : std::uint8_t {
    BloodSpray,
    BloodDrop,
    Count,
};

/// A temporary entity: the sprite it shows, where it is and how it moves (the share of the world's
/// gravity it falls at, whether the world stops it), its roll and how fast it spins, the frame it shows,
/// how fast it plays its frames and its last, its scale, its color and how opaque it is, and when it
/// goes, in the time of the map.
struct TempEntity {
    TempSprite sprite = TempSprite::BloodSpray;
    nx::Vec3 origin{};
    nx::Vec3 velocity{};
    nx::Scale gravity{};
    bool isStoppedByWorld = false;
    nx::Degrees roll{};
    nx::DegreesPerSecond spin{};
    float frame = 0.0F;
    nx::PerSecond framerate{};
    float lastFrame = 0.0F;
    float scale = 0.0F;
    nx::render::RenderColor color{};
    std::uint8_t amount = 0;
    nx::Seconds goesAt{};
};

/// How many frames the blood's sprites have: none of one the level could not read, which is not shown.
struct BloodSpriteFrames {
    std::optional<std::uint32_t> spray{};
    std::optional<std::uint32_t> drop{};
};

/// The path of the file of `sprite`, as `look` names it.
[[nodiscard]] auto pathOf(TempSprite sprite, const BloodSprayLook& look) noexcept -> std::string_view;

/// How many frames the blood's sprites of `look` have among `sprites`, which read them.
[[nodiscard]] auto bloodSpriteFramesOf(const EffectSprites& sprites, const BloodSprayLook& look)
    -> BloodSpriteFrames;

/// The temporary entities of a map's client.
class TempEntities {
public:
    /// None yet, their chances drawn from a stream of `seed`.
    explicit TempEntities(std::uint64_t seed);

    /// Shows `spray` from `now` as `look` makes it (R_BloodSprite), its colors of `palette`: a spray of
    /// the blood's own sprite, a little above where it met the body, playing its frames once; and its
    /// drops, flying off from there. Neither of a sprite of `frames` the level has none of.
    void sprayBlood(const BloodSpray& spray, const BloodSprayLook& look, const nx::assets::Palette& palette,
                    const BloodSpriteFrames& frames, nx::Seconds now);

    /// Moves them over `elapsed` up to `now`, the world's gravity `gravity`, the world of `hulls`
    /// stopping those it stops: each plays its frames up to its last, spins, moves by its velocity,
    /// resting where a line from where it was meets the world, then falls its share of the gravity.
    /// Those gone at `now` are forgotten first.
    void update(nx::Seconds now, nx::Seconds elapsed, const nx::collision::BrushHulls& hulls,
                nx::UnitsPerSecondSquared gravity);

    /// Those shown, in the order they came.
    [[nodiscard]] inline auto shown() const noexcept -> std::span<const TempEntity> { return entities; }

private:
    std::vector<TempEntity> entities;
    nx::Random random;
};

} // namespace cs
