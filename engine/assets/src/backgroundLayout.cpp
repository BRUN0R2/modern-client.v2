// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

constexpr std::string_view kResolutionWord = "resolution";
/// How a layout sizes a picture.
constexpr std::array<std::string_view, 2> kSizingWords{"scaled", "fit"};

} // namespace

auto parseBackgroundLayout(std::string_view text) -> Result<BackgroundLayout, BackgroundLayoutError> {
    Words words{text};
    const std::optional<std::string_view> head = words.next();
    const std::optional<std::string_view> widthWord = words.next();
    const std::optional<std::string_view> heightWord = words.next();
    const std::optional<std::uint32_t> width = widthWord ? wholeNumberOf(*widthWord) : std::nullopt;
    const std::optional<std::uint32_t> height = heightWord ? wholeNumberOf(*heightWord) : std::nullopt;
    if (!head || !equalsIgnoringCase(*head, kResolutionWord) || !width || !height) {
        return fail(BackgroundLayoutError::NoResolution,
                    std::format("{:?} {:?} {:?}", head.value_or(""), widthWord.value_or(""),
                                heightWord.value_or("")));
    }
    if (*width == 0 || *height == 0 || *width > kMaxBackgroundSide || *height > kMaxBackgroundSide) {
        return fail(BackgroundLayoutError::TooLarge, std::format("a screen of {} by {}", *width, *height));
    }
    BackgroundLayout layout{.width = *width, .height = *height};
    while (const std::optional<std::string_view> image = words.next()) {
        const std::optional<std::string_view> sizing = words.next();
        const std::optional<std::string_view> xWord = words.next();
        const std::optional<std::string_view> yWord = words.next();
        const std::optional<std::uint32_t> x = xWord ? wholeNumberOf(*xWord) : std::nullopt;
        const std::optional<std::uint32_t> y = yWord ? wholeNumberOf(*yWord) : std::nullopt;
        const bool isSized = sizing && std::ranges::any_of(kSizingWords, [&](std::string_view word) {
                                 return equalsIgnoringCase(*sizing, word);
                             });
        if (image->empty() || !isSized || !x || !y) {
            return fail(BackgroundLayoutError::BadTile,
                        std::format("{:?} {:?} {:?} {:?}", *image, sizing.value_or(""), xWord.value_or(""),
                                    yWord.value_or("")));
        }
        if (*x >= *width || *y >= *height) {
            return fail(BackgroundLayoutError::TooLarge,
                        std::format("{:?} at {}, {} of {} by {}", *image, *x, *y, *width, *height));
        }
        layout.tiles.push_back({.image = std::string{*image}, .x = *x, .y = *y});
    }
    return layout;
}

} // namespace nx::assets
