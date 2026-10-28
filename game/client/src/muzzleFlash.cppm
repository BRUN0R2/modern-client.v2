// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The muzzle flashes of the weapon in the player's view (docs/05-rendering.md, section 9): what the
/// client events of its view model's sequences show as they play (ViewModelEvents); a muzzle
/// flash's, 5001, 5011, 5021 and 5031, at the view model's first to fourth attachment (as GoldSrc's
/// model events are documented). What it looks like is its style: the event's options, then the
/// weapon's data over them (cs::MuzzleFlashLook). The options are:
/// - the original's, a number: one of sprites/muzzleflash1.spr to 3, chosen and sized by it, at a
///   frame and rolled at random, shown a hundredth of a second (R_MuzzleFlash);
/// - Counter-Strike Online's, keys and their values after a '#' ("#I341 S0.16 R2 F0 P30 T999"): its
///   sprite sprites/muzzleflash<I>.spr, S its scale, played from frame F at P frames a second, once,
///   for T seconds at most (as its files are observed); turning counter-clockwise as it plays, R
///   degrees a frame of it (the Drakar III's R2 turns smoothly that way, as the player sees it in
///   Counter-Strike Online: the rate is the project's estimate until it is measured); the keys whose
///   meaning is unknown are not read.
/// A flash is anchored to its attachment: each frame shows it where that frame's bones put the
/// attachment, so that it stays at the muzzle as the weapon moves, however long it lasts (the
/// original's lasts a frame, where its temporary entity needs no following). Each event shows a flash of
/// its own, as the original makes a temporary entity of each: flashes overlapping at a muzzle add up,
/// so that Counter-Strike Online's, playing longer than a shot's cycle, burn brighter as it fires. The
/// sprites the styles show are read with the view model, before a frame shows them.
///
/// Today: the muzzle flashes, and the light a weapon's data gives them on the weapon in hand in the
/// enhanced look. The original's light of a shot on the world and its models (EF_MUZZLEFLASH) comes
/// with the dynamic lights, the shells with the temporary entities (TODO.md).
export module cs.client:muzzleFlash;

import std;
import nx.core;
import nx.math;
import nx.render;
import cs.shared;

export namespace cs {

/// An original's muzzle flash shows this long.
inline constexpr nx::Seconds kMuzzleFlashTime{0.01F};

/// The attachment the muzzle flash event `event` shows its flash at: 5001 the first, 5011 the second,
/// 5021 the third, 5031 the fourth; none for any other event.
[[nodiscard]] auto muzzleFlashAttachmentOf(std::int32_t event) noexcept -> std::optional<std::uint32_t>;

/// What a muzzle flash looks like and how it plays, every value given.
struct MuzzleFlashStyle {
    std::string sprite{};                      ///< A path of the game's files.
    float scale{};                             ///< Times its sprite's size; 0 is its own size.
    std::optional<std::uint32_t> firstFrame{}; ///< None: a frame at random.
    float frameRate{};               ///< The frames a second it plays on at from its first, once; 0 holds it.
    nx::Seconds time{};              ///< It shows this long at most.
    nx::Degrees roll{};              ///< It is rolled at random, by a whole number of degrees up to this,
    nx::DegreesPerSecond rollRate{}; ///< and turns this fast as it shows, counter-clockwise.
    float brightness = 1.0F;         ///< Its colors times this.
    std::optional<MuzzleFlashLight> light{};
};

/// The style of a muzzle flash event whose options are `options`, of a weapon whose data's look is
/// `look`. Options of the original: the sprite of the number's units, modulo 3; a tenth of its tens in
/// size, half when none; a frame at random, held; rolled by 20 degrees at most for the first sprite, a
/// rifle's, by 359 for the others; options that are not a whole number of 0 or more are 0.
/// Counter-Strike Online's: each key read over the original's of 0, a value that is not a number of 0
/// or more left out. Then each value `look` gives, over the event's.
[[nodiscard]] auto muzzleFlashStyleOf(std::string_view options, const std::optional<MuzzleFlashLook>& look)
    -> MuzzleFlashStyle;

/// Where a muzzle flash shows: an attachment of the view model of a weapon, wherever the bones of the
/// frame shown put it.
struct MuzzleFlashAnchor {
    WeaponIndex weapon{};
    std::uint32_t attachment{};

