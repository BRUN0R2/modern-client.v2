// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the client makes of a frame its session played: what the player hears of it, the marks it leaves on
/// the world, and the notices and the effects it shows.
export module app.client:sessionFrame;

import std;
import nx.core;
import nx.console;
import cs.shared;
import cs.client;
import :session;

namespace app {

/// The volume `play` plays a sound at, and the view model's sound events: as it is.
inline constexpr float kFullVolume = 1.0F;

/// What the player hears of the frame `session` played on `audio`, when there is a sound output, as
/// `hearing` says when: the sounds its weapons made and its view model's sound events, each as long after the
/// frame as it came after what the last frame showed; and the client events its view model passed, which the
/// frame shows. A sound not played is reported when `soundFailures` allows.
void hearSession(Session& session, cs::ClientAudio* audio, const cs::Hearing& hearing,
                 nx::log::RateLimit& soundFailures);

/// Leaves the marks of the frame `session` played where they met the world or a brush entity, once `scene`
/// is drawn: a bullet's where each shot met it, the decal the mark of what it met leaves (cs::bulletDecalOf),
/// of those `bulletHoles` chooses among; then blood's where the blood a hit shed met it, the decal the server
/// chose.
void stampMarks(Session& session, cs::Scene& scene, const cs::GameContent& content, nx::Random& bulletHoles);

/// What tells the player what happened in a frame; those it has no means of showing are none.
struct FrameNotices {
    nx::console::Console& console;
    std::optional<cs::KillFeed>& killFeed;
    std::optional<cs::DamageNumbers>& damageNumbers;
    const cs::DamageNumberCvars& damageNumberCvars;
    std::optional<cs::StepIndicator>& stepIndicator;
    const cs::StepIndicatorCvars& stepIndicatorCvars;
};

/// Shows on `notices` the deaths of the frame `session` played, the weapons they name of `weapons`, the
/// damage its hits dealt and the steps heard, at `now`.
void showNotices(Session& session, const FrameNotices& notices, const cs::WeaponCatalog& weapons,
                 nx::Seconds now);

/// Moves the temporary entities, particles and blood of `session` over `elapsed`, then shows the splashes of
/// streaks since the last frame, the helmets' sparks (cs::Particles::splashStreaks), and the blood the hits
/// sprayed: in the enhanced look as its own (cs::BloodEffects::spray), of its palette entry's color and lit
/// where it met the body (cs::Scene::bloodLightAt), its sprays and their heads' gushes in place of the
/// original's streams; else as the original's (cs::TempEntities::sprayBlood), and the streams the hits in the
/// head made (cs::Particles::streamBlood); the blood but without the engine's palette, as the level logged.
void showEffects(Session& session, const cs::Scene& scene, const cs::GameContent& content, nx::Seconds now,
                 nx::Seconds elapsed);

} // namespace app
