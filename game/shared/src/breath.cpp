// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;
import nx.game;

namespace cs {

namespace {

/// One breath in this many with the head under water may bubble (RANDOM_LONG(0, 0x1f)).
constexpr std::uint64_t kBubbleOneIn = 32;
/// The whole seconds of air a bubble is drawn against: from 0 to 11 (RANDOM_LONG(0, AIRTIME - 1)).
constexpr auto kAirSeconds = static_cast<std::uint64_t>(kAirTime.count());

} // namespace

auto BreathRules::soundFiles() const -> std::vector<std::string_view> {
    std::vector<std::string_view> files;
    for (const GameSound* const sound : {&shortGasp, &longGasp, &bubbles}) {
        for (const std::string& file : sound->files) {
            if (!std::ranges::contains(files, std::string_view{file})) {
                files.emplace_back(file);
            }
        }
    }
    return files;
}

auto readBreathRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<BreathRules, BreathError> {
    auto rules = nx::data::parseToml<BreathRules>(text, sourceName);
    if (!rules) {
        return nx::fail(BreathError::DataInvalid, std::string{sourceName}, std::move(rules.error()));
    }
    for (const auto& [what, sound] :
         {std::pair{"the short gasp", &rules->shortGasp}, std::pair{"the long gasp", &rules->longGasp},
          std::pair{"the bubbles", &rules->bubbles}}) {
        if (const std::optional<std::string_view> problem = soundProblem(*sound)) {
            return nx::fail(BreathError::DataInvalid, std::format("{}: {} {}", sourceName, what, *problem));
        }
    }
    return std::move(*rules);
}

Breath::Breath(const BreathRules& rules) noexcept : m_rules{&rules} {}

void Breath::fill(const nx::game::TickTime& time) noexcept {
    m_airRunsOut = time.now.after(kAirTime, time.rate);
}

auto Breath::breathe(WaterLevel level, MoveType moveType, const nx::game::TickTime& time, nx::Random& random)
    -> std::optional<SoundMade> {
    if (moveType == MoveType::Noclip) {
        return std::nullopt;
    }
    if (level != WaterLevel::Eyes) {
        const GameSound* gasp = nullptr;
        if (m_airRunsOut < time.now) {
            gasp = &m_rules->longGasp;
        } else if (m_airRunsOut < time.now.after(kGaspingAir, time.rate)) {
            gasp = &m_rules->shortGasp;
        }
        fill(time);
        if (gasp != nullptr) {
            return SoundMade{.sound = gasp, .at = time.now, .volume = gasp->volume};
        }
    }
    if (level == WaterLevel::Dry) {
        return std::nullopt;
    }
    // With the head out of water the air is whole: no second of it is drawn above its 11, and it does not
    // bubble. Parity: the original's float time sometimes makes it a little less than whole, so that one
    // wading within 12 seconds before a power of two of seconds bubbles now and then; the game's time
    // (docs/07 section 1) does not.
    const float airLeft = std::trunc(m_airRunsOut.since(time.now, time.rate).count());
    if (random.below(kBubbleOneIn) != 0 || static_cast<float>(random.below(kAirSeconds)) < airLeft) {
        return std::nullopt;
    }
    return SoundMade{.sound = &m_rules->bubbles, .at = time.now, .volume = m_rules->bubbles.volume};
}

} // namespace cs
