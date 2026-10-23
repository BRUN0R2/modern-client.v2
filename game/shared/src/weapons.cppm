// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The weapons of Counter-Strike as data (docs/10-counter-strike-gameplay.md, section 7.2): what each
/// is, the slot it takes, who buys it and for how much, the ammo it fires and how much of it its clip
/// holds, how fast its holder runs, the models it is seen by, how its view model moves as it is
/// drawn and held, and how it fires and is reloaded: its cycle, its accuracy, its spread and its
/// recoil. The game's own are in data/cstrike/weapons/<weapon>.toml and data/cstrike/ammo.toml,
/// with ReGameDLL_CS's values (GPL, consulted: docs/20 section 3).
///
/// A weapon is a type, standalone (docs/10, section 7.5): the game's are read from data, others are
/// made as the game runs, from nothing or from another's values, copied once; its values are set by
/// the key paths of its data ("firing.cycle"), and the game makes another wherever it would make it
/// once it is replaced. The catalog has no limit of weapons nor of kinds of ammo, as the original's
/// 32 of each.
///
/// Today: the guns' fire as it starts, the silencers, the Glock's burst and the knife's attacks; their
/// damage and how far it reaches come next (TODO.md).
export module cs.shared:weapons;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :sounds;

export namespace cs {

/// What can be wrong with the weapons' data.
enum class WeaponError : std::uint8_t {
    DataInvalid,        ///< A file breaks its schema or the catalog's rules (cause: the DataError or the
                        ///< VfsError, when there is one); the detail names the file, the weapon or the ammo.
    IdInvalid,          ///< A new weapon's id is empty, or not letters and digits alone.
    IdTaken,            ///< A weapon of that id, in any case, is in the catalog already.
    VarReadOnly,        ///< Its id, classname, slot or position: what the catalog knows the weapon by.
    VarInvalid,         ///< No value at that key path, one its type refuses (cause: the DataError), or one
                        ///< that leaves a complete weapon incomplete (cause: DataInvalid).
    Incomplete,         ///< A weapon whose values do not make a weapon yet (cause: DataInvalid).
    ReplacementInvalid, ///< A weapon replaced by itself, by a weapon replaced in its turn, or by an
                        ///< incomplete one.
};

/// The slots of a player's weapons, in the order the HUD's weapon menu numbers them, 1 to 5.
enum class WeaponSlot : std::uint8_t {
    Primary,   ///< Rifles, shotguns, submachine guns, sniper rifles and the machine gun.
    Secondary, ///< Pistols.
    Knife,
    Grenade,
    Bomb,
    Count,
};

/// A hand.
enum class Hand : std::uint8_t {
    Left,
    Right,
    Count,
};

/// Who may buy a weapon.
enum class Buyers : std::uint8_t {
    Terrorists,
    CounterTerrorists,
    Everyone,
    Count,
};

/// A picture of a sprite: the sprite, a path of the game's files, and the rectangle of its first
/// frame the picture is, in pixels.
struct SpritePicture {
    std::string sprite{};
    std::uint32_t x{};
    std::uint32_t y{};
    std::uint32_t width{};
    std::uint32_t height{};
};

/// A kind of ammo: how much of it a player carries besides what the clips of its weapons hold, how
/// buying sells it, and the picture the HUD shows of it.
struct AmmoType {
    std::string id{};           ///< The original's name for it: "9mm", "762Nato".
    std::uint32_t maxCarried{}; ///< A player carries this many rounds of it at most.
    nx::Money boxPrice{};       ///< A box of it costs this much,
    std::uint32_t boxRounds{};  ///< and holds this many rounds.
    /// Beside its rounds on the HUD, for screens 640 wide and more: the `ammo` picture of the lists
    /// of the original's weapons that fire it (sprites/weapon_<name>.txt).
    SpritePicture picture{};
};

/// data/cstrike/ammo.toml: the kinds of ammo.
struct AmmoTypes {
    std::vector<AmmoType> ammo{};
};

/// How a weapon is sold.
struct WeaponPurchase {
    Buyers buyers{};
    nx::Money price{};
};

/// The ammo a weapon fires, and how much of it its clip holds.
struct WeaponMagazine {
    std::string ammo{}; ///< The id of an AmmoType.
    std::uint32_t clipSize{};
};

/// An idle of a weapon's view model: the sequence it plays, and how long before it idles again.
struct WeaponIdle {
    std::uint32_t sequence{}; ///< By its number in the view model.
    nx::Seconds wait{};
    float share{}; ///< How likely it is chosen, from 0 to 1: the shares of a weapon's idles add up to 1.
};

/// How a weapon's view model moves as it is drawn and held, and its declared static appearance.
struct WeaponView {
    std::vector<std::uint32_t> drawSequences{}; ///< One of them, at random, as it is drawn.
    /// One of them, at random by their shares, once it is drawn and each time its wait is over.
    std::vector<WeaponIdle> idles{};
    bool idlesWhenEmpty{}; ///< Whether it idles with its clip empty: the pistols do not.
    /// As it is drawn, besides those its view model's sequence plays (the knife's Deploy).
    std::optional<GameSound> drawSound{};
    /// Its fixed skin family, zero-based; none selects the first. Only the weapon's main view may set it.
    std::optional<std::uint32_t> skin{};
    /// Its fixed choices by body-part index, zero-based; omitted parts select their first choice.
    /// Only the weapon's main view may set them; none is the original's empty selection.
    std::optional<std::vector<std::uint32_t>> bodyChoices{};
};

/// A part of a shot's spread: `base`, plus `scale` times the weapon's inaccuracy.
struct SpreadTerm {
    float base{};
    float scale{};
};

/// How far a weapon's shots spread as its holder moves: in the air, else moving faster than
/// `movingAbove`, else ducked (as standing when not given), else standing (the guns' PrimaryAttack).
struct WeaponSpread {
    nx::UnitsPerSecond movingAbove{};
    SpreadTerm air{};
    SpreadTerm moving{};
    std::optional<SpreadTerm> ducked{};
    SpreadTerm standing{};
};

/// An accuracy each shot of a burst worsens: the shots of the burst cubed over `divisor`, in whole
/// numbers as the original divides them, plus `offset`, at most `maximum`. The weapon's inaccuracy
/// is the accuracy itself.
struct BurstAccuracy {
    std::uint32_t divisor{};
    float offset{};
    float maximum{};
};

/// An accuracy the time between shots restores: each shot after the first takes (`threshold` less
/// the time since the one before) times `rate` from it, keeping it within `minimum` and `maximum`.
/// The weapon's inaccuracy is 1 less the accuracy.
struct PacedAccuracy {
    nx::Seconds threshold{};
    float rate{};
    float minimum{};
    float maximum{};
};

/// How a weapon's accuracy changes as it fires: by bursts or by pace, one of them.
struct WeaponAccuracy {
    float initial{}; ///< As it is given, drawn and reloaded.
    std::optional<BurstAccuracy> burst{};
    std::optional<PacedAccuracy> paced{};
};

/// The kick a shot gives its holder's view (KickBack): up by `upBase`, and by `upModifier` more a
/// shot of the burst after the first, to `upMax` at most; to its side likewise; the side it kicks to
/// turned over once in `directionChange` + 1 shots, at random.
struct Kick {
    float upBase{};
    float lateralBase{};
    float upModifier{};
    float lateralModifier{};
    float upMax{};
    float lateralMax{};
    std::uint32_t directionChange{};
};

/// A weapon's kicks as its holder moves: moving, else in the air, else ducked, else standing.
struct KickByMovement {
    Kick moving{};
    Kick air{};
    Kick ducked{};
    Kick standing{};
};

/// How a weapon's shots kick its holder's view: by its kicks, or up by `punch` alone; with neither,
/// not at all.
struct WeaponRecoil {
    std::optional<KickByMovement> kicks{};
    std::optional<nx::Degrees> punch{};
};

/// The rounds a shot fires after it on its own, one a weapon frame (the Glock's burst: FireRemaining).
struct FollowUps {
    std::uint32_t rounds{};
    float spread{}; ///< Each strays this much, whatever the accuracy and the movement.
};

/// How a gun fires.
struct WeaponFiring {
    nx::Seconds cycle{}; ///< From a shot to the next.
    bool isAutomatic{};  ///< Holding its button fires again; else each press fires once.
    nx::Seconds idleAfterShot{};
    /// Its view model's sequences, by their numbers: one at random as it fires, and the one as it
    /// fires its clip's last round, when it has one. Its client chooses them, which is to be measured
    /// on the original (TODO.md).
    std::vector<std::uint32_t> shootSequences{};
    std::optional<std::uint32_t> lastShotSequence{};
    WeaponAccuracy accuracy{};
    WeaponSpread spread{};
    WeaponRecoil recoil{};
    std::optional<FollowUps> followUps{}; ///< None: a shot fires one round.
    /// As it fires a round, a follow-up's too: the original's client event plays it, which chooses
    /// among the files the original has, to be measured on it (TODO.md).
    std::optional<GameSound> sound{};
    std::optional<GameSound> emptySound{}; ///< As it clicks on an empty clip (PlayEmptySound).
};

/// How a weapon is reloaded (DefaultReload).
struct WeaponReload {
    nx::Seconds time{};
    std::vector<std::uint32_t> sequences{}; ///< One of them, at random, as it reloads.
};

/// How long a melee attack holds the weapon's attacks: `+attack`'s, and `+attack2`'s.
struct MeleeTiming {
    nx::Seconds primary{};
    nx::Seconds secondary{};
};

/// A melee attack (the knife's: Swing, Stab): it meets what is within `reach` straight ahead of the
/// eye, along the view without its kick, a line first, then a ducked player's hull; it plays its miss
/// or its hit sequences in turn, an attack after another, and holds the attacks as its miss or its
/// hit says; it idles `idleAfter` after it, or when it would have otherwise.
struct MeleeAttack {
    nx::HammerUnits reach{};
    std::vector<std::uint32_t> missSequences{};
    std::vector<std::uint32_t> hitSequences{};
    MeleeTiming miss{};
    MeleeTiming hit{};
    std::optional<nx::Seconds> idleAfter{};
    std::optional<GameSound> missSound{};
    /// As it meets the world; flesh comes with the players to meet.
    std::optional<GameSound> hitSound{};
};

/// A melee weapon's attacks: `+attack`'s slash, `+attack2`'s stab.
struct WeaponMelee {
    MeleeAttack slash{};
    MeleeAttack stab{};
};

/// What `+attack2` switches a weapon to and back: the USP's and the M4A1's silencers, the Glock's
/// burst. While on, its view, firing and reload are these, those not given the weapon's own.
struct WeaponAlternate {
    /// It switches again no sooner than this after a switch; one that plays a sequence neither fires
    /// nor idles meanwhile either.
    nx::Seconds switchTime{};
    std::optional<std::uint32_t> onSequence{};  ///< Its view model's, as it switches on.
    std::optional<std::uint32_t> offSequence{}; ///< And off.
    std::optional<WeaponView> view{};
    std::optional<WeaponFiring> firing{};
    std::optional<WeaponReload> reload{};
};

/// The models a weapon is seen by.
struct WeaponModels {
    std::string view{};   ///< In its holder's own view: models/v_<weapon>.mdl.
    std::string player{}; ///< In its holder's hands, as the others see it: models/p_<weapon>.mdl.
    /// What its holder's model plays as it holds it: the end of the labels of its sequences ("ak47"
    /// of "ref_aim_ak47").
    std::string playerAnimation{};
    /// The hand its view model holds it in: one modeled in the other hand than the player's is drawn
    /// mirrored. Counter-Strike's guns are modeled in the left hand, its knife in the right.
    Hand viewHand{};
};

/// A weapon: data/cstrike/weapons/<id>.toml.
struct Weapon {
    std::string id{};        ///< Its name, as the buy commands and its file call it: "ak47".
    std::string classname{}; ///< The class of the entity the game makes of it: "weapon_ak47".
    std::string name{};      ///< What the game shows it as: "CV-47".
    WeaponSlot slot{};
    std::uint32_t position{}; ///< The HUD's weapon menu lists a slot's weapons by it.
    /// When the game chooses a weapon for a player, as one runs out or is dropped, it chooses the
    /// heaviest of the others.
    std::uint32_t weight{};
    nx::UnitsPerSecond maxSpeed{};            ///< Its holder runs this fast at most.
    std::optional<WeaponPurchase> purchase{}; ///< None: it is not sold (the knife).
    std::optional<WeaponMagazine> magazine{}; ///< None: it fires no ammo (the knife).
    WeaponModels models{};
    WeaponView view{};
    std::optional<WeaponFiring> firing{};       ///< None: it fires nothing (the knife, until its attacks).
    std::optional<WeaponReload> reload{};       ///< None: it is not reloaded.
    std::optional<WeaponAlternate> alternate{}; ///< None: `+attack2` switches nothing.
    std::optional<WeaponMelee> melee{};         ///< None: it has no melee attacks.
};

/// A weapon of a catalog, by its place in it, which it keeps.
struct WeaponIndex {
    std::uint32_t value = 0;

