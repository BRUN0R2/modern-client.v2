// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How the bots see, remember, move, keep their number and play by their skill
/// (docs/10-counter-strike-gameplay.md, section 11): data/cstrike/bots.toml, with the values of
/// ReGameDLL_CS's reconstruction of Condition Zero's bot where it has them (CCSBot::IsVisible,
/// CBaseMonster::FInViewCone, CCSBotManager::MonitorBotCVars, CCSBot::UpdateLookAngles,
/// CCSBot::SetAimOffset, CCSBot::FireWeaponAtEnemy, the zBot's BotProfile.db; GPL, consulted: docs/20
/// section 3) and the project's own where it has none.
export module cs.bots:botRules;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.agents;
import cs.server;

export namespace cs {

/// Why the bots could not be made.
enum class BotError : std::uint8_t {
    InvalidData,  ///< bots.toml breaks its schema or its rules (cause: the DataError, when there is one).
    Unregistered, ///< A component of the bots' bodies could not be registered in the game (cause).
};

/// The difficulty the bots that join play at (BotDifficultyType), which bot_difficulty names.
enum class BotDifficulty : std::uint8_t {
    Easy,
    Normal,
    Hard,
    Expert,
    Count,
};

/// The unit of BotSkill: none.
struct BotSkillTag {};

/// How good a bot is, from none (0) to the best (1): a profile's Skill, a hundredth of it.
using BotSkill = nx::Quantity<float, BotSkillTag>;

/// A level of skill a bot plays with, as a template of BotProfile.db makes one: its name, the
/// difficulties a bot of it joins at, the names of its profiles, the bots that play with it, its skill, its
/// aggression, from 0 to 1 (Aggression, a hundredth of the template's), how long after it first sees an
/// enemy it knows of it (ReactionTime), and how long after it took an enemy on it fires at it (AttackDelay).
struct BotSkillLevel {
    std::string name;
    std::vector<BotDifficulty> difficulties;
    std::vector<std::string> profiles;
    BotSkill skill{};
    float aggression = 0.0F;
    nx::Seconds reactionTime{};
    nx::Seconds attackDelay{};
};

/// Where a bot looks for a player it may see, from the player's origin, in the order IsVisible tries
/// them: its chest (the origin itself), its head above, its feet below as it stands or ducks, and its
/// sides, either way across the bot's line to it.
struct BotSightSpots {
    nx::HammerUnits head{};
    nx::HammerUnits standingFeet{};
    nx::HammerUnits duckedFeet{};
    nx::HammerUnits side{};
};

/// The spring a bot's view turns by towards where it looks (UpdateLookAngles): it speeds up by how far
/// off it is, times its stiffness, less how fast it turns, times its damping, by maxAcceleration at most.
struct BotSpring {
    nx::PerSecondSquared stiffness{};
    nx::PerSecond damping{};
    nx::DegreesPerSecondSquared maxAcceleration{};
};

/// How a bot's view turns (UpdateLookAngles): by a stiffer spring as it attacks, so that it follows its
/// enemy, than as it roams; its yaw set where it looks once that is nearer than snapTolerance; its pitch
/// pulled pitchStiffness times as hard, as its range is half the yaw's, never snapped, and held within
/// pitchLimit up or down.
struct BotLookRules {
    BotSpring attacking{};
    BotSpring roaming{};
    nx::Degrees snapTolerance{};
    nx::Scale pitchStiffness{};
    nx::Degrees pitchLimit{};
};

/// How a bot's aim errs about where it means to aim (SetAimOffset, UpdateAimOffset): on each axis by
/// errorPerRange of its range at most, times the share of skill it lacks; a skill it gains by focusing,
/// as its view holds steadier than steadyTurn, of all it gains in slowestFocus times the share of skill
/// it lacks, quickestFocus at least, up to bestFocus; a new error drawn soonestNewError to
/// latestNewError after the last, which its aim moves settleShare of the way to, settleRate times a
/// second. Above leadingSkill it aims ahead of a moving enemy by their velocities apart over leadTime.
struct BotAimRules {
    nx::Scale errorPerRange{};
    nx::Seconds slowestFocus{};
    nx::Seconds quickestFocus{};
    BotSkill bestFocus{};
    nx::DegreesPerSecond steadyTurn{};
    nx::Seconds soonestNewError{};
    nx::Seconds latestNewError{};
    nx::Scale settleShare{};
    nx::PerSecond settleRate{};
    BotSkill leadingSkill{};
    nx::Seconds leadTime{};
};

/// A pause drawn between two lengths.
struct BotPause {
    nx::Seconds shortest{};
    nx::Seconds longest{};
};

/// How a bot fires (FireWeaponAtEnemy): it weighs a press at each full think, thinkInterval apart
/// (g_flBotFullThinkInterval), its press held until the next; only with its view's yaw within the angle
/// aimHalfWidth spans at the range of where it aims; the next press a pause later, less thinkInterval:
/// with a pistol, as fast as it may above rapidPistolSkill (StartRapidFire), else after pistolPause;
/// with a gun, at once below spraySkill or nearer than sprayRange, after distantPause beyond
/// distantRange, else after burstPause.
/// A distance drawn at random, from its nearest to its farthest.
struct BotRange {
    nx::HammerUnits nearest{};
    nx::HammerUnits farthest{};
};

/// How a bot moves as it fights (AttackState, cs.bots:botCombat): whether it crouches and holds as a fight
/// begins, farCrouchChance with its enemy farther than crouchFarFrom, else nearCrouchChance of how much
/// aggression it lacks, its sight crouchSightDrop lower clear to its enemy's eye; whether it dodges a fight,
/// dodgeChance of its skill, knifeDodgeFactor times that with a knife; dodging, within dodgeRange of an enemy
/// that faces it, a way drawn each dodgeTime, the first of a fight a jump at jumpChance from jumpSkill,
/// moving faster than movingSpeed (IsNotMoving's still speed); and, below rangeSkill, the combat range it
/// keeps, drawn within combatRange as it spawns, rangeSlack either way.
struct BotCombatRules {
    float farCrouchChance = 0.0F;
    nx::HammerUnits crouchFarFrom{};
    float nearCrouchChance = 0.0F;
    nx::HammerUnits crouchSightDrop{};
    float dodgeChance = 0.0F;
    float knifeDodgeFactor = 0.0F;
    nx::HammerUnits dodgeRange{};
    BotPause dodgeTime{};
    BotSkill jumpSkill{};
    float jumpChance = 0.0F;
    nx::UnitsPerSecond movingSpeed{};
    BotRange combatRange{};
    nx::HammerUnits rangeSlack{};
    BotSkill rangeSkill{};
};

/// How a bot hurt by an enemy looks about (CCSBot::TakeDamage, Panic): unless it sees an enemy as it
/// attacks, or else at random, chance times the square of how much skill it lacks, it looks at a spot about
/// it. From skilledFrom of skill, on the side the attack came from, ahead, behind or either way across of
/// where it looks: `near` that way and up to `shift` the other way; below it, `far` behind and up to
/// `farSide` to a side. Up to `height` up or down, until its view is within lookedWithin of it.
struct BotPanicRules {
    BotSkill skilledFrom{};
    float chance = 0.0F;
    nx::HammerUnits near{};
    nx::HammerUnits shift{};
    nx::HammerUnits far{};
    nx::HammerUnits farSide{};
    nx::HammerUnits height{};
    nx::Degrees lookedWithin{};
};

struct BotFireRules {
    nx::HammerUnits aimHalfWidth{};
    nx::Seconds thinkInterval{};
    BotSkill rapidPistolSkill{};
    BotPause pistolPause{};
    BotSkill spraySkill{};
    nx::HammerUnits sprayRange{};
    nx::HammerUnits distantRange{};
    BotPause distantPause{};
    BotPause burstPause{};
};

/// data/cstrike/bots.toml: what a bot sees (the cone about its view's yaw, the players it looks at each
/// tick in turn, and the spots of each), how long and how many it remembers, how near it attacks, how
/// near it arrives and how far a goal moves before it finds another way (at least, and in shares of how far
/// it is), how long where it placed an enemy holds against a vaguer estimate, how it finds itself stuck and
/// gets out, how far it hears each kind of noise, how far its hunt's next place is drawn, how often bot_quota
/// is looked at, the levels of skill it plays with, how it turns its view, aims and fires, how it looks
/// about when it is hurt, how it moves as it fights, and when a threat draws it off an enemy it lost.
struct BotRules {
    nx::Degrees fieldOfView{};
    std::uint32_t perceptionBudget = 0;
    BotSightSpots spots{};
    nx::Seconds memoryLifetime{};
    std::uint32_t memoryLimit = 0;
    nx::HammerUnits attackDistance{};
    nx::HammerUnits arrivalDistance{};
    nx::HammerUnits replanDistance{};
    float replanShare = 0.0F;
    nx::Seconds shortTermMemory{};
    nx::Seconds threatRecency{};
    nx::HammerUnits threatMargin{};
    nx::agents::StuckRules stuck{};
    HearingRules hearing{};
    nx::HammerUnits huntRadius{};
    nx::Seconds quotaInterval{};
    std::vector<BotSkillLevel> skillLevels;
    BotLookRules look{};
    BotAimRules aim{};
    BotFireRules fire{};
    BotPanicRules panic{};
    BotCombatRules combat{};
};

/// Reads `text`, the file `sourceName`: every distance, time and budget more than none, the cone a
/// whole turn at most; rules a stuck bot's watch may follow (nx::agents::isValid), and a bot may hear by
/// (cs::isValid); a level of skill for each
/// difficulty, each of none or more time and of profiles named as the server names a player and as no other
/// profile is, in any case; each pause's shortest none or more and its longest no shorter, the springs and
/// the pitch's limit more than none (a quarter turn at most). Errors: InvalidData.
[[nodiscard]] auto readBotRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BotRules, BotError>;

/// The rules of the brain a bot of `rules` thinks with, reacting `reactionTime` after it first sees an
/// enemy.
[[nodiscard]] auto brainRulesOf(const BotRules& rules, nx::Seconds reactionTime) noexcept
    -> nx::agents::Rules;

} // namespace cs

