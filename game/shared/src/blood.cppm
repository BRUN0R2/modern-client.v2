// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The blood a hit sheds (docs/10-counter-strike-gameplay.md, section 7.4): data/cstrike/blood.toml,
/// with ReGameDLL_CS's way of it on the server (CBaseEntity::TraceBleed, UTIL_BloodDecalTrace,
/// UTIL_BloodDrips, BloodSplat; GPL, consulted: docs/20 section 3) and Xash3D-FWGS's of the spray and the
/// stream on the client (R_BloodSprite, R_BloodStream; GPL, consulted). Whether a hit sheds blood is the
/// player's armor's to tell (shedsBlood).
export module cs.shared:blood;

import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;

export namespace cs {

/// Why the blood's data could not be read.
enum class BloodError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// How many ways a hit of some damage traces the blood it sheds, each turned off the hit's way by up to
/// `noise` on each axis: those of a damage below `below`, or of any for none.
struct BleedSpread {
    std::optional<nx::Damage> below{};
    std::uint32_t traces = 0;
    float noise = 0.0F;
};

/// How the server tells its clients of the blood a hit sprays where it meets a body (UTIL_BloodDrips,
/// TE_BLOODSPRITE): the palette entry of its color (BLOOD_COLOR_RED), and its size: the damage's whole
/// points times amountPerDamage (the multiplayer rules'), mostAmount at most, over amountPerSize, from
/// smallestSize to largestSize.
struct BloodSprayRules {
    std::uint8_t color = 0;
    std::uint32_t amountPerDamage = 0;
    std::uint32_t mostAmount = 0;
    std::uint32_t amountPerSize = 0;
    std::uint32_t smallestSize = 0;
    std::uint32_t largestSize = 0;
};

/// How a client shows a spray of blood (R_BloodSprite): its sprites, the spray's and its drops', the
/// spray's color the palette entry of the spray's plus 1 to colorShift, the drops' the entry before it,
/// both as opaque as `amount` over 255. The spray, lifted from where the hit met the body by leastLift to
/// mostLift, its scale its size times leastSprayScale to mostSprayScale, plays its frames once in
/// sprayTime. Its drops, as many as its size and extraDropDice dice of extraDropFaces faces, their
/// scales its size times leastDropScale to mostDropScale, fly off across the ground at leastDropSpeed to
/// mostDropSpeed on each axis and up at leastDropRise to mostDropRise, spin at leastDropSpin to
/// mostDropSpin, fall at dropGravity of the world's gravity, stop where they meet the world, and go
/// after leastDropLife to mostDropLife, each on one of its sprite's frames from the second, the last for
/// one beyond mostDropFrame. All drawn at random, each roll a whole turn at most.
struct BloodSprayLook {
    std::string spraySprite;
    std::string dropSprite;
    std::uint8_t colorShift = 0;
    std::uint8_t amount = 0;
    nx::HammerUnits leastLift{};
    nx::HammerUnits mostLift{};
    nx::Scale leastSprayScale{};
    nx::Scale mostSprayScale{};
    nx::Seconds sprayTime{};
    std::uint32_t extraDropDice = 0;
    std::uint32_t extraDropFaces = 0;
    nx::Scale leastDropScale{};
    nx::Scale mostDropScale{};
    nx::UnitsPerSecond leastDropSpeed{};
    nx::UnitsPerSecond mostDropSpeed{};
    nx::UnitsPerSecond leastDropRise{};
    nx::UnitsPerSecond mostDropRise{};
    nx::DegreesPerSecond leastDropSpin{};
    nx::DegreesPerSecond mostDropSpin{};
    nx::Scale dropGravity{};
    nx::Seconds leastDropLife{};
    nx::Seconds mostDropLife{};
    std::uint32_t mostDropFrame = 0;
};

/// How the server tells its clients of the stream of blood a head without a helmet sheds where a hit met
/// it, the way the hit went (BloodSplat, TE_BLOODSTREAM): the palette entry of its color
/// (BLOOD_COLOR_DARKRED), and its speed in whole units a second: the damage times speedPerDamage, cut to
/// a whole number, and 0 to mostExtraSpeed more, `fastest` at most. The original writes the speed as a
/// byte that wraps past 255, so that a head shot's stream may be slow; ReGameDLL_CS holds it at 255
/// (UTIL_BloodStream), as here.
struct BloodStreamRules {
    std::uint8_t color = 0;
    std::uint32_t speedPerDamage = 0;
    std::uint32_t mostExtraSpeed = 0;
    std::uint32_t fastest = 0;
};

/// How a client shows a stream of blood (R_BloodStream), from where the hit met the head along the
/// stream's way, each particle of the palette entry of the stream's plus 0 to colorRange - 1, drawn at
/// random. Its stream: streamParticles particles, the way of each dipped by firstDip less dipStep for each
/// before it, the first at the stream's speed and each after it speedStep slower, none slower than
/// still, falling at streamGravity of the world's gravity, gone after streamLife. Its splash: a particle
/// for each splashPerSpeed of its speed, whole, the way of each dipped by firstSplashDip less dipStep for
/// each before it, at a share of the speed drawn from 0 to 1, cut to a whole number, times that share
/// times splashSpread; and splashCompanions more with each, as fast, dipped a step less, each drawn up to
/// companionJitter off it, as much on each axis; all falling at splashGravity of the world's gravity,
/// gone after splashLife.
struct BloodStreamLook {
    std::uint32_t colorRange = 0;
    std::uint32_t streamParticles = 0;
    float firstDip = 0.0F;
    float dipStep = 0.0F;
    nx::UnitsPerSecond speedStep{};
    nx::Scale streamGravity{};
    nx::Seconds streamLife{};
    std::uint32_t splashPerSpeed = 0;
    float firstSplashDip = 0.0F;
    float splashSpread = 0.0F;
    std::uint32_t splashCompanions = 0;
    nx::HammerUnits companionJitter{};
    nx::Scale splashGravity{};
    nx::Seconds splashLife{};
};

/// How the enhanced look sprays the blood of a hit in one part of the body (cs.client:bloodEffects), the
/// project's own: `dropletsPerSize` droplets for each of the spray's size, their radii leastRadius to
/// mostRadius, flying off where the hit met the body the way it went, but `backShare` of them back the
/// way it came, each turned off that way by up to `spread` of its speed on each axis, at leastSpeed to
/// mostSpeed and up to `rise` faster upward; `mists` mists there, their radii leastMistRadius to
/// mostMistRadius, growing by mistGrowth a second, as opaque as mistOpacity at first and gone, faded out,
/// after mistLife; and after it, as a wound gushes, `gushPulses` pulses of `gushDroplets` droplets
/// each, one every gushInterval, the way the hit went, each gushSlowing as fast as the one before. All
/// drawn at random.
struct BloodBurst {
    float dropletsPerSize = 0.0F;
    nx::HammerUnits leastRadius{};
    nx::HammerUnits mostRadius{};
    nx::UnitsPerSecond leastSpeed{};
    nx::UnitsPerSecond mostSpeed{};
    float spread = 0.0F;
    float backShare = 0.0F;
    nx::UnitsPerSecond rise{};
    std::uint32_t mists = 0;
    nx::HammerUnits leastMistRadius{};
    nx::HammerUnits mostMistRadius{};
    nx::UnitsPerSecond mistGrowth{};
    float mistOpacity = 0.0F;
    nx::Seconds mistLife{};
    std::uint32_t gushPulses = 0;
    std::uint32_t gushDroplets = 0;
    nx::Seconds gushInterval{};
    float gushSlowing = 0.0F;
};

/// How the enhanced look shows the blood a hit sprays (cs.client:bloodEffects), where the legacy look
/// shows the original's (sprayLook, streamLook): at most mostDroplets droplets at once, none made past
/// them, and mostSplats splats, the oldest giving way to a new one; each droplet drawn along streakTime
/// of its flight, falling at `gravity` of the world's, losing `drag` of its speed a second, gone after
/// dropletLife unless it lands first; of those that land on the world, splatShare leave a splat, flat
/// on what they met, their radius times leastSplatGrowth to mostSplatGrowth, lying there for splatLife
/// and fading out over the last splatFade of it; and the bursts of a hit in the head and of a hit
/// elsewhere.
struct BloodEnhancedLook {
    std::uint32_t mostDroplets = 0;
    std::uint32_t mostSplats = 0;
    nx::Seconds streakTime{};
    nx::Scale gravity{};
    float drag = 0.0F;
    nx::Seconds dropletLife{};
    float splatShare = 0.0F;
    nx::Scale leastSplatGrowth{};
    nx::Scale mostSplatGrowth{};
    nx::Seconds splatLife{};
    nx::Seconds splatFade{};
    BloodBurst body{};
    BloodBurst head{};
};

/// data/cstrike/blood.toml: the decals blood leaves (DECAL_BLOOD1 to DECAL_BLOOD6), how far behind a hit
/// it reaches, one in how many of its ways that meet what is solid leave a mark, and its spreads by the
/// hit's damage, the first whose `below` the damage is below, the last of any; how the server tells of
/// the blood a hit sprays and of a head's stream, and how a client shows them, in the original's look
/// and in the enhanced look.
struct BloodRules {
    std::vector<std::string> decals;
    nx::HammerUnits reach{};
    std::uint32_t marksOneIn = 0;
    std::vector<BleedSpread> spreads;
    BloodSprayRules spray{};
    BloodSprayLook sprayLook{};
    BloodStreamRules stream{};
    BloodStreamLook streamLook{};
    BloodEnhancedLook enhancedLook{};
};

/// Reads `text`, the file `sourceName`: decals, a reach and a share of marks, and spreads of traces and
/// noise, each `below` more than the last's, none for the last alone; a spray's sizes from one, its
/// color shifted within the palette, its sprites, its times, scales and dice more than none, and each
/// least no more than its most; a stream's speeds within a byte, its colors within the palette, and its
/// particles, times and steps more than none; the enhanced look's room for droplets and splats, its
/// times and shares, and its bursts' least no more than their most. Errors: DataInvalid.
[[nodiscard]] auto readBloodRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BloodRules, BloodError>;

/// The ways a hit of `damage` going `direction` traces the blood it sheds behind it (TraceBleed): as
/// many as its spread, each `direction` turned by up to the spread's noise on each axis, drawn from
/// `random`.
[[nodiscard]] auto bleedingWaysOf(const BloodRules& rules, nx::Damage damage, nx::Vec3 direction,
                                  nx::Random& random) -> std::vector<nx::Vec3>;

/// The decal, of BloodRules::decals, that a way of blood meeting what is solid leaves: one in
/// BloodRules::marksOneIn of them, each decal as likely (UTIL_BloodDecalTrace), drawn from `random`;
/// none for the others.
[[nodiscard]] auto bloodDecalOf(const BloodRules& rules, nx::Random& random) -> std::optional<std::uint32_t>;

/// The size of the spray of blood a hit of `damage` makes (UTIL_BloodDrips); none for a hit of no whole
/// point, which sprays none.
[[nodiscard]] auto spraySizeOf(const BloodSprayRules& rules, nx::Damage damage)
    -> std::optional<std::uint32_t>;

/// The speed, in whole units a second, of the stream of blood a hit of `damage` makes a head shed
/// (BloodSplat), its extra drawn from `random`.
[[nodiscard]] auto streamSpeedOf(const BloodStreamRules& rules, nx::Damage damage, nx::Random& random)
    -> std::uint32_t;

} // namespace cs

