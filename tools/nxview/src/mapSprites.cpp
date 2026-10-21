// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module tool.nxview;

import std;
import nx.core;
import nx.math;
import nx.assets;
import nx.rhi;
import nx.render;
import nx.collision;

namespace nxview {

namespace {

using nx::render::RenderEffect;
using nx::render::RenderMode;

constexpr std::string_view kSpriteExtension = ".spr";
/// The spawnflags of an env_sprite: it starts on though named; it turns off after its last frame.
constexpr int kStartsOn = 1;
constexpr int kPlaysOnce = 2;
/// The game's entities think ten times a second.
constexpr double kThinkInterval = 0.1;
/// The most a channel of `rendercolor` and `renderamt` is.
constexpr float kChannelTop = 255.0F;
/// How far short of a glow the view's line may stop and still see it.
constexpr float kGlowSightSlack = 8.0F;

/// The game class of `classname` that shows a sprite; none for another.
[[nodiscard]] auto spriteClassOf(std::string_view classname) noexcept -> std::optional<SpriteClass> {
    if (classname == "env_sprite") {
        return SpriteClass::Sprite;
    }
    if (classname == "env_glow") {
        return SpriteClass::Glow;
    }
    return classname == "cycler_sprite" ? std::optional{SpriteClass::Cycler} : std::nullopt;
}

/// `value` held to a channel, cut to a whole number as the entity's state carries it.
[[nodiscard]] auto channelOf(float value) noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(std::clamp(value, 0.0F, kChannelTop));
}

/// The render settings of `entity` into `sprite`; a mode or an effect the original does not number
/// is taken as Normal and None.
void readRendering(const nx::assets::BspEntity& entity, MapSprite& sprite) {
    const int mode = entity.findInteger("rendermode").value_or(0);
    const int effect = entity.findInteger("renderfx").value_or(0);
    sprite.mode = mode >= 0 && mode < std::to_underlying(RenderMode::Count) ? static_cast<RenderMode>(mode)
                                                                            : RenderMode::Normal;
    sprite.effect = effect >= 0 && effect < std::to_underlying(RenderEffect::Count)
                        ? static_cast<RenderEffect>(effect)
                        : RenderEffect::None;
    sprite.amount = channelOf(entity.findNumber("renderamt").value_or(0.0F));
    const nx::Vec3 color = entity.findVector("rendercolor").value_or(nx::Vec3{});
    sprite.color = {.red = channelOf(color.x), .green = channelOf(color.y), .blue = channelOf(color.z)};
}

} // namespace

auto mapSpritesOf(const nx::assets::BspView& map) -> std::vector<MapSprite> {
    std::vector<MapSprite> sprites;
    for (const nx::assets::BspEntity& entity : map.entities()) {
        const std::optional<SpriteClass> kind = spriteClassOf(entity.find("classname").value_or(""));
        const std::string_view model = entity.find("model").value_or("");
        if (!kind || model.size() < kSpriteExtension.size() ||
            !nx::equalsIgnoringCase(model.substr(model.size() - kSpriteExtension.size()), kSpriteExtension)) {
            continue;
        }
        MapSprite sprite{.model = std::string{model},
                         .kind = *kind,
                         .origin = entity.findVector("origin").value_or(nx::Vec3{}),
                         .scale = entity.findNumber("scale").value_or(0.0F),
                         .framerate = entity.findNumber("framerate").value_or(0.0F)};
        nx::Vec3 angles = anglesOf(entity);
        if (*kind == SpriteClass::Sprite) {
            // CSprite::Spawn: the editors set the yaw alone, which the game makes the roll.
            if (angles.y != 0.0F && angles.z == 0.0F) {
                angles = {.x = angles.x, .y = 0.0F, .z = angles.y};
            }
            const int flags = entity.findInteger("spawnflags").value_or(0);
            sprite.isOn = !entity.find("targetname") || (flags & kStartsOn) != 0;
            sprite.playsOnce = (flags & kPlaysOnce) != 0;
        }
        sprite.angles = {
            .pitch = nx::Degrees{angles.x}, .yaw = nx::Degrees{angles.y}, .roll = nx::Degrees{angles.z}};
        readRendering(entity, sprite);
        sprites.push_back(std::move(sprite));
    }
    return sprites;
}

auto frameAt(const MapSprite& sprite, std::size_t frameCount, nx::Seconds sinceStart)
    -> std::optional<float> {
    if (!sprite.isOn) {
        return std::nullopt;
    }
    const double lastFrame = static_cast<double>(frameCount) - 1.0;
    const double framerate = static_cast<double>(sprite.framerate);
    const bool animates = sprite.kind == SpriteClass::Sprite
                              ? (framerate != 0.0 && lastFrame > 1.0) || sprite.playsOnce
                          : sprite.kind == SpriteClass::Glow ? framerate != 0.0 && lastFrame > 1.0
                                                             : lastFrame > 1.0;
    if (!animates) {
        return 0.0F;
    }
    // Each think adds the frames of a tenth of a second: an env_sprite's first at once adds none, the
    // others' first a tenth in.
    const double thinks = std::floor(std::max(static_cast<double>(sinceStart.count()), 0.0) / kThinkInterval);
    double frame = thinks * framerate * kThinkInterval;
    if (sprite.kind == SpriteClass::Sprite) {
        if (frame > lastFrame) {
            if (sprite.playsOnce) {
                return std::nullopt; // it turned off
            }
            frame = lastFrame > 0.0 ? std::fmod(frame, lastFrame) : frame;
        }
    } else if (lastFrame > 0.0) {
        frame = std::fmod(frame, lastFrame);
    }
    return static_cast<float>(frame);
}

