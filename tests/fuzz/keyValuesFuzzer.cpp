// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// VGUI2's KeyValues (nx.assets:keyValues): the text of a file, decoded as its byte order mark says,
// read with and without backslash escapes, every entry visited and looked up, as the toolkit would.

import std;
import nx.assets;

#include "fuzzTarget.hpp"

namespace {

/// Visits `entries` and the blocks within them, looking each key up again by name.
void visit(const std::vector<nx::assets::KeyValuesEntry>& entries, std::size_t& total) {
    for (const nx::assets::KeyValuesEntry& entry : entries) {
        total += entry.key.size() + entry.word.size() + entry.condition.size();
        if (entry.isBlock) {
            for (const nx::assets::KeyValuesEntry& inner : entry.entries) {
                total += entry.find(inner.key) != nullptr ? 1U : 0U;
                total += entry.findWord(inner.key).value_or(std::string_view{}).size();
            }
            visit(entry.entries, total);
        }
    }
}

} // namespace

void nx::test::fuzz(std::span<const std::byte> input) {
    const auto text = nx::assets::keyValuesText(input);
    if (!text) {
        return;
    }
    std::size_t total = 0;
    for (const auto escapes : {nx::assets::KeyValuesEscapes::None, nx::assets::KeyValuesEscapes::Backslash}) {
        if (const auto entries = nx::assets::parseKeyValues(*text, escapes)) {
            visit(*entries, total);
        }
    }
    std::ignore = total; // the reads are the test
}
