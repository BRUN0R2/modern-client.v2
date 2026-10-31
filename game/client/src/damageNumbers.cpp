// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.rhi;
import nx.render;
import nx.ui;
import nx.console;
import cs.shared;

namespace cs {

namespace {

/// The characters a number writes.
constexpr std::u32string_view kDigits = U"0123456789";
/// The stream of random numbers the numbers' sides are drawn from.
constexpr std::string_view kSideStream = "damage number side";
/// The ways about its digits a number's outline is drawn, a unit each: the four sides and the four
/// corners, so that the outline is as thick all round.
constexpr float kDiagonal = std::numbers::sqrt2_v<float> * 0.5F;
constexpr std::array<std::array<float, 2>, 8> kOutlineWays{{{1.0F, 0.0F},
                                                            {-1.0F, 0.0F},
                                                            {0.0F, 1.0F},
                                                            {0.0F, -1.0F},
                                                            {kDiagonal, kDiagonal},
                                                            {-kDiagonal, kDiagonal},
                                                            {kDiagonal, -kDiagonal},
                                                            {-kDiagonal, -kDiagonal}}};

/// The share `part` is of `whole`, from 0 to 1; all of it for a whole of none.
[[nodiscard]] auto shareOf(nx::Seconds part, nx::Seconds whole) noexcept -> float {
    if (whole <= nx::Seconds{}) {
        return 1.0F;
    }
    return std::clamp(part / whole, 0.0F, 1.0F);
}

/// `share` eased out: fast, then slowing down to its end (a cubic).
[[nodiscard]] auto easedOut(float share) noexcept -> float {
    const float left = 1.0F - share;
    return 1.0F - (left * left * left);
}

/// `share` eased in and out: slow, fast, slow (smoothstep).
[[nodiscard]] auto eased(float share) noexcept -> float {
    return share * share * (3.0F - (2.0F * share));
}

[[nodiscard]] auto colorOf(DamageNumberColor color, float opacity) noexcept -> nx::render::ScreenColor {
    return {.red = color.red,
            .green = color.green,
            .blue = color.blue,
            .alpha = static_cast<std::uint8_t>(std::lround(std::clamp(opacity, 0.0F, 1.0F) * 255.0F))};
}

} // namespace

auto DamageNumberCvars::registerIn(nx::console::Console& console)
    -> nx::Result<DamageNumberCvars, nx::console::CvarError> {
    auto shown = console.registerCvar(nx::console::CvarDesc<bool>{
        .name = "hud_damagenumbers",
        .defaultValue = true,
        .description = "Whether the damage of your hits shows over what they hit, 1 or 0.",
        .flags = {nx::console::CvarFlag::Archive}});
    if (!shown) {
        return std::unexpected{std::move(shown.error())};
    }
    return DamageNumberCvars{.shown = std::move(*shown)};
}

auto damageNumberPoseAt(const DamageNumberLook& look, nx::Seconds age) noexcept
    -> std::optional<DamageNumberPose> {
    if (age >= look.life) {
        return std::nullopt;
    }
    age = std::max(age, nx::Seconds{});
    // From small it pops up past its whole size, then settles back to it.
    float scale =
        age < look.growTime
            ? look.startScale + ((look.peakScale - look.startScale) * easedOut(shareOf(age, look.growTime)))
            : look.peakScale +
                  ((1.0F - look.peakScale) * eased(shareOf(age - look.growTime, look.settleTime)));
    float opacity = eased(shareOf(age, look.fadeIn));
    // Over its last moments, it fades out shrinking.
    const nx::Seconds fadeStart = look.life - look.fadeOut;
    if (age > fadeStart) {
        const float faded = eased(shareOf(age - fadeStart, look.fadeOut));
        opacity *= 1.0F - faded;
        scale *= 1.0F + ((look.endScale - 1.0F) * faded);
    }
    return DamageNumberPose{.scale = scale, .opacity = opacity, .rise = easedOut(shareOf(age, look.life))};
}

auto damageNumberMiddleOf(const DamageNumberLook& look, const DamageNumberPose& pose, float side,
                          nx::render::ScreenPoint headTop, float screenHeight, float footBelowMiddle) noexcept
    -> nx::render::ScreenPoint {
    return {.x = headTop.x + (side * look.spread * screenHeight),
            .y = headTop.y - (look.gap * screenHeight) - footBelowMiddle -
                 (pose.rise * look.rise * screenHeight)};
}

auto damageText(nx::Damage damage) -> std::string {
    return std::format("{}", std::max(std::lround(damage.value()), 1L));
}

auto DamageNumbers::open(nx::ui::FontCatalog& fonts, const DamageNumberLook& look, std::uint32_t screenHeight)
    -> nx::Result<DamageNumbers, DamageNumbersError> {
    const std::optional<nx::ui::FontChoice> choice = fonts.choose(
        {.family = look.font.family, .weight = look.font.weight, .isItalic = look.font.isItalic});
    if (!choice) {
        return nx::fail(DamageNumbersError::NoFont,
                        std::format("no face of {:?} among the system's fonts", look.font.family));
    }
    const float wholeHeight = look.height * static_cast<float>(screenHeight);
    // Drawn at its largest from glyphs as large, a number is only ever made smaller, which keeps it sharp.
    const float largest = wholeHeight * std::max({look.peakScale, look.endScale, 1.0F});
    auto font =
        fonts.open(*choice, {.cellHeight = std::max(static_cast<std::int32_t>(std::lround(largest)), 1),
                             .smoothing = nx::ui::FontSmoothing::Antialiased});
    if (!font) {
        return nx::fail(DamageNumbersError::Unusable, look.font.family, std::move(font.error()));
    }
    auto atlas = nx::ui::GlyphAtlas::build(*font, kDigits);
    if (!atlas) {
        return nx::fail(DamageNumbersError::Unusable, look.font.family, std::move(atlas.error()));
    }
    return DamageNumbers{std::move(*atlas), look, wholeHeight};
}

DamageNumbers::DamageNumbers(nx::ui::GlyphAtlas atlas, const DamageNumberLook& look,
                             float wholeHeight) noexcept
    : m_atlas{std::move(atlas)}, m_look{&look}, m_wholeHeight{wholeHeight}, m_sides{0, kSideStream} {}

auto DamageNumbers::load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
    -> nx::Result<void, nx::render::ScreenError> {
    auto image =
        screen.loadImage(commands, m_atlas.texels(), m_atlas.width(), m_atlas.height(), "damageNumbers");
    if (!image) {
        return std::unexpected{std::move(image.error())};
    }
    m_image.emplace(std::move(*image));
    return {};
}

void DamageNumbers::show(std::span<const DamageDealt> dealt, nx::Seconds now) {
    for (const DamageDealt& hit : dealt) {
        m_numbers.push_back({.headTop = hit.headTop,
                             .text = damageText(hit.damage),
                             .isHead = hit.isHead,
                             .bornAt = now,
                             .side = m_sides.between(-1.0F, 1.0F)});
    }
    while (m_numbers.size() > m_look->mostNumbers) {
        m_numbers.pop_front();
    }
}

void DamageNumbers::append(std::vector<nx::render::ScreenQuad>& quads, const nx::Mat4& viewProjection,
                           nx::rhi::Extent2d size, nx::Seconds now) {
    const DamageNumberLook& look = *m_look;
    std::erase_if(m_numbers, [&look, now](const Number& number) { return now - number.bornAt >= look.life; });
    if (!m_image) {
        return;
    }
    const auto width = static_cast<float>(size.width);
    const auto height = static_cast<float>(size.height);
    const nx::ui::FontMetrics& metrics = m_atlas.metrics();
    const auto line = static_cast<float>(metrics.height());
    // Its digits stand on its baseline, below its line's middle by as much as its ascent passes half the
    // line.
    const float footBelowMiddle = (static_cast<float>(metrics.ascent) - (line * 0.5F)) * m_wholeHeight / line;
    for (const Number& number : m_numbers) {
        const std::optional<DamageNumberPose> pose = damageNumberPoseAt(look, now - number.bornAt);
        const std::optional<nx::render::ScreenPoint> head =
            nx::render::screenPointOf(viewProjection, number.headTop, width, height);
        if (!pose || !head) {
            continue;
        }
        const nx::render::ScreenPoint middle =
            damageNumberMiddleOf(look, *pose, number.side, *head, height, footBelowMiddle);
        const float centerX = middle.x;
        const float centerY = middle.y;
        // The atlas's glyphs, drawn at the pose's share of the whole size, about the number's middle.
        const float shrink = pose->scale * m_wholeHeight / line;
        const float textWidth = static_cast<float>(nx::ui::widthOf(m_atlas, number.text));
        const std::vector<nx::ui::PlacedGlyph> glyphs = nx::ui::layoutText(m_atlas, number.text, 0.0F, 0.0F);
        const auto quadOf = [&](const nx::ui::PlacedGlyph& glyph, float offsetX, float offsetY,
                                nx::render::ScreenColor color) {
            return nx::render::ScreenQuad{
                .image = &*m_image,
                .source = glyph.source,
                .target = {.x = centerX + ((glyph.x - (textWidth * 0.5F)) * shrink) + offsetX,
                           .y = centerY + ((glyph.y - (line * 0.5F)) * shrink) + offsetY,
                           .width = static_cast<float>(glyph.source.width) * shrink,
                           .height = static_cast<float>(glyph.source.height) * shrink},
                .color = color,
                .filter = nx::render::ScreenFilter::Linear};
        };
        const float outline = look.outline * m_wholeHeight * pose->scale;
        const nx::render::ScreenColor outlineColor = colorOf(look.outlineColor, pose->opacity);
        if (outline > 0.0F) {
            for (const auto& [x, y] : kOutlineWays) {
                for (const nx::ui::PlacedGlyph& glyph : glyphs) {
                    quads.push_back(quadOf(glyph, x * outline, y * outline, outlineColor));
                }
            }
        }
        const nx::render::ScreenColor color =
            colorOf(number.isHead ? look.headColor : look.color, pose->opacity);
        for (const nx::ui::PlacedGlyph& glyph : glyphs) {
            quads.push_back(quadOf(glyph, 0.0F, 0.0F, color));
        }
    }
}

} // namespace cs
