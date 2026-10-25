// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How a player's feet sound (docs/11-audio.md, section 3): data/cstrike/footsteps.toml, with
/// ReGameDLL_CS's sounds and volumes (PM_PlayStepSound, PM_UpdateStepSound, PM_PlayWaterSounds;
/// GPL, consulted: docs/20 section 3): the sounds of each kind of step, which a step the movement
/// makes plays (cs::Footstep), by where it is and what the ground is made of, and the splash of
/// water.
export module cs.shared:footsteps;

import std;
import nx.core;
import nx.meta;
import nx.data;
import :sounds;
import :materials;
import :movement;

export namespace cs {

/// Why the footsteps' data could not be read.
enum class FootstepError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// The kinds of steps that sound alike (the original's STEP_*): one for each ground that sounds of
/// its own, a ladder's, and water's.
enum class StepKind : std::uint8_t {
    Concrete,
    Metal,
    Dirt,
    Vent,
    Grate,
    Tile,
    Slosh, ///< In water at the feet, or on a ground of liquid.
    Wade,  ///< In water to the knees.
    Ladder,
    Snow,
    Count,
};

/// The kind of step a player makes at `place`, on a ground made of `ground` (PM_MapTextureTypeStepType):
/// a ground of no kind of its own (wood, computers, grass, glass, flesh) sounds as concrete.
[[nodiscard]] auto stepKindOf(StepPlace place, TextureMaterial ground) noexcept -> StepKind;

/// A sound some steps make in place of their foot's: one in `oneIn` of them, at random.
struct RareStep {
    GameSound sound{};
    std::uint32_t oneIn{};
};

/// The sounds of a kind of step: its right foot's and its left foot's, one of each's files at
/// random, their volume the step's; and, for some, a rare one; and, for wading, how many steps of
/// each run of `silentEvery` the first of is silent (none: none is).
struct StepSounds {
    StepKind kind{};
    GameSound rightFoot{};
    GameSound leftFoot{};
    std::optional<RareStep> rare{};
    std::optional<std::uint32_t> silentEvery{};
};

/// data/cstrike/footsteps.toml.
struct FootstepRules {
    std::vector<StepSounds> steps{}; ///< One for each kind at most.
    GameSound splash{};              ///< Entering water, leaving it, and swimming up.

    /// The sounds of `kind`'s steps; none when the file has none.
    [[nodiscard]] auto soundsOf(StepKind kind) const noexcept -> const StepSounds*;
    /// Every file its sounds play, each once, in the order the file names them.
    [[nodiscard]] auto soundFiles() const -> std::vector<std::string_view>;
};

/// Reads `text`, the file `sourceName`: each kind's sounds once at most, each sound with a file, a rare
/// step one in 1 at least. Errors: DataInvalid.
[[nodiscard]] auto readFootstepRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<FootstepRules, FootstepError>;

/// What a player's steps sound like, as the steps of `rules`, which must outlive them, play their
/// sounds: which, and how loud. It keeps what the original's PM_PlayStepSound keeps between them.
class StepVoice {
public:
    explicit StepVoice(const FootstepRules& rules) noexcept;

    /// The sound `step` plays at `at`, on a ground made of `ground`, its rare sound chosen from
    /// `random`; none for a kind of step the rules give no sound, or a wading step in silence (the
    /// first of each run of `silentEvery`, counted for this player: the original counts every player's
    /// together).
    [[nodiscard]] auto soundOf(const Footstep& step, TextureMaterial ground, nx::GameTime at,
                               nx::Random& random) -> std::optional<SoundMade>;

private:
    const FootstepRules* m_rules;
    std::uint32_t m_wadeSteps = 0; ///< Of the run of wading steps (iSkipStep).
};

} // namespace cs

template <>
struct nx::meta::Description<cs::RareStep> {
    static constexpr std::string_view kName = "cs::RareStep";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::RareStep::sound>("sound"),
        nx::meta::field<&cs::RareStep::oneIn>("oneIn", nx::data::Range{.minimum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::StepSounds> {
    static constexpr std::string_view kName = "cs::StepSounds";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::StepSounds::kind>("kind"),
        nx::meta::field<&cs::StepSounds::rightFoot>("rightFoot"),
        nx::meta::field<&cs::StepSounds::leftFoot>("leftFoot"),
        nx::meta::field<&cs::StepSounds::rare>("rare"),
        nx::meta::field<&cs::StepSounds::silentEvery>("silentEvery", nx::data::Range{.minimum = 1.0}),
    };
};

template <>
struct nx::meta::Description<cs::FootstepRules> {
    static constexpr std::string_view kName = "cs::FootstepRules";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::FootstepRules::steps>("steps"),
        nx::meta::field<&cs::FootstepRules::splash>("splash"),
    };
};
