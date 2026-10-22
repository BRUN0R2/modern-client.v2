// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// VGUI2's localized strings (ADR-0020): the text of each token a layout names with a `#`
/// ("#GameUI_Console"), in one language, from the KeyValues of resource/*_<language>.txt, read
/// with backslash escapes: `"lang" { "Language" "English" "Tokens" { ... } }`.
export module nx.assets:localization;

import std;
import nx.core;
import :keyValues;

export namespace nx::assets {

/// What a localization file lacks; the detail says what.
enum class LocalizationError : std::uint8_t {
    NotLocalization, ///< No `lang` block with a `Tokens` block in it.
};

/// The strings of one language: the text of each token.
class LocalizedStrings {
public:
    /// The strings of `entries`, the KeyValues of a localization file: its `lang` block's `Language`
    /// and the words of its `Tokens`; the first of a token written twice is kept.
    /// Errors: NotLocalization.
    [[nodiscard]] static auto read(std::span<const KeyValuesEntry> entries)
        -> Result<LocalizedStrings, LocalizationError>;

    /// The language the file says it is in ("English"); empty when it does not say.
    [[nodiscard]] inline auto language() const noexcept -> std::string_view { return m_language; }
    /// How many tokens it has.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return m_texts.size(); }

    /// The text of `token`, with or without the `#` a layout writes before it, letters in any case;
    /// none when the language has no such token.
    [[nodiscard]] auto find(std::string_view token) const -> std::optional<std::string_view>;

private:
    std::string m_language;
    std::map<std::string, std::string, std::less<>> m_texts; ///< By token, its letters lowered.
};

} // namespace nx::assets
