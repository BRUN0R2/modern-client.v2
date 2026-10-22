// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The weapons a player has and the one it holds (docs/10-counter-strike-gameplay.md, section 7.3):
/// given, drawn and idling as Counter-Strike's are, the same on the server and in the client's
/// prediction (ReGameDLL_CS's AddPlayerItem, AddPrimaryAmmo, GiveAmmo, SelectItem, SelectLastItem,
/// DefaultDeploy, ResetMaxSpeed and the weapons' WeaponIdle, GPL, consulted: docs/20 section 3).
///
/// Today: giving, choosing, drawing and idling; firing, reloading and dropping come next (TODO.md).
export module cs.shared:inventory;

import std;
import nx.core;
import nx.game;
import :movement;
import :weapons;

export namespace cs {

/// How fast a player runs with no weapon in hand.
inline constexpr nx::UnitsPerSecond kUnarmedSpeed{240.0F};
/// A weapon drawn first idles this long after it was drawn (DefaultDeploy).
inline constexpr nx::Seconds kIdleAfterDraw{1.5F};

/// A weapon a player has, and the rounds in its clip.
struct OwnedWeapon {
    WeaponIndex weapon{};
    std::uint32_t clip{}; ///< 0 for a weapon that fires no ammo.
};

/// What a player's view model plays: a sequence of the weapon it holds, since when.
struct ViewAnimation {
    WeaponIndex weapon{};
    std::uint32_t sequence{};
    nx::GameTime start{};
};

/// The weapons a player has, the rounds it carries besides their clips, and the one it holds.
class Inventory {
public:
    /// An inventory of none of the weapons of `catalog`, which must outlive it.
    explicit Inventory(const WeaponCatalog& catalog) noexcept : m_catalog{&catalog} {}

    /// Gives the player `weapon` with a clip's worth of rounds, in its clip (AddPlayerItem); and the
    /// player draws it at `time` when it holds none, or when `autoSwitch` (the player's
    /// _cl_autowepswitch) and it is heavier than the one it holds (FShouldSwitchWeapon). A weapon it
    /// has already gives its rounds alone, to its clip when empty, else besides it (ExtractAmmo).
    /// Returns whether the player had no such weapon.
    auto give(WeaponIndex weapon, bool autoSwitch, const nx::game::TickTime& time, nx::Random& random)
        -> bool;

    /// Gives `rounds` of `ammo` besides the clips, as many as the player can carry of it: returns how
    /// many it took.
    auto giveAmmo(std::string_view ammo, std::uint32_t rounds) -> std::uint32_t;

    /// The weapons' part of `command`, at `time`: the player draws the weapon it selects when it has
    /// it and does not hold it (SelectItem), and then the one it holds idles when its wait is over
    /// (WeaponIdle). The random numbers this draws are the command number's.
    void play(const UserCommand& command, const nx::game::TickTime& time);

    /// The weapon `lastinv` selects (SelectLastItem): the one the player held before, or else the
    /// newest of the first of its primary, secondary and knife slots whose newest it does not hold;
    /// none.
    [[nodiscard]] auto lastWeapon() const -> std::optional<WeaponIndex>;

    [[nodiscard]] inline auto weapons() const noexcept -> std::span<const OwnedWeapon> { return m_weapons; }
    /// The weapon the player holds; none.
    [[nodiscard]] auto held() const noexcept -> const OwnedWeapon*;
    /// The rounds of `ammo` the player carries besides the clips.
    [[nodiscard]] auto reserve(std::string_view ammo) const noexcept -> std::uint32_t;
    /// What the player's view model plays; none while it holds no weapon.
    [[nodiscard]] inline auto animation() const noexcept -> const std::optional<ViewAnimation>& {
        return m_animation;
    }
    /// How fast the weapon the player holds lets it run, kUnarmedSpeed with none (ResetMaxSpeed).
    [[nodiscard]] auto maxSpeed() const noexcept -> nx::UnitsPerSecond;

private:
    /// Where the player's `weapon` is among its weapons; none.
    [[nodiscard]] auto find(WeaponIndex weapon) const noexcept -> std::optional<std::size_t>;
    /// Gives `rounds` to the player's weapon at `index`: to its clip when empty, up to its size, and
    /// the rest besides it (AddPrimaryAmmo).
    void takeRounds(std::size_t index, std::uint32_t rounds);
    /// The player draws its weapon at `index` at `time`, the one it held becoming the one before
    /// (Deploy, DefaultDeploy).
    void draw(std::size_t index, const nx::game::TickTime& time, nx::Random& random);
    /// The weapon the player holds idles at `time` when its wait is over (WeaponIdle).
    void idle(const nx::game::TickTime& time, nx::Random& random);

    const WeaponCatalog* m_catalog;
    std::vector<OwnedWeapon> m_weapons; ///< In the order the player was given them.
    std::map<std::string, std::uint32_t, std::less<>> m_reserves;
    std::optional<std::size_t> m_held;
    std::optional<std::size_t> m_before; ///< The one the player held before.
    std::optional<ViewAnimation> m_animation;
    nx::GameTime m_nextIdle{};
};

} // namespace cs
