// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the readers of the original's word lists share inside nx.assets (sprite lists, background
/// layouts): the words of a text as the original's parser splits them. Nothing here is exported, and
/// the definitions stay in words.cpp, as nx.assets:binary's do.
export module nx.assets:words;

import std;

namespace nx::assets {

/// The words of a text, as the original's parser splits them: at whitespace, a word that starts
/// with `"` running to the next `"`, and `//` starting a comment that runs to the end of the line.
class Words {
public:
    /// The words of `text`, which must outlive it.
    explicit Words(std::string_view text) noexcept;

    /// The next word; none at the end of the text.
    [[nodiscard]] auto next() noexcept -> std::optional<std::string_view>;

private:
    std::string_view rest;
};

/// `word` as a whole number; none for anything else.
[[nodiscard]] auto wholeNumberOf(std::string_view word) noexcept -> std::optional<std::uint32_t>;

} // namespace nx::assets
