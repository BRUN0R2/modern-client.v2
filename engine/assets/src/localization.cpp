// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

/// `token` without the `#` a layout writes before it, its letters lowered: the key of a text.
[[nodiscard]] auto keyOf(std::string_view token) -> std::string {
    if (token.starts_with('#')) {
        token.remove_prefix(1);
    }
    return token | std::views::transform(toLowerAscii) | std::ranges::to<std::string>();
}

} // namespace

auto LocalizedStrings::find(std::string_view token) const -> std::optional<std::string_view> {
    const auto found = m_texts.find(keyOf(token));
    return found == m_texts.end() ? std::nullopt : std::optional<std::string_view>{found->second};
}

auto LocalizedStrings::read(std::span<const KeyValuesEntry> entries)
    -> Result<LocalizedStrings, LocalizationError> {
    const auto lang = std::ranges::find_if(entries, [](const KeyValuesEntry& entry) {
        return entry.isBlock && equalsIgnoringCase(entry.key, "lang");
    });
    const KeyValuesEntry* tokens = lang == entries.end() ? nullptr : lang->find("Tokens");
    if (tokens == nullptr || !tokens->isBlock) {
        return fail(LocalizationError::NotLocalization, "no lang block with Tokens in it");
    }
    LocalizedStrings strings;
    strings.m_language = lang->findWord("Language").value_or(std::string_view{});
    for (const KeyValuesEntry& entry : tokens->entries) {
        if (!entry.isBlock) {
            strings.m_texts.try_emplace(keyOf(entry.key), entry.word);
        }
    }
    return strings;
}

} // namespace nx::assets
