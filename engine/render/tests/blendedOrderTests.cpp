// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// The order a frame draws in: the blended things of every renderer in one list, as the original's
// R_TransEntityCompare sorts its translucent entities, recorded by renderers that log what they are
// asked to draw, in a render pass on whatever Vulkan device the machine has (lavapipe in CI).

#include <gtest/gtest.h>

import std;
import nx.core;
import nx.rhi;
import nx.rhi.vulkan;
import nx.render;

#include "offscreenTarget.hpp"

namespace {

using nx::render::BlendedDraw;
using nx::render::BlendedTurn;
using nx::render::RenderMode;

static_assert(nx::render::OrderedRenderer<nx::render::BspWorld>);
static_assert(nx::render::OrderedRenderer<nx::render::StudioRenderer>);
static_assert(nx::render::OrderedRenderer<nx::render::SpriteRenderer>);

/// A renderer that logs what it is asked to draw, as its name and "solid" or the index of a blended
/// thing.
class LoggingRenderer {
public:
    LoggingRenderer(std::string name, std::vector<BlendedDraw> blended, std::vector<std::string>& log)
        : m_name{std::move(name)}, m_blended{std::move(blended)}, m_log{&log} {}

    [[nodiscard]] auto blendedDraws() const noexcept -> std::span<const BlendedDraw> { return m_blended; }
    void drawSolid(nx::rhi::CommandList& /*commands*/) const { m_log->push_back(m_name + " solid"); }
    void drawBlended(nx::rhi::CommandList& /*commands*/, std::uint32_t index) const {
        m_log->push_back(std::format("{} {}", m_name, index));
    }

private:
    std::string m_name;
    std::vector<BlendedDraw> m_blended;
    std::vector<std::string>* m_log;
};

static_assert(nx::render::OrderedRenderer<LoggingRenderer>);

TEST(BlendedOrderTest, TheFartherIsDrawnFirst) {
    const BlendedDraw near{.distance = 100.0F, .mode = RenderMode::Glow};
    const BlendedDraw far{.distance = 400.0F, .mode = RenderMode::TransColor};
    EXPECT_TRUE(nx::render::isDrawnBefore(far, near));
    EXPECT_FALSE(nx::render::isDrawnBefore(near, far));
    // A brush entity drawn TransAlpha, before every other.
    const BlendedDraw solid{.distance = nx::render::kFarthest, .mode = RenderMode::TransAlpha};
    EXPECT_TRUE(nx::render::isDrawnBefore(solid, far));
}

TEST(BlendedOrderTest, AtOneDistanceTheModesAreDrawnAsTheOriginalRanksThem) {
    // The others first, then TransTexture, TransAdd and Glow (R_RankForRenderMode).
    const std::array ranked{RenderMode::TransColor, RenderMode::TransTexture, RenderMode::TransAdd,
                            RenderMode::Glow};
    for (std::size_t first = 0; first < ranked.size(); ++first) {
        for (std::size_t second = 0; second < ranked.size(); ++second) {
            EXPECT_EQ(nx::render::isDrawnBefore({.distance = 1.0F, .mode = ranked.at(first)},
                                                {.distance = 1.0F, .mode = ranked.at(second)}),
                      first < second)
                << "modes " << first << " and " << second;
        }
    }
    EXPECT_FALSE(nx::render::isDrawnBefore({.distance = 1.0F, .mode = RenderMode::TransAlpha},
                                           {.distance = 1.0F, .mode = RenderMode::TransColor}));
}

TEST(BlendedOrderTest, TheBlendedThingsOfEveryRendererTakeTheirTurnsInOneOrder) {
    const std::array first{BlendedDraw{.distance = 100.0F, .mode = RenderMode::TransTexture, .index = 0},
                           BlendedDraw{.distance = 400.0F, .mode = RenderMode::TransAdd, .index = 1},
                           BlendedDraw{.distance = 100.0F, .mode = RenderMode::TransTexture, .index = 2}};
    const std::array second{BlendedDraw{.distance = 100.0F, .mode = RenderMode::TransTexture, .index = 0},
                            BlendedDraw{.distance = 900.0F, .mode = RenderMode::Glow, .index = 1}};
    const std::array<std::span<const BlendedDraw>, 2> renderers{first, second};
    // At one distance and of one mode, in the order of their renderers, then in their own.
    const std::vector<BlendedTurn> expected{{.renderer = 1, .index = 1},
                                            {.renderer = 0, .index = 1},
                                            {.renderer = 0, .index = 0},
                                            {.renderer = 0, .index = 2},
                                            {.renderer = 1, .index = 0}};
    EXPECT_EQ(nx::render::blendedTurnsOf(renderers), expected);
    EXPECT_TRUE(nx::render::blendedTurnsOf({}).empty());
}

TEST(BlendedOrderTest, TheSolidThingsOfEachRendererAreDrawnFirstThenTheBlendedOnesOfAllInOneOrder) {
    auto device =
        nx::rhi::vulkan::createDevice({.applicationName = "nxRenderTests", .isValidationEnabled = true});
    if (!device) {
        GTEST_SKIP() << "no Vulkan 1.3 device on this machine";
    }
    std::vector<std::string> log;
    const LoggingRenderer world{
        "world",
        {{.distance = 100.0F, .mode = RenderMode::TransTexture, .index = 0},
         {.distance = nx::render::kFarthest, .mode = RenderMode::TransAlpha, .index = 1}},
        log};
    const LoggingRenderer models{"models",
                                 {{.distance = 400.0F, .mode = RenderMode::TransAdd, .index = 0},
                                  {.distance = 100.0F, .mode = RenderMode::TransAdd, .index = 1}},
                                 log};
    const LoggingRenderer sprites{"sprites",
                                  {{.distance = 100.0F, .mode = RenderMode::Glow, .index = 0},
                                   {.distance = 100.0F, .mode = RenderMode::TransTexture, .index = 1}},
                                  log};
    nx::test::OffscreenTarget target{**device};
    std::ignore = target.draw(
        [&](nx::rhi::CommandList& list) { nx::render::drawInOrder(list, world, models, sprites); });

    // At one distance and of one mode, in the order of their renderers.
    const std::vector<std::string> expected{"world solid", "models solid", "sprites solid",
                                            "world 1",     "models 0",     "world 0",
                                            "sprites 1",   "models 1",     "sprites 0"};
    EXPECT_EQ(log, expected);
    EXPECT_EQ((*device)->validationErrors(), 0U);
}

} // namespace
