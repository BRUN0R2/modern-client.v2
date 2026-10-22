// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper of the interface's tests: a GPU device, if the machine has one, the screen renderer
// with the build's shader, and the square font's atlas on it, for the tests that look at what a
// control draws. Include after <gtest/gtest.h>, `import std;`, `import nx.core;`, `import
// nx.platform;`, `import nx.rhi;`, `import nx.rhi.vulkan;`, `import nx.render;` and `import nx.ui;`.

#include "offscreenTarget.hpp"
#include "squareFont.hpp"

namespace nx::test {

/// The square font at 16 pixels per em (a line of 17 + 3), its atlas of "AB " ('A' and 'B' 10
/// pixels apart, the space 4).
[[nodiscard]] inline auto squareAtlas() -> nx::ui::GlyphAtlas {
    auto font = nx::ui::Font::open(std::make_shared<const std::vector<std::byte>>(SquareFont::build()),
                                   {.cellHeight = 20});
    EXPECT_TRUE(font.has_value());
    auto atlas = nx::ui::GlyphAtlas::build(*font, U"AB ");
    EXPECT_TRUE(atlas.has_value());
    return std::move(atlas).value();
}

/// A device and the screen renderer on it, with the square font's atlas loaded; not ready on a
/// machine without a Vulkan 1.3 device.
class GpuScreen {
public:
    GpuScreen() {
        auto device =
            nx::rhi::vulkan::createDevice({.applicationName = "nxUiTests", .isValidationEnabled = true});
        if (!device) {
            return;
        }
        m_device = std::move(*device);
        const auto executable = nx::platform::executablePath();
        const auto shader = executable
                                ? nx::platform::readFile(executable->parent_path() / "shaders" / "screen.spv")
                                : nx::platform::readFile({});
        EXPECT_TRUE(shader.has_value());
        auto renderer = nx::render::ScreenRenderer::create(
            *m_device, {.shaderCode = shader.value_or(std::vector<std::byte>{}),
                        .colorFormat = nx::rhi::Format::Rgba8Unorm});
        EXPECT_TRUE(renderer.has_value()) << (renderer ? "" : nx::describe(renderer.error()));
        if (!renderer) {
            return;
        }
        m_renderer.emplace(std::move(*renderer));
        auto image = inFrameOfItsOwn(*m_device, [&](nx::rhi::CommandList& commands) {
            return m_renderer->loadImage(commands, m_atlas.texels(), m_atlas.width(), m_atlas.height(),
                                         "square");
        });
        EXPECT_TRUE(image.has_value()) << (image ? "" : nx::describe(image.error()));
        if (image) {
            m_image.emplace(std::move(*image));
        }
    }

    [[nodiscard]] auto isReady() const noexcept -> bool { return m_image.has_value(); }
    /// The square font's atlas, ready to draw.
    [[nodiscard]] auto face() const noexcept -> nx::ui::TextFace {
        return {.atlas = &m_atlas, .image = &*m_image};
    }
    [[nodiscard]] auto image() const noexcept -> const nx::render::ScreenImage* { return &*m_image; }

private:
    nx::ui::GlyphAtlas m_atlas = squareAtlas();
    std::unique_ptr<nx::rhi::Device> m_device;
    std::optional<nx::render::ScreenRenderer> m_renderer;
    std::optional<nx::render::ScreenImage> m_image;
};

} // namespace nx::test
