// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;
import nx.collision;
import nx.game;

namespace cs {

namespace {

/// One breath in this many with the head under water may bubble (RANDOM_LONG(0, 0x1f)).
constexpr std::uint64_t kBubbleOneIn = 32;
/// The whole seconds of air a bubble is drawn against: from 0 to 11 (RANDOM_LONG(0, AIRTIME - 1)).
constexpr auto kAirSeconds = static_cast<std::uint64_t>(kAirTime.count());
/// How long after slime hurts a player lava does not (the dmgtime slime sets).
constexpr nx::Seconds kHarmfulWaterRespite{1.0F};

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

Breath::Breath(const BreathRules& rules, const PlayerHarmRules& harm) noexcept
    : m_rules{&rules}, m_harm{&harm} {}

void Breath::fill(const nx::game::TickTime& time) noexcept {
    m_airRunsOut = time.now.after(kAirTime, time.rate);
    m_drowning = m_harm->drowning.start;
    m_isInWater = false;
}

auto Breath::breathe(WaterLevel level, nx::collision::Contents waterType, MoveType moveType,
                     TimedHarms& timed, const nx::game::TickTime& time, nx::Random& random)
    -> std::vector<BreathEvent> {
    if (moveType == MoveType::Noclip) {
        return {};
    }
    std::vector<BreathEvent> events;
    const auto made = [&time](const GameSound& sound) {
        return SoundMade{.sound = &sound, .at = time.now, .volume = sound.volume};
    };
    const DrowningDamage& drowning = m_harm->drowning;
    if (level != WaterLevel::Eyes) {
        if (m_airRunsOut < time.now) {
            events.emplace_back(made(m_rules->longGasp));
        } else if (m_airRunsOut < time.now.after(kGaspingAir, time.rate)) {
            events.emplace_back(made(m_rules->shortGasp));
        }
        m_airRunsOut = time.now.after(kAirTime, time.rate);
        m_drowning = drowning.start;
        timed.headOutOfWater();
    } else {
        const bool hasAir = !(time.now > m_airRunsOut);
        timed.headUnderWater(hasAir);
        if (!hasAir && time.now > m_nextDrowning) {
            m_drowning = std::min(m_drowning + drowning.step, drowning.most);
            events.emplace_back(PlayerHarm{.damage = m_drowning, .kind = HarmKind::Drown});
            m_nextDrowning = time.now.after(drowning.interval, time.rate);
            timed.drowned(m_drowning);
        }
    }
    if (level == WaterLevel::Dry) {
        m_isInWater = false;
        return events;
    }
    // With the head out of water the air is whole: no second of it is drawn above its 11, and it does not
    // bubble. Parity: the original's float time sometimes makes it a little less than whole, so that one
    // wading within 12 seconds before a power of two of seconds bubbles now and then; the game's time
    // (docs/07 section 1) does not.
    const float airLeft = std::trunc(m_airRunsOut.since(time.now, time.rate).count());
    if (random.below(kBubbleOneIn) == 0 && !(static_cast<float>(random.below(kAirSeconds)) < airLeft)) {
        events.emplace_back(made(m_rules->bubbles));
    }
    const auto depth = static_cast<float>(std::to_underlying(level));
    if (waterType == nx::collision::Contents::Lava) {
        if (m_harmfulWaterTime < time.now) {
            events.emplace_back(
                PlayerHarm{.damage = m_harm->harmfulWater.lava * nx::Scale{depth}, .kind = HarmKind::Burn});
        }
    } else if (waterType == nx::collision::Contents::Slime) {
        m_harmfulWaterTime = time.now.after(kHarmfulWaterRespite, time.rate);
        events.emplace_back(
            PlayerHarm{.damage = m_harm->harmfulWater.slime * nx::Scale{depth}, .kind = HarmKind::Acid});
    }
    if (!m_isInWater) {
        m_isInWater = true;
        m_harmfulWaterTime = nx::GameTime{};
    }
    return events;
}

} // namespace cs
