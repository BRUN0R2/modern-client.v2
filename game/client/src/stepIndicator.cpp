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
/// A mark's cell in the picture, in texels, and how the cells lie: in rows of kColumns.
constexpr std::uint32_t kCell = 64;
constexpr std::uint32_t kColumns = 8;
constexpr std::uint32_t kRows = StepIndicator::kStepMarkTurns / kColumns;
constexpr std::size_t kChannels = 4;
constexpr float kOpaque = 255.0F;

/// An ellipse of a footprint, in a cell's units, the cell one across, y down: its middle and half its
/// width and height.
struct FootPart {
    float x = 0.0F;
    float y = 0.0F;
    float halfWidth = 0.0F;
    float halfHeight = 0.0F;
};
/// The footprints of a mark, its toes up: each foot's ball and heel, the left a stride behind the right.
constexpr std::array<FootPart, 4> kFootprints{
    {{.x = -0.11F, .y = -0.02F, .halfWidth = 0.085F, .halfHeight = 0.13F},
     {.x = -0.11F, .y = 0.2F, .halfWidth = 0.065F, .halfHeight = 0.075F},
     {.x = 0.11F, .y = -0.16F, .halfWidth = 0.085F, .halfHeight = 0.13F},
     {.x = 0.11F, .y = 0.06F, .halfWidth = 0.065F, .halfHeight = 0.075F}}};
/// The dark outline about the footprints, in a cell's units, and how opaque it is: it shows them on what
/// is bright.
constexpr float kOutline = 0.035F;
constexpr float kOutlineOpacity = 0.8F;
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

/// How far, in a cell's units, a point of a mark (its toes up) is out of the footprints, less than none
/// within them.
[[nodiscard]] auto outOfFootprints(float x, float y) noexcept -> float {
    float nearest = std::numeric_limits<float>::max();
    for (const FootPart& part : kFootprints) {
        const float across = (x - part.x) / part.halfWidth;
        const float along = (y - part.y) / part.halfHeight;
        // Near enough an ellipse's distance for a shape this round, and exact on its edge.
        const float out = (std::hypot(across, along) - 1.0F) * std::min(part.halfWidth, part.halfHeight);
        nearest = std::min(nearest, out);
    }
    return nearest;
}

/// The marks, each turn's turned clockwise by its share of a turn: white footprints outlined in black,
/// as opaque as they cover each texel.
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
                const float out = outOfFootprints((x * cosine) + (y * sine), (y * cosine) - (x * sine));
                const float fill = std::clamp(0.5F - (out * cell), 0.0F, 1.0F);
                const float outline =
                    std::clamp(0.5F - ((out - kOutline) * cell), 0.0F, 1.0F) * kOutlineOpacity;
                const float opacity = std::max(fill, outline);
                const float white = opacity > 0.0F ? fill / opacity : 0.0F;
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
