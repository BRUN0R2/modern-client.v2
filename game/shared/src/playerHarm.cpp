// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.data;
import nx.collision;
import nx.game;

namespace cs {

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

auto harmPlayer(PlayerStatus& status, nx::Damage damage) noexcept -> nx::game::Harm {
    nx::expects(std::isfinite(damage.value()) && damage >= nx::Damage{},
                "harmPlayer: a damage finite and not negative");
    if (!status.isAlive()) {
        return {};
    }
    const nx::Damage taken{static_cast<float>(nx::truncateAsX86(damage.value()))};
    status.health = status.health - taken;
    return nx::game::Harm{.damage = taken, .isKilled = !status.isAlive()};
}

} // namespace cs
