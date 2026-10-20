// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.console;

import std;
import nx.core;

namespace nx::console {

auto editDistance(std::string_view a, std::string_view b) -> std::size_t {
    // Two rows of the Wagner-Fischer table: distances from a prefix of `a` to every prefix of `b`.
    std::vector<std::size_t> previous(b.size() + 1);
    std::vector<std::size_t> current(b.size() + 1);
    std::iota(previous.begin(), previous.end(), std::size_t{0});
    for (std::size_t i = 0; i < a.size(); ++i) {
        current[0] = i + 1;
        for (std::size_t j = 0; j < b.size(); ++j) {
            const std::size_t mismatch =
                toLowerAscii(a[i]) == toLowerAscii(b[j]) ? std::size_t{0} : std::size_t{1};
            const std::size_t substitution = previous[j] + mismatch;
            current[j + 1] = std::min({previous[j + 1] + 1, current[j] + 1, substitution});
        }
        std::swap(previous, current);
    }
    return previous[b.size()];
}

} // namespace nx::console