template <>
struct nx::meta::Description<cs::BotSkillLevel> {
    static constexpr std::string_view kName = "cs::BotSkillLevel";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotSkillLevel::name>("name"),
        nx::meta::field<&cs::BotSkillLevel::difficulties>("difficulties"),
        nx::meta::field<&cs::BotSkillLevel::profiles>("profiles"),
        nx::meta::field<&cs::BotSkillLevel::skill>("skill", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotSkillLevel::reactionTime>("reactionTime"),
        nx::meta::field<&cs::BotSkillLevel::attackDelay>("attackDelay"),
        nx::meta::field<&cs::BotSkillLevel::aggression>("aggression",
                                                        nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::BotSightSpots> {
    static constexpr std::string_view kName = "cs::BotSightSpots";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotSightSpots::head>("head", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotSightSpots::standingFeet>("standingFeet", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotSightSpots::duckedFeet>("duckedFeet", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotSightSpots::side>("side", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::BotSpring> {
    static constexpr std::string_view kName = "cs::BotSpring";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotSpring::stiffness>("stiffness"),
        nx::meta::field<&cs::BotSpring::damping>("damping"),
        nx::meta::field<&cs::BotSpring::maxAcceleration>("maxAcceleration"),
    };
};

template <>
struct nx::meta::Description<cs::BotLookRules> {
    static constexpr std::string_view kName = "cs::BotLookRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotLookRules::attacking>("attacking"),
        nx::meta::field<&cs::BotLookRules::roaming>("roaming"),
        nx::meta::field<&cs::BotLookRules::snapTolerance>("snapTolerance", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotLookRules::pitchStiffness>("pitchStiffness"),
        nx::meta::field<&cs::BotLookRules::pitchLimit>("pitchLimit"),
    };
};

template <>
struct nx::meta::Description<cs::BotAimRules> {
    static constexpr std::string_view kName = "cs::BotAimRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotAimRules::errorPerRange>("errorPerRange", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotAimRules::slowestFocus>("slowestFocus"),
        nx::meta::field<&cs::BotAimRules::quickestFocus>("quickestFocus"),
        nx::meta::field<&cs::BotAimRules::bestFocus>("bestFocus",
                                                     nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotAimRules::steadyTurn>("steadyTurn"),
        nx::meta::field<&cs::BotAimRules::soonestNewError>("soonestNewError"),
        nx::meta::field<&cs::BotAimRules::latestNewError>("latestNewError"),
        nx::meta::field<&cs::BotAimRules::settleShare>("settleShare",
                                                       nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotAimRules::settleRate>("settleRate"),
        nx::meta::field<&cs::BotAimRules::leadingSkill>("leadingSkill",
                                                        nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotAimRules::leadTime>("leadTime"),
    };
};

template <>
struct nx::meta::Description<cs::BotPause> {
    static constexpr std::string_view kName = "cs::BotPause";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotPause::shortest>("shortest"),
        nx::meta::field<&cs::BotPause::longest>("longest"),
    };
};

template <>
struct nx::meta::Description<cs::BotFireRules> {
    static constexpr std::string_view kName = "cs::BotFireRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotFireRules::aimHalfWidth>("aimHalfWidth"),
        nx::meta::field<&cs::BotFireRules::thinkInterval>("thinkInterval"),
        nx::meta::field<&cs::BotFireRules::rapidPistolSkill>("rapidPistolSkill",
                                                             nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotFireRules::pistolPause>("pistolPause"),
        nx::meta::field<&cs::BotFireRules::spraySkill>("spraySkill",
                                                       nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotFireRules::sprayRange>("sprayRange", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotFireRules::distantRange>("distantRange", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotFireRules::distantPause>("distantPause"),
        nx::meta::field<&cs::BotFireRules::burstPause>("burstPause"),
    };
};

template <>
struct nx::meta::Description<cs::BotRange> {
    static constexpr std::string_view kName = "cs::BotRange";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotRange::nearest>("nearest", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotRange::farthest>("farthest", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::BotCombatRules> {
    static constexpr std::string_view kName = "cs::BotCombatRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotCombatRules::farCrouchChance>(
            "farCrouchChance", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotCombatRules::crouchFarFrom>("crouchFarFrom", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotCombatRules::nearCrouchChance>(
            "nearCrouchChance", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotCombatRules::crouchSightDrop>("crouchSightDrop",
                                                              nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotCombatRules::dodgeChance>("dodgeChance", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotCombatRules::knifeDodgeFactor>("knifeDodgeFactor",
                                                               nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotCombatRules::dodgeRange>("dodgeRange", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotCombatRules::dodgeTime>("dodgeTime"),
        nx::meta::field<&cs::BotCombatRules::jumpSkill>("jumpSkill",
                                                        nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotCombatRules::jumpChance>("jumpChance",
                                                         nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotCombatRules::movingSpeed>("movingSpeed", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotCombatRules::combatRange>("combatRange"),
        nx::meta::field<&cs::BotCombatRules::rangeSlack>("rangeSlack", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotCombatRules::rangeSkill>("rangeSkill",
                                                         nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::BotPanicRules> {
    static constexpr std::string_view kName = "cs::BotPanicRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotPanicRules::skilledFrom>("skilledFrom",
                                                         nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotPanicRules::chance>("chance",
                                                    nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::BotPanicRules::near>("near", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotPanicRules::shift>("shift", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotPanicRules::far>("far", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotPanicRules::farSide>("farSide", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotPanicRules::height>("height", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotPanicRules::lookedWithin>("lookedWithin", nx::data::Range{.minimum = 0.0}),
    };
};

template <>
struct nx::meta::Description<cs::BotRules> {
    static constexpr std::string_view kName = "cs::BotRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::BotRules::fieldOfView>("fieldOfView"),
        nx::meta::field<&cs::BotRules::perceptionBudget>("perceptionBudget"),
        nx::meta::field<&cs::BotRules::spots>("spots"),
        nx::meta::field<&cs::BotRules::memoryLifetime>("memoryLifetime"),
        nx::meta::field<&cs::BotRules::memoryLimit>("memoryLimit"),
        nx::meta::field<&cs::BotRules::attackDistance>("attackDistance"),
        nx::meta::field<&cs::BotRules::arrivalDistance>("arrivalDistance"),
        nx::meta::field<&cs::BotRules::replanDistance>("replanDistance"),
        nx::meta::field<&cs::BotRules::replanShare>("replanShare", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotRules::shortTermMemory>("shortTermMemory", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotRules::threatRecency>("threatRecency", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotRules::threatMargin>("threatMargin", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::BotRules::stuck>("stuck"),
        nx::meta::field<&cs::BotRules::hearing>("hearing"),
        nx::meta::field<&cs::BotRules::huntRadius>("huntRadius"),
        nx::meta::field<&cs::BotRules::quotaInterval>("quotaInterval"),
        nx::meta::field<&cs::BotRules::skillLevels>("skillLevels"),
        nx::meta::field<&cs::BotRules::look>("look"),
        nx::meta::field<&cs::BotRules::aim>("aim"),
        nx::meta::field<&cs::BotRules::fire>("fire"),
        nx::meta::field<&cs::BotRules::panic>("panic"),
        nx::meta::field<&cs::BotRules::combat>("combat"),
    };
};
