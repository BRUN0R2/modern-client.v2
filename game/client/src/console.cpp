// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.platform;
import nx.assets;
import nx.rhi;
import nx.ui;

namespace cs {

namespace {

/// What the console's window logs under.
constexpr nx::log::Category kLog{"console"};

/// Logs that the console has no window, and why.
template <class Code>
void logNoWindow(const nx::Failure<Code>& failure) {
    nx::log::warning(kLog, "the console has no window: {}", nx::describe(failure));
}

} // namespace

auto consoleCharacters() -> std::u32string {
    std::u32string characters;
    constexpr char32_t kSpace = 0x20;
    constexpr char32_t kTilde = 0x7E;
    constexpr char32_t kNoBreakSpace = 0xA0;
    constexpr char32_t kLastLatin1 = 0xFF;
    for (char32_t character = kSpace; character <= kTilde; ++character) {
        characters += character;
    }
    for (char32_t character = kNoBreakSpace; character <= kLastLatin1; ++character) {
        characters += character;
    }
    // The rest of code page 1252 that the console meets: the euro, quotes, dashes and the ellipsis.
    constexpr auto kCodePageExtras =
        std::to_array<char32_t>({0x20AC, 0x2018, 0x2019, 0x201C, 0x201D, 0x2013, 0x2014, 0x2026});
    for (const char32_t character : kCodePageExtras) {
        characters += character;
    }
    return characters;
}

auto openConsoleFont(const nx::assets::Vfs& vfs, nx::ui::FontCatalog& catalog, std::uint32_t screenHeight)
    -> std::optional<nx::ui::SchemeFontSet> {
    const auto path = nx::assets::VfsPath::parse(kWindowScheme);
    if (!path) {
        logNoWindow(path.error());
        return std::nullopt;
    }
    const auto bytes = vfs.readAll(*path);
    if (!bytes) {
        logNoWindow(bytes.error());
        return std::nullopt;
    }
    const auto text = nx::assets::keyValuesText(*bytes);
    if (!text) {
        logNoWindow(text.error());
        return std::nullopt;
    }
    const auto entries = nx::assets::parseKeyValues(*text);
    if (!entries) {
        logNoWindow(entries.error());
        return std::nullopt;
    }
    const auto scheme = nx::assets::readScheme(*entries);
    if (!scheme) {
        logNoWindow(scheme.error());
        return std::nullopt;
    }
    const nx::assets::SchemeFont* font = scheme->font(kConsoleFont);
    if (font == nullptr) {
        nx::log::warning(kLog, "the console has no window: {} has no font {:?}", kWindowScheme, kConsoleFont);
        return std::nullopt;
    }
    for (const std::filesystem::path& folder : nx::platform::fontFolders()) {
        std::ignore = catalog.addFolder(folder);
    }
    auto opened = nx::ui::SchemeFontSet::open(*font, screenHeight, catalog);
    if (!opened) {
        logNoWindow(opened.error());
        return std::nullopt;
    }
    return std::move(*opened);
}

auto consoleBounds(nx::rhi::Extent2d size) -> nx::ui::PanelBounds {
    constexpr std::int32_t kMargin = 8; // an eighth of the screen on each side
    const auto wide = nx::narrow<std::int32_t>(size.width);
    const auto tall = nx::narrow<std::int32_t>(size.height);
    return {.x = wide / kMargin,
            .y = tall / kMargin,
            .wide = wide - (2 * (wide / kMargin)),
            .tall = tall - (2 * (tall / kMargin))};
}

} // namespace cs