    [[nodiscard]] inline auto operator==(const WeaponIndex&) const noexcept -> bool = default;
};

/// What a new weapon is made of: its id, its slot, and the weapon whose values it starts from.
struct WeaponDraft {
    std::string_view id{};
    WeaponSlot slot{};
    std::optional<WeaponIndex> from{}; ///< None: it starts from no values, incomplete.
};

/// Reads `text`, the file `sourceName`: the kinds of ammo, each with an id and its picture's sprite a
/// path of the game's files. Errors: DataInvalid.
[[nodiscard]] auto readAmmoTypes(std::string_view text, std::string_view sourceName)
    -> nx::Result<AmmoTypes, WeaponError>;

/// Reads `text`, the file `sourceName`: a weapon, its id, classname and name not empty, its models
/// paths of the game's files, a draw sequence and an idle at least, each idle's wait above 0 and the
/// idles' shares adding up to 1; firing and reloading only with a magazine, a shoot sequence and a
/// reload sequence at least, a cycle above 0, and one accuracy, by bursts or by pace; and the same of
/// its alternate's, which plays both its switch's sequences or neither; melee attacks with a miss and
/// a hit sequence at least, of a weapon with no alternate. Errors: DataInvalid.
[[nodiscard]] auto readWeapon(std::string_view text, std::string_view sourceName)
    -> nx::Result<Weapon, WeaponError>;

/// The game's weapons and kinds of ammo, each checked against the others; and the weapons made as
/// the game runs, each standalone. A weapon keeps its place, and its values their address, as
/// others are made.
class WeaponCatalog {
public:
    /// The catalog of `ammo` and `weapons`, in their order, all complete. Errors: DataInvalid: an id
    /// twice (of a kind of ammo, or of a weapon, in any case), a classname twice, two weapons at one
    /// position of a slot, a weapon firing a kind of ammo `ammo` lacks, or one readWeapon refuses.
    [[nodiscard]] static auto make(AmmoTypes ammo, std::vector<Weapon> weapons)
        -> nx::Result<WeaponCatalog, WeaponError>;

