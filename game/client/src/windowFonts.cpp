// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.platform;
import nx.assets;
import nx.ui;

namespace cs {

namespace {

/// What the windows' fonts log under.
constexpr nx::log::Category kLog{"windowFonts"};

/// The scheme `vfs` keeps at kWindowScheme. Errors: SchemeUnreadable.
[[nodiscard]] auto readWindowScheme(const nx::assets::Vfs& vfs)
    -> nx::Result<nx::assets::Scheme, WindowFontError> {
    const auto path = nx::assets::VfsPath::parse(kWindowScheme);
    if (!path) {
        return nx::fail(WindowFontError::SchemeUnreadable, path.error());
    }
    const auto bytes = vfs.readAll(*path);
    if (!bytes) {
        return nx::fail(WindowFontError::SchemeUnreadable, bytes.error());
    }
    const auto text = nx::assets::keyValuesText(*bytes);
    if (!text) {
        return nx::fail(WindowFontError::SchemeUnreadable, std::string{kWindowScheme}, text.error());
    }
    const auto entries = nx::assets::parseKeyValues(*text);
    if (!entries) {
        return nx::fail(WindowFontError::SchemeUnreadable, std::string{kWindowScheme}, entries.error());
    }
    auto scheme = nx::assets::readScheme(*entries);
    if (!scheme) {
        return nx::fail(WindowFontError::SchemeUnreadable, std::string{kWindowScheme}, scheme.error());
    }
    return std::move(*scheme);
}

} // namespace

auto WindowFonts::open(const nx::assets::Vfs& vfs, nx::ui::FontCatalog& catalog)
    -> nx::Result<WindowFonts, WindowFontError> {
    auto scheme = readWindowScheme(vfs);
    if (!scheme) {
        return std::unexpected{std::move(scheme.error())};
    }
    for (const std::filesystem::path& folder : nx::platform::fontFolders()) {
        if (auto added = catalog.addFolder(folder); !added) {
            nx::log::warning(kLog, "the fonts of a folder are left out: {}", nx::describe(added.error()));
        }
    }
    return WindowFonts{std::move(*scheme), catalog};
}

WindowFonts::WindowFonts(nx::assets::Scheme scheme, nx::ui::FontCatalog& catalog) noexcept
    : m_scheme{std::move(scheme)}, m_catalog{&catalog} {}

auto WindowFonts::font(std::string_view name, std::uint32_t screenHeight) const
    -> nx::Result<nx::ui::SchemeFontSet, WindowFontError> {
    const nx::assets::SchemeFont* font = m_scheme.font(name);
    if (font == nullptr) {
        return nx::fail(WindowFontError::NoSuchFont, std::format("{} has no font {:?}", kWindowScheme, name));
    }
    auto opened = nx::ui::SchemeFontSet::open(*font, screenHeight, *m_catalog);
    if (!opened) {
        return nx::fail(WindowFontError::Unusable, std::move(opened.error()));
    }
    return std::move(*opened);
}

} // namespace cs
