// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.console;
import nx.render;
import cs.shared;
import cs.client;

namespace app {

namespace {

constexpr nx::log::Category kLog{"client"};

/// Reports `played` when it failed and `failures` allows.
void reportSound(const nx::Result<void, cs::PlayError>& played, nx::log::RateLimit& failures) {
    if (!played && failures.allow()) {
        nx::log::warning(kLog, "a sound not played: {} ({} others not played since the last report)",
                         nx::describe(played.error()), failures.takeSuppressed());
    }
}

} // namespace

void hearSession(Session& session, cs::ClientAudio* audio, const cs::Hearing& hearing,
                 nx::log::RateLimit& soundFailures) {
    session.viewPose = session.client.viewModel();
    session.clientEvents = session.viewModelEvents.passed(
        session.viewPose ? session.level->viewModel(session.viewPose->weapon) : nullptr, session.viewPose);
    const std::vector<cs::SoundMade> sounds = session.client.takeSounds();
    if (audio == nullptr) {
        return;
    }
    for (const cs::SoundMade& made : sounds) {
        reportSound(audio->play(*made.sound, made.volume, hearing.delayOf(made.at)), soundFailures);
    }
    for (const cs::PassedEvent& passed : session.clientEvents) {
        if (passed.event->event == cs::kSoundEvent) {
            const nx::GameTime at = session.viewPose->start.after(passed.intoPlay, hearing.rate);
            reportSound(audio->playLocal(passed.event->options, kFullVolume, hearing.delayOf(at)),
                        soundFailures);
        }
    }
}

void stampMarks(Session& session, cs::Scene& scene, const cs::GameContent& content, nx::Random& bulletHoles) {
    const std::vector<cs::Impact> impacts = session.client.takeImpacts();
    const std::vector<cs::BloodMark> bloodMarks = session.client.takeBloodMarks();
    const cs::Level& level = *session.level;
    const auto stamp = [&scene, &level](std::string_view name, nx::Vec3 position, const cs::StampTarget& on) {
        // One the installation lacks was reported as the level was read.
        if (const std::optional<nx::render::DecalImageIndex> decal = level.decals().indexOf(name)) {
            std::ignore = scene.shootDecal(*decal, position, cs::decalTargetOf(on));
        }
    };
    for (const cs::Impact& impact : impacts) {
        if (const std::optional<std::string_view> name =
                cs::bulletDecalOf(level.bulletMarkOn(impact.on.model), bulletHoles)) {
            stamp(*name, impact.position, impact.on);
        }
    }
    const std::vector<std::string>& bloodDecals = content.blood.decals;
    for (const cs::BloodMark& mark : bloodMarks) {
        nx::expects(mark.decal < bloodDecals.size(), "stampMarks: a blood decal of the game's content");
        stamp(bloodDecals[mark.decal], mark.position, mark.on);
    }
}

void showNotices(Session& session, const FrameNotices& notices, const cs::WeaponCatalog& weapons,
                 nx::Seconds now) {
    const std::vector<cs::DamageDealt> dealt = session.client.takeDamageDealt();
    const std::vector<cs::EntityStep> steps = session.client.takeEntitySteps();
    const std::vector<cs::KillNotice> kills = session.client.takeKillNotices();
    if (notices.killFeed) {
        notices.killFeed->show(kills, weapons, now);
    }
    for (const cs::KillNotice& kill : kills) {
        notices.console.print(cs::killLineOf(
            kill,
            kill.weapon ? std::optional<std::string_view>{weapons.weapon(*kill.weapon).id} : std::nullopt));
    }
    if (notices.damageNumbers && notices.damageNumberCvars.shown.value()) {
        notices.damageNumbers->show(dealt, now);
    }
    if (notices.stepIndicator && notices.stepIndicatorCvars.shown.value()) {
        notices.stepIndicator->hear(steps, now);
    }
}

void showEffects(Session& session, const cs::Scene& scene, const cs::GameContent& content, nx::Seconds now,
                 nx::Seconds elapsed) {
    const std::vector<cs::BloodSpray> sprays = session.client.takeBloodSprays();
    const std::vector<cs::BloodStream> streams = session.client.takeBloodStreams();
    const std::vector<cs::StreakSplash> splashes = session.client.takeStreakSplashes();
    const cs::Level& level = *session.level;
    const nx::UnitsPerSecondSquared gravity = cs::MovementSettings{}.gravity;
    session.effects.update(now, elapsed, level.hulls(), gravity);
    session.particles.update(now, elapsed, gravity);
    const cs::BloodEnhancedLook& enhancedLook = content.blood.enhancedLook;
    session.blood.update(now, elapsed, level.hulls(), gravity, enhancedLook);
    for (const cs::StreakSplash& splash : splashes) {
        session.particles.splashStreaks(splash, now);
    }
    const std::optional<nx::assets::Palette>& palette = level.palette();
    if (!palette) {
        return;
    }
    if (scene.look() == nx::render::StudioLook::Enhanced) {
        const nx::render::LightStyleValues styles = session.client.lightStylesAt(now);
        for (const cs::BloodSpray& spray : sprays) {
            const nx::assets::PaletteColor color = palette->color(spray.color);
            session.blood.spray(spray, enhancedLook,
                                {.color = {.red = color.red, .green = color.green, .blue = color.blue},
                                 .light = scene.bloodLightAt(level, spray.position, styles)},
                                now);
        }
        return;
    }
    const cs::BloodSprayLook& look = content.blood.sprayLook;
    const cs::BloodSpriteFrames frames = cs::bloodSpriteFramesOf(level.bloodSprites(), look);
    for (const cs::BloodSpray& spray : sprays) {
        session.effects.sprayBlood(spray, look, *palette, frames, now);
    }
    for (const cs::BloodStream& stream : streams) {
        session.particles.streamBlood(stream, content.blood.streamLook, now);
    }
}

} // namespace app