    [[nodiscard]] inline auto ammo() const noexcept -> std::span<const AmmoType> { return m_ammo; }
    [[nodiscard]] inline auto weapons() const noexcept -> const std::deque<Weapon>& { return m_weapons; }

    /// The kind of ammo `id` names; none when the catalog has none of that id.
    [[nodiscard]] auto findAmmo(std::string_view id) const noexcept -> const AmmoType*;
    /// The weapon `id` names, in any case ("drakariii" names "DrakarIII"); none when the catalog has
    /// none of that id.
    [[nodiscard]] auto findWeapon(std::string_view id) const noexcept -> const Weapon*;
    /// Where the weapon `id` names is, in any case; none when the catalog has none of that id.
    [[nodiscard]] auto indexOf(std::string_view id) const noexcept -> std::optional<WeaponIndex>;
    /// The weapon at `index`, which must be one of the catalog's.
    [[nodiscard]] auto weapon(WeaponIndex index) const noexcept -> const Weapon&;

    /// Makes the weapon `draft` describes, its classname weapon_<id>, at the next position of its
    /// slot, its name its id: with the values of the weapon it starts from, copied once and linked
    /// to it no more; or with none, incomplete until its values make a weapon. Errors: IdInvalid,
    /// IdTaken.
    [[nodiscard]] auto create(const WeaponDraft& draft) -> nx::Result<WeaponIndex, WeaponError>;

