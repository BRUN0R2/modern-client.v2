// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The weapons a player has and the one it holds (docs/10-counter-strike-gameplay.md, section 7.3):
/// given, drawn, fired, reloaded and idling as Counter-Strike's are, the same on the server and in
/// the client's prediction (ReGameDLL_CS's AddPlayerItem, AddPrimaryAmmo, GiveAmmo, SelectItem,
/// SelectLastItem, DefaultDeploy, ItemPostFrame, the guns' PrimaryAttack and Fire, KickBack,
/// DefaultReload, ResetMaxSpeed and the weapons' WeaponIdle, GPL, consulted: docs/20 section 3).
///
/// The weapons' timers are the original's (UseDecrement): seconds left, set as an event happens and
/// counted down in float by each command's frame time once its weapon frame is over, so that a
/// delay lasts as many commands as the original's does (0.15 seconds, 16 at 100 a second).
///
/// Today: giving, choosing, drawing, idling, the guns' fire, accuracy, spread, kick and reload, their
/// alternates, the silencers and the Glock's burst, and the knife's attacks; what the shots and the
/// blades hit, and dropping, come next (TODO.md).
export module cs.shared:inventory;

import std;
import nx.core;
import nx.math;
import nx.collision;
import nx.game;
import :movement;
import :sounds;
import :weapons;

export namespace cs {

/// How fast a player runs with no weapon in hand.
inline constexpr nx::UnitsPerSecond kUnarmedSpeed{240.0F};
/// A weapon drawn first idles this long after it was drawn, and it neither fires nor is reloaded
/// for this long (DefaultDeploy): counted down.
inline constexpr nx::Seconds kIdleAfterDraw{1.5F};
inline constexpr nx::Seconds kAttackAfterDraw{0.75F};

/// Whether a weapon given heavier than the one the player holds is drawn: the player's
/// `_cl_autowepswitch`.
enum class AutoSwitch : std::uint8_t {
    Off,
    ToHeavier,
};

/// A weapon a player has, the rounds in its clip, and how it fires now.
struct OwnedWeapon {
    WeaponIndex weapon{};
    std::uint32_t clip{}; ///< 0 for a weapon that fires no ammo.
    float accuracy{};     ///< What its spread is made of (m_flAccuracy).
    /// The shots of its burst, or since its button went down for a gun that fires once a press
    /// (m_iShotsFired).
    std::uint32_t shotsFired{};
    std::optional<nx::GameTime> lastShot{}; ///< None before its first (m_flLastFire).
    /// Until it may fire again: at 0 and below (m_flNextPrimaryAttack), counted down to -1 second.
    nx::Seconds nextShot{};
    /// While its button is up, its burst loses a shot from then on (m_flDecreaseShotsFired).
    nx::GameTime fewerShotsAt{};
    bool isBursting{};  ///< It fired since its button went up (m_bDelayFire).
    bool kicksLeft{};   ///< The side its next kick turns the view to (m_iDirection).
    bool isAlternate{}; ///< Its alternate is on: its silencer, its burst (m_iWeaponState).
    /// Until `+attack2` may act again (m_flNextSecondaryAttack), counted down to -0.001 second.
    nx::Seconds nextSecondary{};
    std::uint32_t slashes{}; ///< The slashes it made, which play their sequences in turn (m_iSwing).
    std::uint32_t stabs{};
    std::uint32_t followUpsLeft{}; ///< The rounds its last shot still fires, one a frame.
};

/// What a player's view model plays: a sequence of the weapon it holds, since when.
struct ViewAnimation {
    WeaponIndex weapon{};
    std::uint32_t sequence{};
    nx::GameTime start{};
};

/// What a player's weapon fires from and how its holder moves.
struct Shooter {
    nx::Vec3 eye{};
    nx::EulerAngles view{}; ///< The command's.
    /// The kick to the view (punchangle): shots aim along the view turned by it, and add to it.
    nx::EulerAngles punch{};
    nx::UnitsPerSecond horizontalSpeed{};
    bool isOnGround{};
    bool isDucked{};
    /// Whether `Attack` fires or strikes: not while the round is frozen, as the original's
    /// m_bCanShoot and IsFreezePeriod hold it back (`Attack2` still switches and stabs).
    bool canAttack = true;
};

/// What a weapon's attack met: its trace, and whether what stopped it is a body, which sounds as flesh:
/// an entity of a class other than none and the machines' (Classify), a hostage today.
struct WeaponHit {
    nx::collision::Trace trace{};
    bool isBody = false;
};

/// How a weapon's attacks meet what is solid: the server's way for a hull from a start to an end
/// (UTIL_TraceLine, UTIL_TraceHull), and what it met.
using WeaponTracer = std::function<WeaponHit(nx::collision::Hull hull, nx::Vec3 start, nx::Vec3 end)>;

/// A shot fired: from where, the way it goes, how far its bullet may stray, and how far it goes.
struct Shot {
    nx::Vec3 origin{};
    /// Forward, strayed by at most twice `spread` to the right and up, at random: not of length 1,
    /// as the original's (FireBullets3).
    nx::Vec3 direction{};
    float spread{};
    /// Its weapon's (WeaponFiring::reach), along `direction`: its end is `origin` plus `direction` times
    /// it, as the original's is.
    nx::HammerUnits reach{};
};

/// The weapons a player has, the rounds it carries besides their clips, and the one it holds.
class Inventory {
public:
    /// An inventory of none of the weapons of `catalog`, which must outlive it.
    inline explicit Inventory(const WeaponCatalog& catalog) noexcept : m_catalog{&catalog} {}

