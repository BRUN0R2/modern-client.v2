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

/// How many looks the kept harms with a count are kept after they are first looked at (the original's
/// drowning recovery's 4, ACID_DURATION).
constexpr std::uint8_t kGivingBackLooks = 4;
constexpr std::uint8_t kAcidLooks = 2;

} // namespace

auto PlayerHarmRules::soundFiles() const -> std::vector<std::string_view> {
    std::vector<std::string_view> files;
    for (const GameSound* const sound : {&pain, &splat, &death}) {
        for (const std::string& file : sound->files) {
            if (!std::ranges::contains(files, std::string_view{file})) {
                files.emplace_back(file);
            }
        }
    }
    return files;
}

auto readPlayerHarmRules(std::string_view text, std::string_view sourceName)
    -> nx::Result<PlayerHarmRules, PlayerHarmError> {
    auto rules = nx::data::parseToml<PlayerHarmRules>(text, sourceName);
    if (!rules) {
        return nx::fail(PlayerHarmError::DataInvalid, std::string{sourceName}, std::move(rules.error()));
    }
    if (!(rules->fall.fatalSpeed > rules->fall.safeSpeed)) {
        return nx::fail(PlayerHarmError::DataInvalid,
                        std::format("{}: the fatal speed of a fall is not above its safe speed", sourceName));
    }
    for (const auto& [what, sound] :
         {std::pair{"the pain", &rules->pain}, std::pair{"the splat", &rules->splat},
          std::pair{"the death", &rules->death}}) {
        if (const std::optional<std::string_view> problem = soundProblem(*sound)) {
            return nx::fail(PlayerHarmError::DataInvalid,
                            std::format("{}: {} {}", sourceName, what, *problem));
        }
    }
    return std::move(*rules);
}

auto fallDamageOf(const FallDamage& rules, nx::UnitsPerSecond fall) noexcept -> nx::Damage {
    // DAMAGE_FOR_FALL_SPEED, a float constant.
    const float perSpeed =
        rules.damageAtFatalSpeed.value() / (rules.fatalSpeed.value() - rules.safeSpeed.value());
    const float beyond = fall.value() - rules.safeSpeed.value();
    return nx::Damage{static_cast<float>(double{beyond * perSpeed} * rules.scale)};
}

auto landingDamageOf(const FallDamage& rules, const MovementState& before,
                     const MovementState& after) noexcept -> std::optional<nx::Damage> {
    // The original's PreThink keeps the fall of a player in the air; its PostThink weighs it once the
    // player stands, and a landing in water hurts it none (slime and lava do not spare it).
    if (before.isOnGround || !after.isOnGround || after.waterType == nx::collision::Contents::Water) {
        return std::nullopt;
    }
    const nx::UnitsPerSecond fall{-before.velocity.z};
    if (!(fall > rules.safeSpeed)) {
        return std::nullopt;
    }
    const nx::Damage damage = fallDamageOf(rules, fall);
    return damage > nx::Damage{} ? std::optional{damage} : std::nullopt;
}

void TimedHarms::took(HarmKind kind) noexcept {
    if (kind == HarmKind::Drown) {
        m_kept.set(Kept::Drowning);
    } else if (kind == HarmKind::Acid) {
        m_kept.set(Kept::Acid);
        m_looksLeft[Kept::Acid] = 0;
    }
}

void TimedHarms::drowned(nx::Damage damage) noexcept {
    m_drowned += nx::truncateAsX86(damage.value());
}

void TimedHarms::headOutOfWater() noexcept {
    if (m_drowned > m_givenBack) {
        // Parity: its looks are counted anew each command while some is not given back, so that the
        // giving back goes on until all is
        m_looksLeft[Kept::GivingBack] = 0;
        m_kept.set(Kept::GivingBack);
        m_kept.reset(Kept::Drowning);
    }
}

void TimedHarms::headUnderWater(bool hasAir) noexcept {
    m_looksLeft[Kept::GivingBack] = 0;
    m_kept.reset(Kept::GivingBack);
    if (hasAir) {
        m_kept.reset(Kept::Drowning);
    }
}

void TimedHarms::spawned() noexcept {
    m_kept.reset(Kept::Drowning);
    m_kept.reset(Kept::GivingBack);
    m_looksLeft[Kept::GivingBack] = 0;
    m_drowned = 0;
    m_givenBack = 0;
}

auto TimedHarms::check(nx::HitPoints mostGivenBack, nx::GameTime now, nx::TickRate rate) noexcept
    -> nx::HitPoints {
    if (m_kept.none() || now.since(m_lastLook, rate) < kKeptHarmsInterval) {
        return nx::HitPoints{};
    }
    m_lastLook = now;
    nx::HitPoints given{};
    for (const auto& [kept, looks] :
         {std::pair{Kept::GivingBack, kGivingBackLooks}, std::pair{Kept::Acid, kAcidLooks}}) {
        if (!m_kept.test(kept)) {
            continue;
        }
        if (kept == Kept::GivingBack && m_drowned > m_givenBack) {
            const std::int32_t back =
                std::min(m_drowned - m_givenBack, nx::truncateAsX86(mostGivenBack.value()));
            given = nx::HitPoints{given.value() + static_cast<float>(back)};
            m_givenBack += back;
        }
        std::uint8_t& left = m_looksLeft[kept];
        if (left == 0) {
            left = looks;
        } else if (--left == 0) {
            m_kept.reset(kept);
        }
    }
    return given;
}

auto harmPlayer(PlayerStatus& status, TimedHarms& timed, const PlayerHarm& harm) noexcept -> nx::game::Harm {
    nx::expects(std::isfinite(harm.damage.value()) && harm.damage >= nx::Damage{},
                "harmPlayer: a damage finite and not negative");
    if (!status.isAlive()) {
        return {};
    }
    const nx::Damage taken{static_cast<float>(nx::truncateAsX86(harm.damage.value()))};
    status.health = status.health - taken;
    // Kept even when it kills, as TakeDamage keeps it once Killed is over.
    timed.took(harm.kind);
    return nx::game::Harm{.damage = taken, .isKilled = !status.isAlive()};
}

void healPlayer(PlayerStatus& status, nx::HitPoints health) noexcept {
    nx::expects(health >= nx::HitPoints{}, "healPlayer: a health not negative");
    if (status.isAlive() && status.health < kSpawnHealth) {
        status.health = std::min(nx::HitPoints{status.health.value() + health.value()}, kSpawnHealth);
    }
}

} // namespace cs