template <>
struct nx::meta::Description<cs::BleedSpread> {
    static constexpr std::string_view kName = "cs::BleedSpread";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BleedSpread::below>("below"),
        nx::meta::field<&cs::BleedSpread::traces>("traces"),
        nx::meta::field<&cs::BleedSpread::noise>("noise", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::BloodSprayRules> {
    static constexpr std::string_view kName = "cs::BloodSprayRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodSprayRules::color>("color"),
        nx::meta::field<&cs::BloodSprayRules::amountPerDamage>("amountPerDamage"),
        nx::meta::field<&cs::BloodSprayRules::mostAmount>("mostAmount"),
        nx::meta::field<&cs::BloodSprayRules::amountPerSize>("amountPerSize"),
        nx::meta::field<&cs::BloodSprayRules::smallestSize>("smallestSize"),
        nx::meta::field<&cs::BloodSprayRules::largestSize>("largestSize"),
    };
};

template <>
struct nx::meta::Description<cs::BloodSprayLook> {
    static constexpr std::string_view kName = "cs::BloodSprayLook";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodSprayLook::spraySprite>("spraySprite"),
        nx::meta::field<&cs::BloodSprayLook::dropSprite>("dropSprite"),
        nx::meta::field<&cs::BloodSprayLook::colorShift>("colorShift"),
        nx::meta::field<&cs::BloodSprayLook::amount>("amount"),
        nx::meta::field<&cs::BloodSprayLook::leastLift>("leastLift"),
        nx::meta::field<&cs::BloodSprayLook::mostLift>("mostLift"),
        nx::meta::field<&cs::BloodSprayLook::leastSprayScale>("leastSprayScale"),
        nx::meta::field<&cs::BloodSprayLook::mostSprayScale>("mostSprayScale"),
        nx::meta::field<&cs::BloodSprayLook::sprayTime>("sprayTime"),
        nx::meta::field<&cs::BloodSprayLook::extraDropDice>("extraDropDice"),
        nx::meta::field<&cs::BloodSprayLook::extraDropFaces>("extraDropFaces"),
        nx::meta::field<&cs::BloodSprayLook::leastDropScale>("leastDropScale"),
        nx::meta::field<&cs::BloodSprayLook::mostDropScale>("mostDropScale"),
        nx::meta::field<&cs::BloodSprayLook::leastDropSpeed>("leastDropSpeed"),
        nx::meta::field<&cs::BloodSprayLook::mostDropSpeed>("mostDropSpeed"),
        nx::meta::field<&cs::BloodSprayLook::leastDropRise>("leastDropRise"),
        nx::meta::field<&cs::BloodSprayLook::mostDropRise>("mostDropRise"),
        nx::meta::field<&cs::BloodSprayLook::leastDropSpin>("leastDropSpin"),
        nx::meta::field<&cs::BloodSprayLook::mostDropSpin>("mostDropSpin"),
        nx::meta::field<&cs::BloodSprayLook::dropGravity>("dropGravity"),
        nx::meta::field<&cs::BloodSprayLook::leastDropLife>("leastDropLife"),
        nx::meta::field<&cs::BloodSprayLook::mostDropLife>("mostDropLife"),
        nx::meta::field<&cs::BloodSprayLook::mostDropFrame>("mostDropFrame"),
    };
};

