// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;

namespace cs {

namespace {

/// The kind of step of a ground made of each material: its own, or concrete's.
constexpr nx::EnumArray<TextureMaterial, StepKind> kGroundSteps{
    nx::EnumArray<TextureMaterial, StepKind>::Storage{
        StepKind::Concrete, StepKind::Metal, StepKind::Dirt, StepKind::Vent, StepKind::Grate, StepKind::Tile,
        StepKind::Slosh, StepKind::Concrete, StepKind::Concrete, StepKind::Concrete, StepKind::Concrete,
        StepKind::Concrete, StepKind::Snow}};

} // namespace

auto stepKindOf(StepPlace place, TextureMaterial ground) noexcept -> StepKind {
    switch (place) {
    case StepPlace::Ladder:
        return StepKind::Ladder;
    case StepPlace::Wade:
        return StepKind::Wade;
    case StepPlace::Slosh:
        return StepKind::Slosh;
    default:
        return kGroundSteps[ground];
    }
}

auto FootstepRules::soundsOf(StepKind kind) const noexcept -> const StepSounds* {
    const auto found = std::ranges::find(steps, kind, &StepSounds::kind);
    return found != steps.end() ? &*found : nullptr;
}

auto FootstepRules::soundFiles() const -> std::vector<std::string_view> {
    std::vector<std::string_view> files;
    const auto add = [&files](const GameSound& sound) {
        for (const std::string& file : sound.files) {
            if (!std::ranges::contains(files, std::string_view{file})) {
                files.emplace_back(file);
            }
        }
    };
    add(splash);
    for (const StepSounds& step : steps) {
        add(step.rightFoot);
        add(step.leftFoot);
        if (step.rare) {
            add(step.rare->sound);
        }
    }
    return files;
}

auto readFootstepRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<FootstepRules, FootstepError> {
    auto rules = nx::data::parseToml<FootstepRules>(text, sourceName);
    if (!rules) {
        return nx::fail(FootstepError::DataInvalid, std::string{sourceName}, std::move(rules.error()));
    }
    const auto invalid = [sourceName](std::string_view what, std::string_view problem) {
        return nx::fail(FootstepError::DataInvalid, std::format("{}: {} {}", sourceName, what, problem));
    };
    if (const std::optional<std::string_view> problem = soundProblem(rules->splash)) {
        return invalid("the splash", *problem);
    }
    std::set<StepKind> kinds;
    for (const StepSounds& step : rules->steps) {
        const std::string what = std::format("the {} steps", nx::enumName(step.kind));
        if (!kinds.insert(step.kind).second) {
            return invalid(what, "twice");
        }
        for (const GameSound* const sound :
             {&step.rightFoot, &step.leftFoot, step.rare ? &step.rare->sound : nullptr}) {
            if (sound == nullptr) {
                continue;
            }
            if (const std::optional<std::string_view> problem = soundProblem(*sound)) {
                return invalid(what, *problem);
            }
        }
    }
    return std::move(*rules);
}

StepVoice::StepVoice(const FootstepRules& rules) noexcept : m_rules{&rules} {}

auto StepVoice::soundOf(const Footstep& step, TextureMaterial ground, nx::GameTime at, nx::Random& random)
    -> std::optional<SoundMade> {
    const StepSounds* const sounds = m_rules->soundsOf(stepKindOf(step.place, ground));
    if (sounds == nullptr) {
        return std::nullopt;
    }
    if (sounds->silentEvery) {
        // The first step of each run is silent.
        const bool isSilent = m_wadeSteps == 0;
        m_wadeSteps = (m_wadeSteps + 1) % *sounds->silentEvery;
        if (isSilent) {
            return std::nullopt;
        }
    }
    const GameSound* sound = step.isLeftFoot ? &sounds->leftFoot : &sounds->rightFoot;
    if (sounds->rare && random.below(sounds->rare->oneIn) == 0) {
        sound = &sounds->rare->sound;
    }
    return SoundMade{.sound = sound, .at = at, .volume = step.volume.value_or(sound->volume * step.share)};
}

} // namespace cs
