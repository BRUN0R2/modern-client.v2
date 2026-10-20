// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// WAD3 files and their textures (nx.assets:wad).

import std;
import nx.assets;

#include "fuzzTarget.hpp"

void nx::test::fuzz(std::span<const std::byte> input) {
    const auto wad = nx::assets::WadView::parse(input);
    if (!wad) {
        return;
    }
    for (const nx::assets::WadEntry& entry : wad->entries()) {
        std::ignore = nx::assets::readTexture(entry); // a failure is a valid outcome; only a crash is not
        std::ignore = wad->find(entry.name);
    }
}
