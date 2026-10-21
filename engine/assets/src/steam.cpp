// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;
import nx.platform;

namespace nx::assets {

namespace {

constexpr log::Category kLog{"assets"};

/// One entry of a KeyValues file: a key with a text, or a key with a block of entries.
struct KeyValue {
    std::string key;
    std::optional<std::string> text; // nullopt for a block
    std::vector<KeyValue> entries;   // a block's entries
};

/// Reads Valve KeyValues text: quoted or bare words, `{` `}` blocks and `//` comments, with the
/// escapes `\\`, `\"`, `\n` and `\t` in quoted words.
class KeyValuesReader {
public:
    KeyValuesReader(std::string_view text, std::string_view sourceName) noexcept
        : m_text{text}, m_sourceName{sourceName} {}

    /// Every top-level entry. Errors: ConfigInvalid.
    [[nodiscard]] auto readFile() -> Result<std::vector<KeyValue>, ContentError> { return readEntries(0); }

private:
    enum class TokenKind : std::uint8_t {
        Word,
        Open,
        Close,
        End,
    };

    struct Token {
        TokenKind kind = TokenKind::End;
        std::string word;
    };

    [[nodiscard]] auto readEntries(std::size_t depth) -> Result<std::vector<KeyValue>, ContentError> {
        std::vector<KeyValue> entries;
        for (;;) {
            auto key = nextToken();
            if (!key) {
                return std::unexpected(std::move(key.error()));
            }
            if (key->kind == TokenKind::End) {
                if (depth > 0) {
                    return failure("a block is not closed with '}'");
                }
                return entries;
            }
            if (key->kind == TokenKind::Close) {
                if (depth == 0) {
                    return failure("'}' closes no block");
                }
                return entries;
            }
            if (key->kind == TokenKind::Open) {
                return failure("a block has no key");
            }
            auto value = nextToken();
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            if (value->kind == TokenKind::Word) {
                entries.push_back(
                    KeyValue{.key = std::move(key->word), .text = std::move(value->word), .entries = {}});
                continue;
            }
            if (value->kind != TokenKind::Open) {
                return failure(std::format("the key {:?} has no value", key->word));
            }
            if (depth == kMaxKeyValuesDepth) {
                return failure("blocks nest deeper than kMaxKeyValuesDepth");
            }
            auto block = readEntries(depth + 1);
            if (!block) {
                return std::unexpected(std::move(block.error()));
            }
            entries.push_back(
                KeyValue{.key = std::move(key->word), .text = std::nullopt, .entries = std::move(*block)});
        }
    }

    [[nodiscard]] auto nextToken() -> Result<Token, ContentError> {
        skipSpaceAndComments();
        if (m_offset == m_text.size()) {
            return Token{.kind = TokenKind::End, .word = {}};
        }
        switch (m_text[m_offset]) {
        case '{':
            ++m_offset;
            return Token{.kind = TokenKind::Open, .word = {}};
        case '}':
            ++m_offset;
            return Token{.kind = TokenKind::Close, .word = {}};
        case '"':
            return readQuoted().transform(
                [](std::string word) { return Token{.kind = TokenKind::Word, .word = std::move(word)}; });
        default:
            return Token{.kind = TokenKind::Word, .word = readBare()};
        }
    }

    void skipSpaceAndComments() noexcept {
        while (m_offset < m_text.size()) {
            if (m_text.substr(m_offset).starts_with("//")) {
                const std::size_t lineEnd = m_text.find('\n', m_offset);
                m_offset = lineEnd == std::string_view::npos ? m_text.size() : lineEnd;
            } else if (isSpace(m_text[m_offset])) {
                m_line += m_text[m_offset] == '\n' ? 1U : 0U;
                ++m_offset;
            } else {
                return;
            }
        }
    }

    /// A quoted word; m_offset is at its opening quote.
    [[nodiscard]] auto readQuoted() -> Result<std::string, ContentError> {
        const std::uint32_t firstLine = m_line;
        std::string word;
        for (++m_offset; m_offset < m_text.size(); ++m_offset) {
            const char character = m_text[m_offset];
            if (character == '"') {
                ++m_offset;
                return word;
            }
            if (character == '\\' && m_offset + 1 < m_text.size()) {
                ++m_offset;
                word += unescaped(m_text[m_offset]);
                continue;
            }
            m_line += character == '\n' ? 1U : 0U;
            word += character;
        }
        m_line = firstLine;
        return failure("a quoted word is not closed");
    }

    /// A word up to a space, a brace or a quote; m_offset is at its first character.
    [[nodiscard]] auto readBare() -> std::string {
        const std::size_t start = m_offset;
        while (m_offset < m_text.size() && !isSpace(m_text[m_offset]) && m_text[m_offset] != '{' &&
               m_text[m_offset] != '}' && m_text[m_offset] != '"') {
            ++m_offset;
        }
        return std::string{m_text.substr(start, m_offset - start)};
    }

    [[nodiscard]] static auto isSpace(char character) noexcept -> bool {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    }

    /// The character an escape stands for: `\n` and `\t` for a line break and a tab, and any other
    /// character (`\\`, `\"`) for itself.
    [[nodiscard]] static auto unescaped(char character) noexcept -> char {
        switch (character) {
        case 'n':
            return '\n';
        case 't':
            return '\t';
        default:
            return character;
        }
    }

    [[nodiscard]] auto failure(std::string_view problem) const -> std::unexpected<Failure<ContentError>> {
        return fail(ContentError::ConfigInvalid, std::format("{}:{}: {}", m_sourceName, m_line, problem));
    }

