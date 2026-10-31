// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.math;
import nx.ecs;
import nx.rhi;
import nx.render;
import nx.console;
import cs.shared;

namespace cs {

namespace {

constexpr float kFullTurn = 2.0F * std::numbers::pi_v<float>;
/// A mark's cell in the picture, in texels, and how the cells lie: in rows of kColumns. Twice as fine as a
/// mark shows on a screen 1080 pixels tall, so that it is only ever made smaller there.
constexpr std::uint32_t kCell = 128;
constexpr std::uint32_t kColumns = 8;
constexpr std::uint32_t kRows = StepIndicator::kStepMarkTurns / kColumns;
constexpr std::size_t kChannels = 4;
constexpr float kOpaque = 255.0F;

/// An ellipse of a bloody print, in a cell's units, the cell one across, y down: its middle and half its
/// width and height.
struct PrintPart {
    float x = 0.0F;
    float y = 0.0F;
    float halfWidth = 0.0F;
    float halfHeight = 0.0F;
};
/// A bare right foot's print about its middle, its toes up: its ball, the outer edge of its arch, its heel,
/// and its toes from the big one out; the left foot's is its mirror.
constexpr std::array<PrintPart, 8> kFoot{
    {{.x = 0.0F, .y = -0.02F, .halfWidth = 0.075F, .halfHeight = 0.09F},
     {.x = 0.035F, .y = 0.085F, .halfWidth = 0.045F, .halfHeight = 0.075F},
     {.x = 0.012F, .y = 0.17F, .halfWidth = 0.056F, .halfHeight = 0.065F},
     {.x = -0.045F, .y = -0.135F, .halfWidth = 0.03F, .halfHeight = 0.034F},
     {.x = -0.005F, .y = -0.15F, .halfWidth = 0.022F, .halfHeight = 0.024F},
     {.x = 0.03F, .y = -0.14F, .halfWidth = 0.019F, .halfHeight = 0.021F},
     {.x = 0.058F, .y = -0.12F, .halfWidth = 0.016F, .halfHeight = 0.018F},
     {.x = 0.08F, .y = -0.09F, .halfWidth = 0.014F, .halfHeight = 0.016F}}};
/// How many of kFoot's parts make a sole, blended into one over kSoleBlend of a cell; the rest are toes.
constexpr std::size_t kSoleParts = 3;
constexpr float kSoleBlend = 0.05F;
/// Where each foot's middle is in a mark: the left a stride behind the right.
constexpr PrintPart kLeftFoot{.x = -0.12F, .y = 0.07F};
constexpr PrintPart kRightFoot{.x = 0.12F, .y = -0.09F};
/// The blood a heel drags behind it, below each heel, fainter than the prints.
constexpr PrintPart kSmear{.y = 0.24F, .halfWidth = 0.035F, .halfHeight = 0.075F};
constexpr float kSmearOpacity = 0.45F;
/// The drops of blood about the prints.
constexpr std::array<PrintPart, 4> kDrops{
    {{.x = 0.02F, .y = 0.29F, .halfWidth = 0.012F, .halfHeight = 0.014F},
     {.x = -0.21F, .y = -0.05F, .halfWidth = 0.01F, .halfHeight = 0.011F},
     {.x = 0.22F, .y = 0.12F, .halfWidth = 0.009F, .halfHeight = 0.01F},
     {.x = -0.03F, .y = -0.3F, .halfWidth = 0.008F, .halfHeight = 0.009F}}};
/// How ragged the prints' edges are, in a cell's units, and how many waves of it a cell holds.
constexpr float kRaggedness = 0.012F;
constexpr float kEdgeWaves = 14.0F;
/// The blood's shade within the prints: from kDarkestBlood to all of the look's color, in waves finer than
/// the edges', and darker toward their rims, where blood dries, over kRim of a cell.
constexpr float kDarkestBlood = 0.55F;
constexpr float kBloodWaves = 26.0F;
constexpr float kRim = 0.02F;
constexpr float kRimDarkening = 0.35F;
/// The thin dark outline about the prints, in a cell's units, and how opaque it is: it shows them on what
/// is bright without framing them.
constexpr float kOutline = 0.012F;
constexpr float kOutlineOpacity = 0.55F;
/// A mark fainter than this is not drawn.
constexpr float kFaintest = 0.002F;

/// The share `part` is of `whole`, from 0 to 1; all of it for a whole of none.
[[nodiscard]] auto shareOf(nx::Seconds part, nx::Seconds whole) noexcept -> float {
    if (whole <= nx::Seconds{}) {
        return 1.0F;
    }
    return std::clamp(part / whole, 0.0F, 1.0F);
}

/// `share`, from 0 to 1, eased in and out (smoothstep).
[[nodiscard]] auto eased(float share) noexcept -> float {
    return share * share * (3.0F - (2.0F * share));
}

/// How far, in a cell's units, (`x`, `y`) is out of `part` placed at `at`, mirrored across when
/// `isMirrored`; less than none within it. Near enough an ellipse's distance for shapes this round, and
/// exact on its edge.
[[nodiscard]] auto outOf(const PrintPart& part, PrintPart at, bool isMirrored, float x, float y) noexcept
    -> float {
    const float middleX = at.x + (isMirrored ? -part.x : part.x);
    const float across = (x - middleX) / part.halfWidth;
    const float along = (y - (at.y + part.y)) / part.halfHeight;
    return (std::hypot(across, along) - 1.0F) * std::min(part.halfWidth, part.halfHeight);
}

/// How far (`x`, `y`) is out of the print of a foot placed at `at`, mirrored when `isMirrored`: its sole's
/// parts blended into one (a polynomial smooth minimum), its toes apart.
[[nodiscard]] auto outOfFoot(PrintPart at, bool isMirrored, float x, float y) noexcept -> float {
    float sole = outOf(kFoot.front(), at, isMirrored, x, y);
    for (const PrintPart& part : std::span{kFoot}.subspan(1, kSoleParts - 1)) {
        const float other = outOf(part, at, isMirrored, x, y);
        const float blended = std::max(kSoleBlend - std::abs(sole - other), 0.0F) / kSoleBlend;
        sole = std::min(sole, other) - (blended * blended * kSoleBlend * 0.25F);
    }
    for (const PrintPart& toe : std::span{kFoot}.subspan(kSoleParts)) {
        sole = std::min(sole, outOf(toe, at, isMirrored, x, y));
    }
    return sole;
}

/// A smooth wave of noise from 0 to 1 at (`x`, `y`), `waves` of it across a cell: values drawn at the
/// corners of a lattice, by a hash of each corner, blended between them.
[[nodiscard]] auto noiseAt(float x, float y, float waves) noexcept -> float {
    const float latticeX = (x + 1.0F) * waves;
    const float latticeY = (y + 1.0F) * waves;
    const float cornerX = std::floor(latticeX);
    const float cornerY = std::floor(latticeY);
    const auto valueAt = [](float column, float row) {
        auto hash = (static_cast<std::uint32_t>(column) * 0x8DA6B343U) ^
                    (static_cast<std::uint32_t>(row) * 0xD8163841U);
        hash ^= hash >> 13U;
        hash *= 0x5BD1E995U;
        hash ^= hash >> 15U;
        return static_cast<float>(hash & 0xFFFFU) / static_cast<float>(0xFFFFU);
    };
    const float acrossShare = eased(latticeX - cornerX);
    const float alongShare = eased(latticeY - cornerY);
    const float top = std::lerp(valueAt(cornerX, cornerY), valueAt(cornerX + 1.0F, cornerY), acrossShare);
    const float bottom =
        std::lerp(valueAt(cornerX, cornerY + 1.0F), valueAt(cornerX + 1.0F, cornerY + 1.0F), acrossShare);
    return std::lerp(top, bottom, alongShare);
}

/// What a texel at (`x`, `y`) of a mark (its toes up) holds: how much the prints and their drops cover
/// it, how much the heels' smears do, the blood's shade there, from 0 to 1, and how much the outline does.
struct PrintTexel {
    float print = 0.0F;
    float smear = 0.0F;
    float shade = 0.0F;
    float outline = 0.0F;
};

[[nodiscard]] auto printAt(float x, float y, float cell) noexcept -> PrintTexel {
    float out = std::min(outOfFoot(kLeftFoot, true, x, y), outOfFoot(kRightFoot, false, x, y));
    for (const PrintPart& drop : kDrops) {
        out = std::min(out, outOf(drop, {}, false, x, y));
    }
    // Blood's edges are ragged.
    out += (noiseAt(x, y, kEdgeWaves) - 0.5F) * 2.0F * kRaggedness;
    const float smeared =
        std::min(outOf(kSmear, kLeftFoot, true, x, y), outOf(kSmear, kRightFoot, false, x, y)) +
        ((noiseAt(y, x, kEdgeWaves) - 0.5F) * 2.0F * kRaggedness);
    const float rim = std::clamp(1.0F + (out / kRim), 0.0F, 1.0F);
    return {.print = std::clamp(0.5F - (out * cell), 0.0F, 1.0F),
            .smear = std::clamp(0.5F - (smeared * cell), 0.0F, 1.0F) * kSmearOpacity,
            .shade =
                std::lerp(kDarkestBlood, 1.0F, noiseAt(x, y, kBloodWaves)) * (1.0F - (kRimDarkening * rim)),
            .outline = std::clamp(0.5F - ((std::min(out, smeared) - kOutline) * cell), 0.0F, 1.0F) *
                       kOutlineOpacity};
}

/// The marks, each turn's turned clockwise by its share of a turn: bloody prints of bare feet, their drops
/// and their heels' smears, in shades of white that the look's color tints, ragged, darker at their rims,
/// thinly outlined in black, as opaque as they cover each texel.
[[nodiscard]] auto drawnMarks() -> std::vector<std::byte> {
    const std::uint32_t width = kCell * kColumns;
    std::vector<std::byte> texels(std::size_t{width} * kCell * kRows * kChannels);
    const auto cell = static_cast<float>(kCell);
    for (std::uint32_t turn = 0; turn < StepIndicator::kStepMarkTurns; ++turn) {
        const float angle =
            kFullTurn * static_cast<float>(turn) / static_cast<float>(StepIndicator::kStepMarkTurns);
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        const std::uint32_t left = (turn % kColumns) * kCell;
        const std::uint32_t top = (turn / kColumns) * kCell;
        for (std::uint32_t row = 0; row < kCell; ++row) {
            for (std::uint32_t column = 0; column < kCell; ++column) {
                // The texel's middle in the cell's units about its middle, turned back to the mark's own.
                const float x = ((static_cast<float>(column) + 0.5F) / cell) - 0.5F;
                const float y = ((static_cast<float>(row) + 0.5F) / cell) - 0.5F;
                const PrintTexel texel = printAt((x * cosine) + (y * sine), (y * cosine) - (x * sine), cell);
                const float blood = std::max(texel.print, texel.smear);
                const float opacity = std::max(blood, texel.outline);
                const float white = opacity > 0.0F ? blood * texel.shade / opacity : 0.0F;
                const std::size_t at = ((std::size_t{top + row} * width) + left + column) * kChannels;
                const auto shade = static_cast<std::byte>(std::lround(white * kOpaque));
                texels[at] = shade;
                texels[at + 1] = shade;
                texels[at + 2] = shade;
                texels[at + 3] = static_cast<std::byte>(std::lround(opacity * kOpaque));
            }
        }
    }
    return texels;
}

} // namespace

auto StepIndicatorCvars::registerIn(nx::console::Console& console)
    -> nx::Result<StepIndicatorCvars, nx::console::CvarError> {
    auto shown = console.registerCvar(nx::console::CvarDesc<bool>{
        .name = "hud_stepindicator",
        .defaultValue = true,
        .description =
            "Whether the steps of the monsters you hear show around the middle of the screen, 1 or 0.",
        .flags = {nx::console::CvarFlag::Archive}});
    if (!shown) {
        return std::unexpected{std::move(shown.error())};
    }
    return StepIndicatorCvars{.shown = std::move(*shown)};
}

auto bearingOf(nx::Vec3 listener, nx::Degrees yaw, nx::Vec3 source) noexcept -> float {
    // The map's yaw turns counterclockwise from +x seen from above; the bearing turns clockwise.
    const float toward = std::atan2(source.y - listener.y, source.x - listener.x);
    const float facing = yaw.value() * std::numbers::pi_v<float> / 180.0F;
    return std::remainder(facing - toward, kFullTurn);
}

auto hearingOf(const StepIndicatorLook& look, nx::HammerUnits distance) noexcept -> float {
    if (distance <= look.nearDistance) {
        return 1.0F;
    }
    const float faded =
        (distance - look.nearDistance).value() / (look.farDistance - look.nearDistance).value();
    return 1.0F - eased(std::clamp(faded, 0.0F, 1.0F));
}

auto stepLevelAt(const StepIndicatorLook& look, nx::Seconds since, float from) noexcept -> float {
    since = std::max(since, nx::Seconds{});
    const float lit = from + ((1.0F - from) * eased(shareOf(since, look.attack)));
    const float released = eased(shareOf(since - look.attack - look.hold, look.release));
    return lit * (1.0F - released);
}

StepIndicator::StepIndicator(const StepIndicatorLook& look) : m_look{&look}, m_picture{drawnMarks()} {}

auto StepIndicator::lit() const noexcept -> nx::Seconds {
    return m_look->attack + m_look->hold + m_look->release;
}

auto StepIndicator::load(nx::render::ScreenRenderer& screen, nx::rhi::CommandList& commands)
    -> nx::Result<void, nx::render::ScreenError> {
    auto image = screen.loadImage(commands, m_picture, kCell * kColumns, kCell * kRows, "stepIndicator");
    if (!image) {
        return std::unexpected{std::move(image.error())};
    }
    m_image.emplace(std::move(*image));
    return {};
}

void StepIndicator::hear(std::span<const EntityStep> steps, nx::Seconds now) {
    const StepIndicatorLook& look = *m_look;
    m_heard.reserve(look.mostHeard);
    for (const EntityStep& step : steps) {
        const auto heard = std::ranges::find(m_heard, step.entity, &HeardBody::entity);
        if (heard != m_heard.end()) {
            heard->from = stepLevelAt(look, now - heard->steppedAt, heard->from);
            heard->steppedAt = now;
            heard->position = step.position;
            continue;
        }
        const HeardBody body{.entity = step.entity, .position = step.position, .steppedAt = now};
        if (m_heard.size() < look.mostHeard) {
            m_heard.push_back(body);
            continue;
        }
        *std::ranges::min_element(m_heard, {}, [&look, now](const HeardBody& each) {
            return stepLevelAt(look, now - each.steppedAt, each.from);
        }) = body;
    }
}

void StepIndicator::append(std::vector<nx::render::ScreenQuad>& quads, nx::Vec3 listener, nx::Degrees yaw,
                           nx::rhi::Extent2d size, nx::Seconds now) {
    const StepIndicatorLook& look = *m_look;
    const nx::Seconds shows = lit();
    std::erase_if(m_heard, [shows, now](const HeardBody& body) { return now - body.steppedAt >= shows; });
    if (!m_image || m_heard.empty()) {
        return;
    }
    // Each body's mark: how loud it shows, and the way it is.
    struct Mark {
        const HeardBody* body = nullptr;
        float loudness = 0.0F;
        float bearing = 0.0F;
    };
    std::vector<Mark> marks;
    marks.reserve(m_heard.size());
    for (const HeardBody& body : m_heard) {
        const float loudness = stepLevelAt(look, now - body.steppedAt, body.from) *
                               hearingOf(look, nx::HammerUnits{nx::length(body.position - listener)});
        if (loudness > kFaintest) {
            marks.push_back(
                {.body = &body, .loudness = loudness, .bearing = bearingOf(listener, yaw, body.position)});
        }
    }
    std::ranges::stable_sort(marks, std::ranges::greater{}, &Mark::loudness);
    const auto height = static_cast<float>(size.height);
    const float middleX = static_cast<float>(size.width) * 0.5F;
    const float middleY = height * 0.5F;
    const float ring = look.ringRadius * height;
    const float wholeSize = look.size * height;
    const float closest = look.separation * wholeSize;
    std::vector<nx::render::ScreenPoint> placed;
    for (const Mark& mark : marks) {
        if (placed.size() == look.mostShown) {
            break;
        }
        const nx::render::ScreenPoint at{.x = middleX + (std::sin(mark.bearing) * ring),
                                         .y = middleY - (std::cos(mark.bearing) * ring)};
        if (std::ranges::any_of(placed, [&at, closest](const nx::render::ScreenPoint& other) {
                return std::hypot(other.x - at.x, other.y - at.y) < closest;
            })) {
            continue;
        }
        placed.push_back(at);
        // Each step pops it larger, back to its size.
        const nx::Seconds since = now - mark.body->steppedAt;
        const float popped =
            since < look.pulseTime ? look.pulseScale * (1.0F - eased(shareOf(since, look.pulseTime))) : 0.0F;
        const float across = wholeSize * (1.0F + popped);
        const float turns = static_cast<float>(kStepMarkTurns);
        const auto turn = static_cast<std::uint32_t>(
            (std::lround(mark.bearing / kFullTurn * turns) % static_cast<long>(kStepMarkTurns) +
             static_cast<long>(kStepMarkTurns)) %
            static_cast<long>(kStepMarkTurns));
        quads.push_back(nx::render::ScreenQuad{.image = &*m_image,
                                               .source = {.x = (turn % kColumns) * kCell,
                                                          .y = (turn / kColumns) * kCell,
                                                          .width = kCell,
                                                          .height = kCell},
                                               .target = {.x = at.x - (across * 0.5F),
                                                          .y = at.y - (across * 0.5F),
                                                          .width = across,
                                                          .height = across},
                                               .color = {.red = look.color.red,
                                                         .green = look.color.green,
                                                         .blue = look.color.blue,
                                                         .alpha = static_cast<std::uint8_t>(std::lround(
                                                             look.opacity * mark.loudness * kOpaque))},
                                               .filter = nx::render::ScreenFilter::Linear});
    }
}

} // namespace cs