template <>
struct nx::meta::Description<cs::BloodStreamRules> {
    static constexpr std::string_view kName = "cs::BloodStreamRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodStreamRules::color>("color"),
        nx::meta::field<&cs::BloodStreamRules::speedPerDamage>("speedPerDamage"),
        nx::meta::field<&cs::BloodStreamRules::mostExtraSpeed>("mostExtraSpeed"),
        nx::meta::field<&cs::BloodStreamRules::fastest>("fastest"),
    };
};

template <>
struct nx::meta::Description<cs::BloodStreamLook> {
    static constexpr std::string_view kName = "cs::BloodStreamLook";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodStreamLook::colorRange>("colorRange"),
        nx::meta::field<&cs::BloodStreamLook::streamParticles>("streamParticles"),
        nx::meta::field<&cs::BloodStreamLook::firstDip>("firstDip"),
        nx::meta::field<&cs::BloodStreamLook::dipStep>("dipStep"),
        nx::meta::field<&cs::BloodStreamLook::speedStep>("speedStep"),
        nx::meta::field<&cs::BloodStreamLook::streamGravity>("streamGravity"),
        nx::meta::field<&cs::BloodStreamLook::streamLife>("streamLife"),
        nx::meta::field<&cs::BloodStreamLook::splashPerSpeed>("splashPerSpeed"),
        nx::meta::field<&cs::BloodStreamLook::firstSplashDip>("firstSplashDip"),
        nx::meta::field<&cs::BloodStreamLook::splashSpread>("splashSpread"),
        nx::meta::field<&cs::BloodStreamLook::splashCompanions>("splashCompanions"),
        nx::meta::field<&cs::BloodStreamLook::companionJitter>("companionJitter"),
        nx::meta::field<&cs::BloodStreamLook::splashGravity>("splashGravity"),
        nx::meta::field<&cs::BloodStreamLook::splashLife>("splashLife"),
    };
};