GlowBlockers::GlowBlockers(const nx::assets::BspView& map) : m_isGlass(map.models().size(), false) {
    for (const nx::assets::BspEntity& entity : map.entities()) {
        const std::optional<nx::assets::ModelIndex> model = entity.findBrushModel();
        if (model && model->value < m_isGlass.size()) {
            m_isGlass[model->value] = entity.findInteger("rendermode").value_or(0) != 0;
        }
    }
}

auto GlowBlockers::hides(const nx::collision::BrushHulls& hulls,
                         std::span<const nx::render::ModelPlacement> placements, nx::Vec3 eye,
                         nx::Vec3 point) const -> bool {
    // A point's line, by the client's rules of movement (PM_PlayerTraceExt), stopped by the nearest.
    float fraction =
        hulls.traceMovement({.hull = nx::collision::Hull::Point, .start = eye, .end = point}).fraction;
    for (const nx::render::ModelPlacement& placement : placements) {
        if (placement.model.value < m_isGlass.size() && !m_isGlass[placement.model.value]) {
            fraction = std::min(fraction, hulls
                                              .traceMovement({.model = placement.model,
                                                              .hull = nx::collision::Hull::Point,
                                                              .offset = placement.origin,
                                                              .angles = placement.angles,
                                                              .start = eye,
                                                              .end = point})
                                              .fraction);
        }
    }
    return (1.0F - fraction) * nx::distance(eye, point) > kGlowSightSlack;
}

auto MapSprites::read(const nx::assets::Vfs& vfs, const nx::assets::BspView& map) -> MapSprites {
    MapSprites sprites;
    std::map<std::string, std::optional<std::size_t>, std::less<>> files; // by path; none: left out
    for (MapSprite& sprite : mapSpritesOf(map)) {
        auto [found, isNew] = files.try_emplace(sprite.model | std::views::transform(nx::toLowerAscii) |
                                                std::ranges::to<std::string>()); // the VFS ignores case
        if (isNew) {
            auto file = std::make_unique<File>(File{.path = sprite.model});
            const auto path = nx::assets::VfsPath::parseGoldsrc(sprite.model);
            auto bytes = path.and_then([&vfs](const nx::assets::VfsPath& at) { return vfs.readAll(at); });
            if (!bytes) {
                sprites.m_missing.push_back(
                    std::format("{} ({})", sprite.model, nx::describe(bytes.error())));
                continue;
            }
            file->bytes = std::move(*bytes);
            auto view = nx::assets::SpriteView::parse(file->bytes);
            if (!view) {
                sprites.m_missing.push_back(std::format("{} ({})", sprite.model, nx::describe(view.error())));
                continue;
            }
            file->view = std::move(*view);
            found->second = sprites.m_files.size();
            sprites.m_files.push_back(std::move(file));
        }
        if (found->second) {
            sprites.m_entities.emplace_back(std::move(sprite), *found->second);
        }
    }
    return sprites;
}

auto MapSprites::load(nx::render::SpriteRenderer& renderer, nx::rhi::CommandList& commands) const
    -> nx::Result<std::vector<nx::render::SpriteModel>, nx::render::SpriteError> {
    std::vector<nx::render::SpriteModel> models;
    models.reserve(m_files.size());
    for (const std::unique_ptr<File>& file : m_files) {
        auto model = renderer.load(commands, *file->view, file->path);
        if (!model) {
            return std::unexpected{std::move(model.error())};
        }
        models.push_back(std::move(*model));
    }
    return models;
}

auto MapSprites::instances(std::span<const nx::render::SpriteModel> models, nx::Seconds sinceStart) const
    -> std::vector<nx::render::SpriteInstance> {
    nx::expects(models.size() == m_files.size(), "MapSprites::instances: a model for each file");
    std::vector<nx::render::SpriteInstance> shown;
    shown.reserve(m_entities.size());
    for (const auto& [sprite, file] : m_entities) {
        if (file >= models.size()) {
            continue; // the contract's observe path
        }
        const nx::render::SpriteModel& model = models[file];
        const std::optional<float> frame = frameAt(sprite, model.frameCount(), sinceStart);
        if (!frame) {
            continue;
        }
        shown.push_back(nx::render::SpriteInstance{.model = model,
                                                   .origin = sprite.origin,
                                                   .angles = sprite.angles,
                                                   .frame = *frame,
                                                   .scale = sprite.scale,
                                                   .mode = sprite.mode,
                                                   .effect = sprite.effect,
                                                   .amount = sprite.amount,
                                                   .color = sprite.color});
    }
    return shown;
}

} // namespace nxview