    /// Sets the value of `weapon` at `keyPath` ("magazine.clipSize") to `text`, read as its data file
    /// reads it (nx::data::assignField): a complete weapon stays complete, an incomplete one becomes
    /// complete when its values make a weapon. Errors: VarReadOnly, VarInvalid.
    [[nodiscard]] auto setVar(WeaponIndex weapon, std::string_view keyPath, std::string_view text)
        -> nx::Result<void, WeaponError>;
    /// The value of `weapon` at `keyPath`, as its data file writes it; none when it has none there.
    /// Errors: VarInvalid (no value at that key path).
    [[nodiscard]] auto var(WeaponIndex weapon, std::string_view keyPath) const
        -> nx::Result<std::optional<std::string>, WeaponError>;
    /// Every value of `weapon`, by its key path.
    [[nodiscard]] auto vars(WeaponIndex weapon) const -> std::vector<nx::data::FieldText>;

    /// Whether `weapon`'s values make a weapon, which the game can give.
    [[nodiscard]] auto isComplete(WeaponIndex weapon) const noexcept -> bool;
    /// What `weapon` lacks to be complete. Errors: Incomplete (cause: what readWeapon would refuse).
    [[nodiscard]] auto checkComplete(WeaponIndex weapon) const -> nx::Result<void, WeaponError>;
    /// How many times `weapon`'s values were set since it was made: what is made of them (its view
    /// model) is made again when it changes.
    [[nodiscard]] auto revision(WeaponIndex weapon) const noexcept -> std::uint32_t;

