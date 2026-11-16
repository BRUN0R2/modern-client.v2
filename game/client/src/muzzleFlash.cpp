// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.render;
import cs.shared;

namespace cs {

namespace {

/// The muzzle flash events: the first attachment's, and the next attachment's this many after.
constexpr std::int32_t kFirstMuzzleFlashEvent = 5001;
constexpr std::int32_t kMuzzleFlashEventStep = 10;
constexpr std::uint32_t kMuzzleFlashAttachments = 4;
/// An event's number: its units choose the sprite, its tens are its size in tenths.
constexpr std::uint32_t kDecimalBase = 10;
constexpr nx::Scale kScalePerTen{0.1F};
constexpr nx::Scale kScaleWithoutTens{0.5F};
/// The original's sprites, sprites/muzzleflash1.spr to 3.
constexpr std::uint32_t kOriginalSprites = 3;
/// The roll of the first sprite's flash, a rifle's, and of the others', at most.
constexpr nx::Degrees kRifleRoll{20.0F};
constexpr nx::Degrees kOtherRoll{359.0F};

/// Counter-Strike Online's options start with this; then keys, a letter each, and their values, apart.
constexpr char kKeyedOptions = '#';
constexpr char kKeySeparator = ' ';
constexpr char kSpriteKey = 'I';
constexpr char kScaleKey = 'S';
constexpr char kRollKey = 'R';
constexpr char kFirstFrameKey = 'F';
constexpr char kFrameRateKey = 'P';
constexpr char kTimeKey = 'T';

/// The sprite of number `number`: sprites/muzzleflash<number>.spr.
[[nodiscard]] auto spriteOf(std::uint32_t number) -> std::string {
    return std::format("sprites/muzzleflash{}.spr", number);
}

/// `text` as a number of 0 or more, of `Number`; none when it is not one, whole.
template <class Number>
[[nodiscard]] auto numberOf(std::string_view text) noexcept -> std::optional<Number> {
    Number value{};
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        return std::nullopt;
    }
    if constexpr (std::floating_point<Number>) {
        if (!(value >= Number{})) {
            return std::nullopt; // below 0, or not a number
        }
    }
    return value;
}

/// The style of the original's options `type`, a whole number (R_MuzzleFlash).
[[nodiscard]] auto originalStyleOf(std::uint32_t type) -> MuzzleFlashStyle {
    const std::uint32_t sprite = (type % kDecimalBase) % kOriginalSprites;
    const std::uint32_t tensDigit = type / kDecimalBase;
    const nx::Scale tens = kScalePerTen * static_cast<float>(tensDigit);
    return MuzzleFlashStyle{.sprite = spriteOf(sprite + 1),
                            .scale = tens == nx::Scale{} ? kScaleWithoutTens : tens,
                            .firstFrame = std::nullopt,
                            .frameRate = FlashFrameRate{},
                            .time = kMuzzleFlashTime,
                            .roll = sprite == 0 ? kRifleRoll : kOtherRoll,
                            .rollRate = nx::DegreesPerSecond{},
                            .brightness = 1.0F,
                            .light = std::nullopt};
}

/// The style of Counter-Strike Online's `keys`, its options after the '#': each key it reads over
/// `style`; R, degrees a frame of its animation, its turn.
void readKeys(std::string_view keys, MuzzleFlashStyle& style) {
    std::optional<float> rollPerFrame;
    for (const auto part : std::views::split(keys, kKeySeparator)) {
        const std::string_view token{part.begin(), part.end()};
        if (token.empty()) {
            continue;
        }
        const std::string_view value = token.substr(1);
        switch (token.front()) {
        case kSpriteKey:
            if (const std::optional<std::uint32_t> number = numberOf<std::uint32_t>(value)) {
                style.sprite = spriteOf(*number);
            }
            break;
        case kScaleKey:
            if (const std::optional<float> scale = numberOf<float>(value)) {
                style.scale = nx::Scale{*scale};
            }
            break;
        case kFirstFrameKey:
            if (const std::optional<std::uint32_t> frame = numberOf<std::uint32_t>(value)) {
                style.firstFrame = frame;
            }
            break;
        case kFrameRateKey:
            if (const std::optional<float> frameRate = numberOf<float>(value)) {
                style.frameRate = FlashFrameRate{*frameRate};
            }
            break;
        case kTimeKey:
            if (const std::optional<float> seconds = numberOf<float>(value)) {
                style.time = nx::Seconds{*seconds};
            }
            break;
        case kRollKey:
            if (const std::optional<float> perFrame = numberOf<float>(value)) {
                rollPerFrame = perFrame;
            }
            break;
        default:
            break; // a key whose meaning is unknown
        }
    }
    if (rollPerFrame) {
        style.rollRate = nx::DegreesPerSecond{*rollPerFrame * style.frameRate.value()};
    }
}

/// `value` when given, else `otherwise`.
template <class Value>
void over(Value& otherwise, const std::optional<Value>& value) {
    if (value) {
        otherwise = *value;
    }
}

} // namespace

auto muzzleFlashAttachmentOf(std::int32_t event) noexcept -> std::optional<std::uint32_t> {
    const std::int32_t step = event - kFirstMuzzleFlashEvent;
    if (step < 0 || step % kMuzzleFlashEventStep != 0) {
        return std::nullopt;
    }
    const auto attachment = static_cast<std::uint32_t>(step / kMuzzleFlashEventStep);
    return attachment < kMuzzleFlashAttachments ? std::optional{attachment} : std::nullopt;
}

