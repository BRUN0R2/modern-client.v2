// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// KeyValues (ADR-0020): the text VGUI2 keeps its schemes (resource/TrackerScheme.res,
/// resource/ClientScheme.res), its layouts (resource/*.res, classes/*.res) and its localized strings
/// (resource/*_english.txt) in. Each entry is a key and, as its value, a word or a block of entries
/// in braces. Read from the files themselves and the public descriptions of the format.
export module nx.assets:keyValues;

import std;
import nx.core;

export namespace nx::assets {

/// What can be wrong with a KeyValues text; the detail has the line.
enum class KeyValuesError : std::uint8_t {
    BadEncoding,   ///< UTF-16 of an odd number of bytes, or with half of a surrogate pair.
    UnclosedQuote, ///< A quoted word runs to the end of the text.
    UnclosedBlock, ///< A block is still open at the end of the text.
    StrayBrace,    ///< A `}` that closes no block, or a `{` where a key belongs.
    MissingValue,  ///< A key with nothing after it but a `}` or the end of the text.
    TooDeep,       ///< Blocks within blocks deeper than kKeyValuesMaxDepth.
};

/// How deep blocks may lie within blocks: far beyond VGUI2's files, a few levels deep.
inline constexpr std::size_t kKeyValuesMaxDepth = 64;

/// How the quoted words of a KeyValues text spell what they hold.
enum class KeyValuesEscapes : std::uint8_t {
    None,      ///< As they are: the schemes, the layouts and the settings.
    Backslash, ///< `\n`, `\t`, `\\` and `\"` for a line break, a tab, a backslash and a quote: the
               ///< localized strings.
};

/// An entry of a KeyValues text: a key, and as its value a word or a block of entries.
struct KeyValuesEntry {
    std::string key{};
    std::string word{};                    ///< Its value, when it is a word.
    std::vector<KeyValuesEntry> entries{}; ///< Its value, when it is a block: the entries in their order.
    std::string condition{};               ///< The `[$WIN32]` that follows it; empty without one.
    bool isBlock = false;

    /// The first entry of this block whose key is `name`, letters in any case, as VGUI2 looks keys
    /// up; null when there is none.
    [[nodiscard]] auto find(std::string_view name) const noexcept -> const KeyValuesEntry*;
    /// The word of the first entry of this block whose key is `name`; none when there is no such
    /// entry, or when it is a block.
    [[nodiscard]] auto findWord(std::string_view name) const noexcept -> std::optional<std::string_view>;
};

/// The entries of the KeyValues text `text`, as VGUI2 reads it: words, quoted (and then running
/// across lines up to the next `"`) or bare (up to a space, a brace or a quote); `{` and `}`; and
/// `//` before a word starting a comment to the end of its line. A key is followed by a word, its
/// value, or by a block; a condition in brackets may follow either. With Backslash escapes, quoted
/// words spell \n, \t, \\ and \" as VGUI2's localized strings do; another backslash is kept.
/// Errors: UnclosedQuote, UnclosedBlock, StrayBrace, MissingValue, TooDeep.
[[nodiscard]] auto parseKeyValues(std::string_view text, KeyValuesEscapes escapes = KeyValuesEscapes::None)
    -> Result<std::vector<KeyValuesEntry>, KeyValuesError>;

/// The text of the KeyValues file `bytes` in UTF-8: decoded from UTF-16 little-endian when it starts
/// with that byte order mark, as the localized strings do; else without a UTF-8 byte order mark, the
/// rest as it is. Errors: BadEncoding.
[[nodiscard]] auto keyValuesText(std::span<const std::byte> bytes) -> Result<std::string, KeyValuesError>;

} // namespace nx::assets