    /// The game makes `replacement` wherever it would make `original` (buying, a loadout, a map's
    /// entity, `give`), and selecting `original` selects it; none: `original` again. Errors:
    /// ReplacementInvalid (the replacement is `original`, is replaced in its turn, or incomplete).
    [[nodiscard]] auto replace(WeaponIndex original, std::optional<WeaponIndex> replacement)
        -> nx::Result<void, WeaponError>;
    /// The weapon the game makes for `weapon`: its replacement, or itself.
    [[nodiscard]] auto madeFor(WeaponIndex weapon) const noexcept -> WeaponIndex;

private:
    /// How a weapon of the catalog stands.
    struct Standing {
        bool isComplete = false;
        std::uint32_t revision = 0;
        std::optional<WeaponIndex> replacement{};
    };

    WeaponCatalog(std::vector<AmmoType> ammo, std::vector<Weapon> weapons) noexcept;

    /// DataInvalid when `weapon`, of `where` (a file, or the catalog), is not complete: what
    /// readWeapon refuses, or a magazine of a kind of ammo the catalog lacks.
    [[nodiscard]] auto check(const Weapon& weapon, std::string_view where) const
        -> nx::Result<void, WeaponError>;
    [[nodiscard]] auto standing(WeaponIndex weapon) const noexcept -> const Standing&;

    std::vector<AmmoType> m_ammo;
    std::deque<Weapon> m_weapons;      ///< A deque: making a weapon moves none of the others.
    std::vector<Standing> m_standings; ///< Of each weapon, by its index.
};

} // namespace cs

template <>
struct nx::meta::Description<cs::AmmoType> {
    static constexpr std::string_view kName = "cs::AmmoType";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::AmmoType::id>("id"),
        nx::meta::field<&cs::AmmoType::maxCarried>("maxCarried"),
        nx::meta::field<&cs::AmmoType::boxPrice>("boxPrice"),
        nx::meta::field<&cs::AmmoType::boxRounds>("boxRounds", nx::data::Range{.minimum = 1.0}),
        nx::meta::field<&cs::AmmoType::picture>("picture"),
    };
};