    std::string_view m_text;
    std::string_view m_sourceName;
    std::size_t m_offset = 0;
    std::uint32_t m_line = 1;
};

/// Whether `key` numbers a library: the keys of the libraries are "0", "1", ...; the old layout
/// mixes them with others ("TimeNextStatsReport").
[[nodiscard]] auto isLibraryKey(std::string_view key) noexcept -> bool {
    return !key.empty() &&
           std::ranges::all_of(key, [](char character) { return character >= '0' && character <= '9'; });
}

/// Every library of the Steam folder `steamFolder`: itself, then those its libraryfolders.vdf
/// lists. Errors: ConfigInvalid.
[[nodiscard]] auto librariesOf(const std::filesystem::path& steamFolder)
    -> Result<std::vector<std::filesystem::path>, ContentError> {
    std::vector<std::filesystem::path> libraries{steamFolder};
    const std::filesystem::path listFile = steamFolder / "steamapps" / "libraryfolders.vdf";
    auto text = platform::readTextFile(listFile);
    if (!text) {
        if (text.error().code == platform::FileError::NotFound) {
            return libraries; // a Steam folder without other libraries
        }
        return fail(ContentError::ConfigInvalid, platform::toUtf8(listFile), std::move(text.error()));
    }
    auto listed = readSteamLibraries(*text, platform::toUtf8(listFile));
    if (!listed) {
        return std::unexpected(std::move(listed.error()));
    }
    std::ranges::move(*listed, std::back_inserter(libraries));
    return libraries;
}

/// Every library of `steamFolders`, in order, each once: the current layout lists the Steam
/// folder itself among its libraries.
[[nodiscard]] auto librariesOf(std::span<const std::filesystem::path> steamFolders)
    -> Result<std::vector<std::filesystem::path>, ContentError> {
    std::vector<std::filesystem::path> libraries;
    for (const std::filesystem::path& steamFolder : steamFolders) {
        auto listed = librariesOf(steamFolder);
        if (!listed) {
            return std::unexpected(std::move(listed.error()));
        }
        for (std::filesystem::path& library : *listed) {
            const bool isKnown =
                std::ranges::any_of(libraries, [&library](const std::filesystem::path& known) {
                    std::error_code ignored; // libraries that cannot be compared are different libraries
                    return std::filesystem::equivalent(known, library, ignored);
                });
            if (!isKnown) {
                libraries.push_back(std::move(library));
            }
        }
    }
    return libraries;
}

} // namespace

auto readSteamLibraries(std::string_view text, std::string_view sourceName)
    -> Result<std::vector<std::filesystem::path>, ContentError> {
    auto entries = KeyValuesReader{text, sourceName}.readFile();
    if (!entries) {
        return std::unexpected(std::move(entries.error()));
    }
    const auto list = std::ranges::find_if(*entries, [](const KeyValue& entry) {
        return !entry.text && equalsIgnoringCase(entry.key, "libraryfolders");
    });
    if (list == entries->end()) {
        return fail(ContentError::ConfigInvalid, std::format("{}: no libraryfolders block", sourceName));
    }
    std::vector<std::filesystem::path> libraries;
    for (const KeyValue& library : list->entries) {
        if (!isLibraryKey(library.key)) {
            continue;
        }
        if (library.text) {
            libraries.push_back(platform::fromUtf8(*library.text));
            continue;
        }
        const auto path = std::ranges::find_if(library.entries, [](const KeyValue& field) {
            return field.text && equalsIgnoringCase(field.key, "path");
        });
        if (path == library.entries.end()) {
            return fail(ContentError::ConfigInvalid,
                        std::format("{}: library {:?} has no path", sourceName, library.key));
        }
        libraries.push_back(platform::fromUtf8(*path->text));
    }
    return libraries;
}

auto detectGoldsrcInstall(const GoldsrcInstallDesc& desc, std::span<const std::filesystem::path> steamFolders)
    -> Result<GoldsrcInstall, ContentError> {
    if (!isDirectoryName(desc.steamInstallFolder)) {
        return fail(
            ContentError::ConfigInvalid,
            std::format("steamInstallFolder {:?} is not one directory name", desc.steamInstallFolder));
    }
    auto libraries = librariesOf(steamFolders);
    if (!libraries) {
        return std::unexpected(std::move(libraries.error()));
    }
    const std::string manifest = std::format("appmanifest_{}.acf", desc.steamAppId);
    std::optional<Failure<ContentError>> firstFailure;
    for (const std::filesystem::path& library : *libraries) {
        const std::filesystem::path manifestPath = library / "steamapps" / manifest;
        std::error_code error;
        const std::filesystem::file_status status = std::filesystem::status(manifestPath, error);
        if (status.type() == std::filesystem::file_type::not_found) {
            continue;
        }
        if (error) {
            log::warning(kLog, "the Steam library {} cannot be read: {}", platform::toUtf8(library),
                         error.message());
            continue;
        }
        if (!std::filesystem::is_regular_file(status)) {
            continue;
        }
        auto install = GoldsrcInstall::open(desc, library / "steamapps" / "common" /
                                                      platform::fromUtf8(desc.steamInstallFolder));
        if (install) {
            return install;
        }
        log::warning(kLog, "an installation of Steam app {} fails validation: {}", desc.steamAppId,
                     describe(install.error()));
        if (!firstFailure) {
            firstFailure = std::move(install.error());
        }
    }
    if (firstFailure) {
        return std::unexpected(std::move(*firstFailure));
    }
    return fail(ContentError::InstallNotFound,
                std::format("no Steam library has app {} ({} libraries in {} Steam folders)", desc.steamAppId,
                            libraries->size(), steamFolders.size()));
}

} // namespace nx::assets
