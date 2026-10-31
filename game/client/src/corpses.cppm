// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How long a client shows the dead (docs/10-counter-strike-gameplay.md, section 7.4): `cl_corpsestay`,
/// as Counter-Strike's client has it, the seconds a dead body stays, from 0 to 600 and 600 by default; then
/// it fades out over kCorpseFade and is drawn no more. Each body's clock starts as the client first sees
/// it dead, whatever told it: a player's state, an entity's health. The game goes on with the bodies as
/// it does; this is what the client draws of them.
export module cs.client:corpses;

import std;
import nx.core;
import nx.console;
import nx.render;

export namespace cs {

/// The longest a corpse stays, `cl_corpsestay`'s most, and its default, as Counter-Strike's.
inline constexpr nx::Seconds kLongestCorpseStay{600.0F};
/// How long a corpse fades out once its stay is over: the project's own, the original's not measured.
inline constexpr nx::Seconds kCorpseFade{1.0F};
/// How opaque a body is drawn while it shows wholly: the most of a byte.
inline constexpr std::uint8_t kWholeBody = std::numeric_limits<std::uint8_t>::max();

/// How a body drawn `amount` opaque is drawn: as its model is when wholly, else blended by its amount.
[[nodiscard]] constexpr auto renderModeOf(std::uint8_t amount) noexcept -> nx::render::RenderMode {
    return amount == kWholeBody ? nx::render::RenderMode::Normal : nx::render::RenderMode::TransTexture;
}

/// The cvar of how long the dead stay, archived, the player's.
struct CorpseCvars {
    nx::console::Cvar<float> stay; ///< `cl_corpsestay`: the seconds a dead body stays.

    /// Registers it in `console`. Errors: those of Console::registerCvar.
    [[nodiscard]] static auto registerIn(nx::console::Console& console)
        -> nx::Result<CorpseCvars, nx::console::CvarError>;
    /// How long a corpse stays: cl_corpsestay's, held from none to kLongestCorpseStay, none for a value
    /// that is not a number.
    [[nodiscard]] auto stayTime() const noexcept -> nx::Seconds;
};

/// How opaque, of 255, a body is drawn that died `dead` ago, none for one alive, its corpse staying
/// `stay`: wholly while it lives and through its stay, then less and less over kCorpseFade, then not at
/// all.
[[nodiscard]] auto corpseAmountOf(std::optional<nx::Seconds> dead, nx::Seconds stay) noexcept -> std::uint8_t;

/// When the bodies a client shows died, by a key of each (a player's index, an entity's handle), as the
/// client first saw them dead. A body seen alive again, a player respawned, is forgotten, its next death
/// timed anew; so is one no longer asked about, gone from what the client shows.
template <class Key>
class CorpseClock {
public:
    /// How long ago `body`, seen dead or not (`isDead`), died at `now`, the game's ticks of `rate`: none
    /// while it lives; since `now` for one first seen dead now.
    [[nodiscard]] auto deadFor(const Key& body, bool isDead, nx::GameTime now, nx::TickRate rate)
        -> std::optional<nx::Seconds> {
        if (!isDead) {
            corpses.erase(body);
            return std::nullopt;
        }
        const auto corpse = corpses.try_emplace(body, Corpse{.diedAt = now}).first;
        corpse->second.isAsked = true;
        return now.since(corpse->second.diedAt, rate);
    }
    /// Forgets the bodies not asked about since the last call.
    void forgetUnasked() {
        std::erase_if(corpses, [](const auto& entry) { return !entry.second.isAsked; });
        for (auto& entry : corpses) {
            entry.second.isAsked = false;
        }
    }
    /// How many corpses it times.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return corpses.size(); }

private:
    struct Corpse {
        nx::GameTime diedAt{};
        bool isAsked = true;
    };
    std::map<Key, Corpse> corpses;
};

} // namespace cs