template <>
struct nx::meta::Description<cs::SpritePicture> {
    static constexpr std::string_view kName = "cs::SpritePicture";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::SpritePicture::sprite>("sprite"),
        nx::meta::field<&cs::SpritePicture::x>("x"),
        nx::meta::field<&cs::SpritePicture::y>("y"),
        nx::meta::field<&cs::SpritePicture::width>("width", nx::data::Range{.minimum = 1.0}),
        nx::meta::field<&cs::SpritePicture::height>("height", nx::data::Range{.minimum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::AmmoTypes> {
    static constexpr std::string_view kName = "cs::AmmoTypes";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::AmmoTypes::ammo>("ammo"),
    };
};

template <>
struct nx::meta::Description<cs::WeaponPurchase> {
    static constexpr std::string_view kName = "cs::WeaponPurchase";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponPurchase::buyers>("buyers"),
        nx::meta::field<&cs::WeaponPurchase::price>("price", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::WeaponMagazine> {
    static constexpr std::string_view kName = "cs::WeaponMagazine";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponMagazine::ammo>("ammo"),
        nx::meta::field<&cs::WeaponMagazine::clipSize>("clipSize", nx::data::Range{.minimum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::WeaponIdle> {
    static constexpr std::string_view kName = "cs::WeaponIdle";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponIdle::sequence>("sequence"),
        nx::meta::field<&cs::WeaponIdle::wait>("wait"),
        nx::meta::field<&cs::WeaponIdle::share>("share", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::WeaponView> {
    static constexpr std::string_view kName = "cs::WeaponView";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponView::drawSequences>("drawSequences"),
        nx::meta::field<&cs::WeaponView::idles>("idles"),
        nx::meta::field<&cs::WeaponView::idlesWhenEmpty>("idlesWhenEmpty"),
        nx::meta::field<&cs::WeaponView::drawSound>("drawSound"),
        nx::meta::field<&cs::WeaponView::skin>("skin"),
        nx::meta::field<&cs::WeaponView::bodyChoices>("bodyChoices"),
    };
};

template <>
struct nx::meta::Description<cs::SpreadTerm> {
    static constexpr std::string_view kName = "cs::SpreadTerm";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::SpreadTerm::base>("base", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::SpreadTerm::scale>("scale", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::WeaponSpread> {
    static constexpr std::string_view kName = "cs::WeaponSpread";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponSpread::movingAbove>("movingAbove", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::WeaponSpread::air>("air"),
        nx::meta::field<&cs::WeaponSpread::moving>("moving"),
        nx::meta::field<&cs::WeaponSpread::ducked>("ducked"),
        nx::meta::field<&cs::WeaponSpread::standing>("standing"),
    };
};

template <>
struct nx::meta::Description<cs::BurstAccuracy> {
    static constexpr std::string_view kName = "cs::BurstAccuracy";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BurstAccuracy::divisor>("divisor", nx::data::Range{.minimum = 1.0}),
        nx::meta::field<&cs::BurstAccuracy::offset>("offset"),
        nx::meta::field<&cs::BurstAccuracy::maximum>("maximum"),
    };
};

template <>
struct nx::meta::Description<cs::PacedAccuracy> {
    static constexpr std::string_view kName = "cs::PacedAccuracy";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::PacedAccuracy::threshold>("threshold", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::PacedAccuracy::rate>("rate"),
        nx::meta::field<&cs::PacedAccuracy::minimum>("minimum"),
        nx::meta::field<&cs::PacedAccuracy::maximum>("maximum"),
    };
};

template <>
struct nx::meta::Description<cs::WeaponAccuracy> {
    static constexpr std::string_view kName = "cs::WeaponAccuracy";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponAccuracy::initial>("initial"),
        nx::meta::field<&cs::WeaponAccuracy::burst>("burst"),
        nx::meta::field<&cs::WeaponAccuracy::paced>("paced"),
    };
};

template <>
struct nx::meta::Description<cs::Kick> {
    static constexpr std::string_view kName = "cs::Kick";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::Kick::upBase>("upBase"),
        nx::meta::field<&cs::Kick::lateralBase>("lateralBase"),
        nx::meta::field<&cs::Kick::upModifier>("upModifier"),
        nx::meta::field<&cs::Kick::lateralModifier>("lateralModifier"),
        nx::meta::field<&cs::Kick::upMax>("upMax", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::Kick::lateralMax>("lateralMax", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::Kick::directionChange>("directionChange"),
    };
};

template <>
struct nx::meta::Description<cs::KickByMovement> {
    static constexpr std::string_view kName = "cs::KickByMovement";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::KickByMovement::moving>("moving"),
        nx::meta::field<&cs::KickByMovement::air>("air"),
        nx::meta::field<&cs::KickByMovement::ducked>("ducked"),
        nx::meta::field<&cs::KickByMovement::standing>("standing"),
    };
};

template <>
struct nx::meta::Description<cs::WeaponRecoil> {
    static constexpr std::string_view kName = "cs::WeaponRecoil";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponRecoil::kicks>("kicks"),
        nx::meta::field<&cs::WeaponRecoil::punch>("punch"),
    };
};

template <>
struct nx::meta::Description<cs::FollowUps> {
    static constexpr std::string_view kName = "cs::FollowUps";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::FollowUps::rounds>("rounds", nx::data::Range{.minimum = 1.0}),
        nx::meta::field<&cs::FollowUps::spread>("spread", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::WeaponFiring> {
    static constexpr std::string_view kName = "cs::WeaponFiring";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponFiring::cycle>("cycle"),
        nx::meta::field<&cs::WeaponFiring::isAutomatic>("isAutomatic"),
        nx::meta::field<&cs::WeaponFiring::idleAfterShot>("idleAfterShot", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::WeaponFiring::shootSequences>("shootSequences"),
        nx::meta::field<&cs::WeaponFiring::lastShotSequence>("lastShotSequence"),
        nx::meta::field<&cs::WeaponFiring::accuracy>("accuracy"),
        nx::meta::field<&cs::WeaponFiring::spread>("spread"),
        nx::meta::field<&cs::WeaponFiring::recoil>("recoil"),
        nx::meta::field<&cs::WeaponFiring::followUps>("followUps"),
        nx::meta::field<&cs::WeaponFiring::sound>("sound"),
        nx::meta::field<&cs::WeaponFiring::emptySound>("emptySound"),
    };
};

template <>
struct nx::meta::Description<cs::WeaponReload> {
    static constexpr std::string_view kName = "cs::WeaponReload";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponReload::time>("time", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::WeaponReload::sequences>("sequences"),
    };
};

template <>
struct nx::meta::Description<cs::MeleeTiming> {
    static constexpr std::string_view kName = "cs::MeleeTiming";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::MeleeTiming::primary>("primary", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::MeleeTiming::secondary>("secondary", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::MeleeAttack> {
    static constexpr std::string_view kName = "cs::MeleeAttack";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::MeleeAttack::reach>("reach", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::MeleeAttack::missSequences>("missSequences"),
        nx::meta::field<&cs::MeleeAttack::hitSequences>("hitSequences"),
        nx::meta::field<&cs::MeleeAttack::miss>("miss"),
        nx::meta::field<&cs::MeleeAttack::hit>("hit"),
        nx::meta::field<&cs::MeleeAttack::idleAfter>("idleAfter", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::MeleeAttack::missSound>("missSound"),
        nx::meta::field<&cs::MeleeAttack::hitSound>("hitSound"),
    };
};

template <>
struct nx::meta::Description<cs::WeaponMelee> {
    static constexpr std::string_view kName = "cs::WeaponMelee";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponMelee::slash>("slash"),
        nx::meta::field<&cs::WeaponMelee::stab>("stab"),
    };
};

template <>
struct nx::meta::Description<cs::WeaponAlternate> {
    static constexpr std::string_view kName = "cs::WeaponAlternate";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponAlternate::switchTime>("switchTime", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::WeaponAlternate::onSequence>("onSequence"),
        nx::meta::field<&cs::WeaponAlternate::offSequence>("offSequence"),
        nx::meta::field<&cs::WeaponAlternate::view>("view"),
        nx::meta::field<&cs::WeaponAlternate::firing>("firing"),
        nx::meta::field<&cs::WeaponAlternate::reload>("reload"),
    };
};

template <>
struct nx::meta::Description<cs::WeaponModels> {
    static constexpr std::string_view kName = "cs::WeaponModels";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WeaponModels::view>("view"),
        nx::meta::field<&cs::WeaponModels::player>("player"),
        nx::meta::field<&cs::WeaponModels::playerAnimation>("playerAnimation"),
        nx::meta::field<&cs::WeaponModels::viewHand>("viewHand"),
    };
};

template <>
struct nx::meta::Description<cs::Weapon> {
    static constexpr std::string_view kName = "cs::Weapon";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::Weapon::id>("id"),
        nx::meta::field<&cs::Weapon::classname>("classname"),
        nx::meta::field<&cs::Weapon::name>("name"),
        nx::meta::field<&cs::Weapon::slot>("slot"),
        nx::meta::field<&cs::Weapon::position>("position"),
        nx::meta::field<&cs::Weapon::weight>("weight"),
        nx::meta::field<&cs::Weapon::maxSpeed>("maxSpeed", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::Weapon::purchase>("purchase"),
        nx::meta::field<&cs::Weapon::magazine>("magazine"),
        nx::meta::field<&cs::Weapon::models>("models"),
        nx::meta::field<&cs::Weapon::view>("view"),
        nx::meta::field<&cs::Weapon::firing>("firing"),
        nx::meta::field<&cs::Weapon::reload>("reload"),
        nx::meta::field<&cs::Weapon::alternate>("alternate"),
        nx::meta::field<&cs::Weapon::melee>("melee"),
    };
};