    /// Gives the player the weapon the catalog makes for `asked` (its replacement, or itself), which
    /// must be complete, with a clip's worth of rounds, in its clip (AddPlayerItem); and the player
    /// draws it at `time` when it holds none, or when `autoSwitch` is ToHeavier and it is heavier than
    /// the one it holds (FShouldSwitchWeapon). A weapon it has already gives its rounds alone, to its
    /// clip when empty, else besides it (ExtractAmmo). Returns whether the player had no such weapon.
    auto give(WeaponIndex asked, AutoSwitch autoSwitch, const nx::game::TickTime& time, nx::Random& random)
        -> bool;

    /// Gives `rounds` of `ammo` besides the clips, as many as the player can carry of it: returns how
    /// many it took.
    auto giveAmmo(std::string_view ammo, std::uint32_t rounds) -> std::uint32_t;
    /// Takes every weapon and every round from the player (RemoveAllItems): it holds none, and its
    /// view model shows nothing. The sounds its weapons made stay, to be taken.
    void removeAll();

    /// The part of `command` before it moves the player (CmdStart): the player draws the weapon it
    /// selects, or else that weapon's replacement, at `time` when it has it and does not hold it
    /// (SelectItem). The random numbers this draws are the command number's.
    void select(const UserCommand& command, const nx::game::TickTime& time);

    /// The weapon held after `command` moved the player, at `time` (ItemPostFrame), `shooter` its
    /// holder: nothing while it is being drawn or reloaded; a reload over fills its clip; with
    /// `Attack` down it fires when its cycle allows (a click on an empty clip, and once a press for a
    /// gun that is not automatic), its accuracy and spread as it moves, kicking the shooter's view, a
    /// shot's follow-up rounds one a frame after it; with `Attack2` down it switches to its alternate
    /// and back when it may, before it fires; a melee weapon slashes with `Attack` and stabs with
    /// `Attack2`, meeting what `tracer` finds;
    /// with `Reload` down it is reloaded; with no fire button down its burst wanes (a pistol's at
    /// once), an empty clip is reloaded, or it idles; then the weapons' timers count down by the
    /// command's duration. With `Attack` down and its cycle allowing, a weapon whose shooter may not
    /// attack does nothing more. Returns the shots it fired. The random numbers this draws are the
    /// command number's.
    auto frame(const UserCommand& command, Shooter& shooter, const WeaponTracer& tracer,
               const nx::game::TickTime& time) -> std::vector<Shot>;

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
    /// Whether the weapon held is being reloaded.
    [[nodiscard]] inline auto isReloading() const noexcept -> bool { return m_isReloading; }
    /// How fast the weapon the player holds lets it run, kUnarmedSpeed with none (ResetMaxSpeed).
    [[nodiscard]] auto maxSpeed() const noexcept -> nx::UnitsPerSecond;

