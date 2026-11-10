// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

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
