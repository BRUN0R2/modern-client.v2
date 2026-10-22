// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

/// The words of a text, as the original's parser splits them: at whitespace, a word that starts
/// with `"` running to the next `"`, and `//` starting a comment that runs to the end of the line.
class Words {
public:
    explicit Words(std::string_view text) noexcept : m_rest{text} {}

    /// The next word; none at the end of the text.
    [[nodiscard]] auto next() noexcept -> std::optional<std::string_view> {
        for (;;) {
            const auto start = std::ranges::find_if_not(m_rest, isSpace);
            m_rest = m_rest.substr(narrow<std::size_t>(start - m_rest.begin()));
            if (!m_rest.starts_with("//")) {
                break;
            }
            const std::size_t lineEnd = m_rest.find('\n');
            m_rest = lineEnd == std::string_view::npos ? std::string_view{} : m_rest.substr(lineEnd);
        }
        if (m_rest.empty()) {
            return std::nullopt;
        }
        if (m_rest.front() == '"') {
            const std::size_t close = m_rest.find('"', 1);
            const std::string_view word =
                m_rest.substr(1, close == std::string_view::npos ? close : close - 1);
            m_rest = close == std::string_view::npos ? std::string_view{} : m_rest.substr(close + 1);
            return word;
        }
        const auto end = std::ranges::find_if(m_rest, isSpace);
        const auto length = narrow<std::size_t>(end - m_rest.begin());
        const std::string_view word = m_rest.substr(0, length);
        m_rest = m_rest.substr(length);
        return word;
    }

private:
    [[nodiscard]] static constexpr auto isSpace(char character) noexcept -> bool {
        return static_cast<unsigned char>(character) <= static_cast<unsigned char>(' ');
    }

    std::string_view m_rest;
};

/// `word` as a whole number; none for anything else.
[[nodiscard]] auto wholeNumberOf(std::string_view word) noexcept -> std::optional<std::uint32_t> {
    std::uint32_t value = 0;
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value);
    return error == std::errc{} && end == word.data() + word.size() ? std::optional{value} : std::nullopt;
}

} // namespace

auto parseSpriteList(std::string_view text) -> Result<std::vector<SpriteListEntry>, SpriteListError> {
    Words words{text};
    const std::optional<std::string_view> countWord = words.next();
    const std::optional<std::uint32_t> count = countWord ? wholeNumberOf(*countWord) : std::nullopt;
    if (!count) {
        return fail(SpriteListError::NoCount, std::format("{:?}", countWord.value_or("")));
    }
    constexpr std::size_t kWordsPerEntry = 7;
    std::vector<SpriteListEntry> entries;
    for (std::uint32_t index = 0; index < *count; ++index) {
        std::array<std::string_view, kWordsPerEntry> fields{};
        std::size_t read = 0;
        for (; read < fields.size(); ++read) {
            const std::optional<std::string_view> word = words.next();
            if (!word) {
                break;
            }
            fields.at(read) = *word;
        }
        if (read == 0) {
            break; // the list ends before its count: the entries it has
        }
        const std::optional<std::uint32_t> resolution = wholeNumberOf(fields[1]);
        const std::optional<std::uint32_t> x = wholeNumberOf(fields[3]);
        const std::optional<std::uint32_t> y = wholeNumberOf(fields[4]);
        const std::optional<std::uint32_t> width = wholeNumberOf(fields[5]);
        const std::optional<std::uint32_t> height = wholeNumberOf(fields[6]);
        if (read < fields.size() || !resolution || !x || !y || !width || !height) {
            return fail(SpriteListError::BadEntry,
                        std::format("entry {} {:?}: {}", index, fields[0],
                                    fields | std::views::take(read) | std::views::join_with(' ') |
                                        std::ranges::to<std::string>()));
        }
        entries.push_back({.name = std::string{fields[0]},
                           .resolution = *resolution,
                           .sprite = std::string{fields[2]},
                           .rect = {.x = *x, .y = *y, .width = *width, .height = *height}});
    }
    return entries;
}

} // namespace nx::assets