    [[nodiscard]] inline auto operator==(const MuzzleFlashAnchor&) const noexcept -> bool = default;
};

/// A muzzle flash shown: where, which sprite, how big, its roll, its frames, and when.
struct MuzzleFlash {
    MuzzleFlashAnchor anchor{};
    std::uint32_t sprite{}; ///< Of the MuzzleFlashSprites.
    float scale{};
    nx::Degrees roll{};              ///< As it shows first,
    nx::DegreesPerSecond rollRate{}; ///< turning this fast, counter-clockwise.
    float brightness = 1.0F;
    float firstFrame{};
    float frameRate{};
    std::size_t frameCount{}; ///< Its sprite's.
    nx::Seconds start{};      ///< The map's time it shows from,
    nx::Seconds until{};      ///< and until, at most.
    std::optional<MuzzleFlashLight> light{};

    /// The frame it shows at `time`; none once it is over: past its time, or past its last frame.
    [[nodiscard]] auto frameAt(nx::Seconds time) const noexcept -> std::optional<float>;
    /// Its roll at `time`, turned as it shows.
    [[nodiscard]] auto rollAt(nx::Seconds time) const noexcept -> nx::Degrees;
    /// The light it casts at `time` from `origin`, where its anchor is then, dimming to nothing over the
    /// light's time; none without one, or once that time is over.
    [[nodiscard]] auto lightAt(nx::Seconds time, nx::Vec3 origin) const noexcept
        -> std::optional<nx::render::StudioLight>;
    /// Whether neither it nor its light shows at `time`.
    [[nodiscard]] auto isOverAt(nx::Seconds time) const noexcept -> bool;

private:
    /// How much of its light is left at `time`, from 1 down; none without one, or once it is out.
    [[nodiscard]] auto lightLeftAt(nx::Seconds time) const noexcept -> std::optional<float>;
};

/// The muzzle flash of `style` shown at `anchor` from `time`, of sprite `sprite`, which has
/// `frameCount` frames: its roll, then its frame when the style leaves it to chance, drawn from
/// `random`. None when the sprite has no such frame.
[[nodiscard]] auto muzzleFlashOf(const MuzzleFlashStyle& style, std::uint32_t sprite, std::size_t frameCount,
                                 MuzzleFlashAnchor anchor, nx::Seconds time, nx::Random& random)
    -> std::optional<MuzzleFlash>;

/// Where the attachment of an anchor is in the frame shown; none when it is nowhere.
using AnchorPlace = std::function<std::optional<nx::Vec3>(const MuzzleFlashAnchor& anchor)>;

/// The muzzle flashes showing, each event's its own.
class MuzzleFlashes {
public:
    /// Shows `flash`, besides those showing.
    void show(MuzzleFlash flash);
    /// Forgets those over at `time`, and those of another weapon than `inHand`, the one the player
    /// holds; all of them when it holds none.
    void forget(nx::Seconds time, std::optional<WeaponIndex> inHand);

    [[nodiscard]] inline auto shown() const noexcept -> std::span<const MuzzleFlash> { return m_flashes; }
    /// The light the flashes cast at `time`, one a muzzle, the lights of its flashes added (lights at
    /// one place add up), from where `placeOf` puts its anchor; none from a muzzle it puts nowhere.
    [[nodiscard]] auto lightsAt(nx::Seconds time, const AnchorPlace& placeOf) const
        -> std::vector<nx::render::StudioLight>;

private:
    std::vector<MuzzleFlash> m_flashes;
};

} // namespace cs