    /// The sounds the player's weapons made since the last call, in their order, from the catalog, each
    /// with the moment of its tick: as one was drawn, fired, clicked empty, missed or met the world.
    [[nodiscard]] auto takeSounds() -> std::vector<SoundMade>;

private:
    /// Where the player's `weapon` is among its weapons; none.
    [[nodiscard]] auto find(WeaponIndex weapon) const noexcept -> std::optional<std::size_t>;
    /// Gives `rounds` to the player's weapon at `index`: to its clip when empty, up to its size, and
    /// the rest besides it (AddPrimaryAmmo).
    void takeRounds(std::size_t index, std::uint32_t rounds);
    /// The player draws its weapon at `index` at `time`, the one it held becoming the one before
    /// (Holster, Deploy, DefaultDeploy).
    void draw(std::size_t index, const nx::game::TickTime& time, nx::Random& random);
    /// The held `owned`, of `weapon`, fires at `time` (PrimaryAttack, and the guns' Fire): its shot, if
    /// a round left it.
    auto fire(OwnedWeapon& owned, const Weapon& weapon, Shooter& shooter, std::uint32_t seed,
              const nx::game::TickTime& time, nx::Random& random) -> std::optional<Shot>;
    /// The held `owned`, of `weapon`, fires the next of its last shot's follow-up rounds at `time`
    /// (FireRemaining): its shot, if a round was left for it.
    auto fireFollowUp(OwnedWeapon& owned, const Weapon& weapon, const Shooter& shooter, std::uint32_t seed,
                      const nx::game::TickTime& time) -> std::optional<Shot>;
    /// The held `owned`, of `weapon`, switches to its alternate or back at `time` (SecondaryAttack).
    void switchAlternate(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time);
    /// The held `owned` makes `attack`, the `count`th of its kind, at `time`, meeting what `tracer`
    /// finds ahead of `shooter` (the knife's Swing, Stab).
    void strike(OwnedWeapon& owned, const MeleeAttack& attack, std::uint32_t& count, const Shooter& shooter,
                const WeaponTracer& tracer, const nx::game::TickTime& time);
    /// The held `owned`, of `weapon`, is reloaded at `time` when a round can go into its clip
    /// (DefaultReload, and the guns' Reload).
    void reload(OwnedWeapon& owned, const Weapon& weapon, const nx::game::TickTime& time, nx::Random& random);
    /// The weapon the player holds idles at `time` when its wait is over (WeaponIdle).
    void idle(const nx::game::TickTime& time, nx::Random& random);
    /// Counts the timers down by `frameTime`, as the original's PostThink does once the weapon frame
    /// is over.
    void countDown(nx::Seconds frameTime) noexcept;
    /// Adds `sound`, when there is one, to those the weapons made, at `time`'s tick.
    void makeSound(const std::optional<GameSound>& sound, const nx::game::TickTime& time);

    const WeaponCatalog* m_catalog;
    std::vector<OwnedWeapon> m_weapons; ///< In the order the player was given them.
    std::map<std::string, std::uint32_t, std::less<>> m_reserves;
    std::optional<std::size_t> m_held;
    std::optional<std::size_t> m_before; ///< The one the player held before.
    std::optional<ViewAnimation> m_animation;
    /// Until the weapon held idles (m_flTimeWeaponIdle), counted down to -0.001 second.
    nx::Seconds m_nextIdle{};
    /// Until the weapon held does anything (the player's m_flNextAttack), counted down to -0.001
    /// second.
    nx::Seconds m_nextAttack{};
    bool m_isReloading = false;      ///< (m_fInReload)
    std::vector<SoundMade> m_sounds; ///< Made since takeSounds.
};

} // namespace cs