auto muzzleFlashStyleOf(std::string_view options, const std::optional<MuzzleFlashLook>& look)
    -> MuzzleFlashStyle {
    const bool isKeyed = options.starts_with(kKeyedOptions);
    MuzzleFlashStyle style = originalStyleOf(isKeyed ? 0 : numberOf<std::uint32_t>(options).value_or(0));
    if (isKeyed) {
        readKeys(options.substr(1), style);
    }
    if (look) {
        over(style.sprite, look->sprite);
        if (look->scale) {
            style.scale = nx::Scale{*look->scale};
        }
        if (look->firstFrame) {
            style.firstFrame = look->firstFrame;
        }
        if (look->frameRate) {
            style.frameRate = FlashFrameRate{*look->frameRate};
        }
        over(style.time, look->time);
        over(style.roll, look->roll);
        over(style.rollRate, look->rollRate);
        over(style.brightness, look->brightness);
        if (look->light) {
            style.light = look->light;
        }
    }
    return style;
}

auto MuzzleFlash::frameAt(nx::Seconds time) const noexcept -> std::optional<float> {
    if (time > until) {
        return std::nullopt;
    }
    const float frame = firstFrame + (std::max((time - start).count(), 0.0F) * frameRate.value());
    return frame < static_cast<float>(frameCount) ? std::optional{frame} : std::nullopt;
}

auto MuzzleFlash::rollAt(nx::Seconds time) const noexcept -> nx::Degrees {
    return nx::Degrees{roll.value() + (rollRate.value() * std::max((time - start).count(), 0.0F))};
}

auto MuzzleFlash::lightAt(nx::Seconds time, nx::Vec3 origin) const noexcept
    -> std::optional<nx::render::StudioLight> {
    const std::optional<float> left = lightLeftAt(time);
    if (!left) {
        return std::nullopt;
    }
    return nx::render::StudioLight{
        .position = origin,
        .reach = light->reach,
        .color = nx::Vec3{.x = light->color[0], .y = light->color[1], .z = light->color[2]} * *left};
}

auto MuzzleFlash::isOverAt(nx::Seconds time) const noexcept -> bool {
    return !frameAt(time) && !lightLeftAt(time);
}

auto MuzzleFlash::lightLeftAt(nx::Seconds time) const noexcept -> std::optional<float> {
    const float elapsed = std::max((time - start).count(), 0.0F);
    if (!light || elapsed >= light->time.count()) {
        return std::nullopt;
    }
    return 1.0F - (elapsed / light->time.count());
}

auto muzzleFlashOf(const MuzzleFlashStyle& style, std::uint32_t sprite, std::size_t frameCount,
                   MuzzleFlashAnchor anchor, nx::Seconds time, nx::Random& random)
    -> std::optional<MuzzleFlash> {
    const auto roll = nx::Degrees{static_cast<float>(random.below(
        static_cast<std::uint64_t>(std::clamp(style.roll.value(), 0.0F, kOtherRoll.value())) + 1))};
    const std::uint64_t frame = style.firstFrame ? *style.firstFrame : random.below(frameCount);
    if (frame >= frameCount) {
        return std::nullopt;
    }
    return MuzzleFlash{.anchor = anchor,
                       .sprite = sprite,
                       .scale = style.scale,
                       .roll = roll,
                       .rollRate = style.rollRate,
                       .brightness = style.brightness,
                       .firstFrame = static_cast<float>(frame),
                       .frameRate = style.frameRate,
                       .frameCount = frameCount,
                       .start = time,
                       .until = time + style.time,
                       .light = style.light};
}

void MuzzleFlashes::show(MuzzleFlash flash) {
    m_flashes.push_back(std::move(flash));
}

void MuzzleFlashes::forget(nx::Seconds time, std::optional<WeaponIndex> inHand) {
    std::erase_if(m_flashes, [time, inHand](const MuzzleFlash& shown) {
        return shown.isOverAt(time) || inHand != shown.anchor.weapon;
    });
}

auto MuzzleFlashes::lightsAt(nx::Seconds time, const AnchorPlace& placeOf) const
    -> std::vector<nx::render::StudioLight> {
    std::vector<std::pair<MuzzleFlashAnchor, nx::render::StudioLight>> byMuzzle;
    for (const MuzzleFlash& shown : m_flashes) {
        const std::optional<nx::Vec3> origin = placeOf(shown.anchor);
        const std::optional<nx::render::StudioLight> light =
            origin ? shown.lightAt(time, *origin) : std::nullopt;
        if (!light) {
            continue;
        }
        const auto same = std::ranges::find(byMuzzle, shown.anchor,
                                            &std::pair<MuzzleFlashAnchor, nx::render::StudioLight>::first);
        if (same == byMuzzle.end()) {
            byMuzzle.emplace_back(shown.anchor, *light);
            continue;
        }
        same->second.color = same->second.color + light->color;
        same->second.reach = std::max(same->second.reach, light->reach);
    }
    return byMuzzle | std::views::values | std::ranges::to<std::vector>();
}

} // namespace cs
