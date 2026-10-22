// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// Fonts (nx.ui:font): a font file opened at the heights the schemes ask for, with and without
// antialiasing, every Latin-1 glyph drawn; and the bytes read as a VDMX table.

import std;
import nx.ui;

#include "fuzzTarget.hpp"

void nx::test::fuzz(std::span<const std::byte> input) {
    std::size_t total = nx::ui::readDeviceHeights(input).size();
    const auto file = std::make_shared<const std::vector<std::byte>>(input.begin(), input.end());
    for (const std::int32_t cellHeight : {13, 16, 28}) {
        for (const auto smoothing : {nx::ui::FontSmoothing::None, nx::ui::FontSmoothing::Antialiased}) {
            const auto font = nx::ui::Font::open(file, {.cellHeight = cellHeight, .smoothing = smoothing});
            if (!font) {
                return;
            }
            total += font->family().size() + font->style().size();
            for (char32_t character = 0; character < 256; ++character) {
                if (const auto glyph = font->glyph(character)) {
                    total += glyph->coverage.size();
                }
            }
        }
    }
    std::ignore = total; // the reads are the test
}