template <>
struct nx::meta::Description<cs::BloodBurst> {
    static constexpr std::string_view kName = "cs::BloodBurst";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodBurst::dropletsPerSize>("dropletsPerSize", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BloodBurst::leastRadius>("leastRadius"),
        nx::meta::field<&cs::BloodBurst::mostRadius>("mostRadius"),
        nx::meta::field<&cs::BloodBurst::leastSpeed>("leastSpeed"),
        nx::meta::field<&cs::BloodBurst::mostSpeed>("mostSpeed"),
        nx::meta::field<&cs::BloodBurst::spread>("spread", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BloodBurst::backShare>("backShare",
                                                    nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BloodBurst::rise>("rise"),
        nx::meta::field<&cs::BloodBurst::mists>("mists"),
        nx::meta::field<&cs::BloodBurst::leastMistRadius>("leastMistRadius"),
        nx::meta::field<&cs::BloodBurst::mostMistRadius>("mostMistRadius"),
        nx::meta::field<&cs::BloodBurst::mistGrowth>("mistGrowth"),
        nx::meta::field<&cs::BloodBurst::mistOpacity>("mistOpacity",
                                                      nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BloodBurst::mistLife>("mistLife"),
        nx::meta::field<&cs::BloodBurst::gushPulses>("gushPulses"),
        nx::meta::field<&cs::BloodBurst::gushDroplets>("gushDroplets"),
        nx::meta::field<&cs::BloodBurst::gushInterval>("gushInterval"),
        nx::meta::field<&cs::BloodBurst::gushSlowing>("gushSlowing", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::BloodEnhancedLook> {
    static constexpr std::string_view kName = "cs::BloodEnhancedLook";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodEnhancedLook::mostDroplets>("mostDroplets"),
        nx::meta::field<&cs::BloodEnhancedLook::mostSplats>("mostSplats"),
        nx::meta::field<&cs::BloodEnhancedLook::streakTime>("streakTime"),
        nx::meta::field<&cs::BloodEnhancedLook::gravity>("gravity"),
        nx::meta::field<&cs::BloodEnhancedLook::drag>("drag", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BloodEnhancedLook::dropletLife>("dropletLife"),
        nx::meta::field<&cs::BloodEnhancedLook::splatShare>("splatShare",
                                                            nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BloodEnhancedLook::leastSplatGrowth>("leastSplatGrowth"),
        nx::meta::field<&cs::BloodEnhancedLook::mostSplatGrowth>("mostSplatGrowth"),
        nx::meta::field<&cs::BloodEnhancedLook::splatLife>("splatLife"),
        nx::meta::field<&cs::BloodEnhancedLook::splatFade>("splatFade"),
        nx::meta::field<&cs::BloodEnhancedLook::body>("body"),
        nx::meta::field<&cs::BloodEnhancedLook::head>("head"),
    };
};

template <>
struct nx::meta::Description<cs::BloodRules> {
    static constexpr std::string_view kName = "cs::BloodRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BloodRules::decals>("decals"),
        nx::meta::field<&cs::BloodRules::reach>("reach"),
        nx::meta::field<&cs::BloodRules::marksOneIn>("marksOneIn"),
        nx::meta::field<&cs::BloodRules::spreads>("spreads"),
        nx::meta::field<&cs::BloodRules::spray>("spray"),
        nx::meta::field<&cs::BloodRules::sprayLook>("sprayLook"),
        nx::meta::field<&cs::BloodRules::stream>("stream"),
        nx::meta::field<&cs::BloodRules::streamLook>("streamLook"),
        nx::meta::field<&cs::BloodRules::enhancedLook>("enhancedLook"),
    };
};
