// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// TGA images (nx.assets:tga). An image that reads has the pixels of its size, the size an upload
// trusts, and every one is read.

import std;
import nx.core;
import nx.assets;

#include "fuzzTarget.hpp"

void nx::test::fuzz(std::span<const std::byte> input) {
    const auto image = nx::assets::readTga(input);
    if (!image) {
        return;
    }
    nx::expects(image->pixels.size() == std::size_t{image->width} * image->height * 4,
                "readTga: four bytes for each pixel of the image");
    int sum = 0;
    for (const std::byte channel : image->pixels) {
        sum += std::to_integer<int>(channel);
    }
    std::ignore = sum; // the reads are the test
}
