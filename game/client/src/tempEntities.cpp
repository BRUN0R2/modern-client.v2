// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.collision;
import nx.render;
import cs.shared;

namespace cs {

namespace {

/// The stream of random numbers the temporary entities draw from.
constexpr std::string_view kTempEntityStream = "temporary entities";
/// The widest roll a temporary entity is drawn at: a whole turn.
constexpr float kWholeTurnDegrees = 360.0F;

[[nodiscard]] auto colorOf(const nx::assets::Palette& palette, std::uint32_t entry)
    -> nx::render::RenderColor {
    const nx::assets::PaletteColor color = palette.color(nx::narrow<std::uint8_t>(entry));
    return nx::render::RenderColor{.red = color.red, .green = color.green, .blue = color.blue};
}

} // namespace

auto pathOf(TempSprite sprite, const BloodSprayLook& look) noexcept -> std::string_view {
    switch (sprite) {
    case TempSprite::BloodSpray:
        return look.spraySprite;
    case TempSprite::BloodDrop:
    case TempSprite::Count:
        return look.dropSprite;
    }
    std::unreachable();
}

auto bloodSpriteFramesOf(const EffectSprites& sprites, const BloodSprayLook& look) -> BloodSpriteFrames {
    const auto framesOf = [&sprites, &look](TempSprite sprite) -> std::optional<std::uint32_t> {
        const std::optional<std::uint32_t> index = sprites.indexOf(pathOf(sprite, look));
        return index ? sprites.frameCountOf(*index) : std::nullopt;
    };
    return BloodSpriteFrames{.spray = framesOf(TempSprite::BloodSpray),
                             .drop = framesOf(TempSprite::BloodDrop)};
}

TempEntities::TempEntities(std::uint64_t seed) : random{seed, kTempEntityStream} {}

void TempEntities::sprayBlood(const BloodSpray& spray, const BloodSprayLook& look,
                              const nx::assets::Palette& palette, const BloodSpriteFrames& frames,
                              nx::Seconds now) {
    const auto size = static_cast<float>(spray.size);
    // The spray's color an entry 1 to colorShift after the server's, its drops' the one before it.
    const std::uint32_t impact =
        std::uint32_t{spray.color} + 1 + nx::narrow<std::uint32_t>(random.below(look.colorShift));
    if (frames.spray) {
        const auto lastFrame = static_cast<float>(*frames.spray - 1);
        const float lift = random.between(look.leastLift.value(), look.mostLift.value());
        const float scale =
            random.between(size * look.leastSprayScale.value(), size * look.mostSprayScale.value());
        entities.push_back(TempEntity{.sprite = TempSprite::BloodSpray,
                                      .origin = spray.position + nx::Vec3{.z = lift},
                                      .roll = nx::Degrees{random.between(0.0F, kWholeTurnDegrees)},
                                      // Its frames once in sprayTime.
                                      .framerate = nx::PerSecond{lastFrame / look.sprayTime.count()},
                                      .lastFrame = lastFrame,
                                      .scale = scale,
                                      .color = colorOf(palette, impact),
                                      .amount = look.amount,
                                      .goesAt = now + look.sprayTime});
    }
    if (!frames.drop) {
        return;
    }
    const std::uint32_t lastDropFrame = *frames.drop - 1;
    std::uint32_t drops = spray.size;
    for (std::uint32_t die = 0; die < look.extraDropDice; ++die) {
        drops += 1 + nx::narrow<std::uint32_t>(random.below(look.extraDropFaces));
    }
    for (std::uint32_t drop = 0; drop < drops; ++drop) {
        const float scale =
            random.between(size * look.leastDropScale.value(), size * look.mostDropScale.value());
        const float across = random.between(look.leastDropSpeed.value(), look.mostDropSpeed.value());
        const float along = random.between(look.leastDropSpeed.value(), look.mostDropSpeed.value());
        const float rise = random.between(look.leastDropRise.value(), look.mostDropRise.value());
        const float spin = random.between(look.leastDropSpin.value(), look.mostDropSpin.value());
        const float life = random.between(look.leastDropLife.count(), look.mostDropLife.count());
        // A frame from the second, the last for one beyond mostDropFrame.
        std::uint32_t frame =
            lastDropFrame == 0 ? 0 : 1 + nx::narrow<std::uint32_t>(random.below(lastDropFrame));
        if (frame > look.mostDropFrame) {
            frame = lastDropFrame;
        }
        entities.push_back(TempEntity{.sprite = TempSprite::BloodDrop,
                                      .origin = spray.position,
                                      .velocity = nx::Vec3{.x = across, .y = along, .z = rise},
                                      .gravity = look.dropGravity,
                                      .isStoppedByWorld = true,
                                      .roll = nx::Degrees{random.between(0.0F, kWholeTurnDegrees)},
                                      .spin = nx::DegreesPerSecond{spin},
                                      .frame = static_cast<float>(frame),
                                      .lastFrame = static_cast<float>(lastDropFrame),
                                      .scale = scale,
                                      .color = colorOf(palette, impact - 1),
                                      .amount = look.amount,
                                      .goesAt = now + nx::Seconds{life}});
    }
}

void TempEntities::update(nx::Seconds now, nx::Seconds elapsed, const nx::collision::BrushHulls& hulls,
                          nx::UnitsPerSecondSquared gravity) {
    std::erase_if(entities, [now](const TempEntity& entity) { return now >= entity.goesAt; });
    const float seconds = elapsed.count();
    for (TempEntity& entity : entities) {
        entity.frame = std::min(entity.frame + (entity.framerate.value() * seconds), entity.lastFrame);
        entity.roll = nx::normalizeAngle(entity.roll + nx::Degrees{entity.spin.value() * seconds});
        if (entity.velocity == nx::Vec3{} && entity.gravity == nx::Scale{}) {
            continue;
        }
        const nx::Vec3 moved = entity.origin + (entity.velocity * seconds);
        if (entity.isStoppedByWorld) {
            const nx::collision::Trace met = hulls.trace({.start = entity.origin, .end = moved});
            if (met.fraction < 1.0F) {
                // It rests where it met the world.
                entity.origin = met.end;
                entity.velocity = {};
                entity.gravity = {};
                entity.spin = {};
                continue;
            }
        }
        entity.origin = moved;
        entity.velocity.z -= gravity.value() * entity.gravity.value() * seconds;
    }
}

} // namespace cs
