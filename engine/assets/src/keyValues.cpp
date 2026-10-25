// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

enum class TokenKind : std::uint8_t {
    Word,      ///< A key or a value.
    Open,      ///< `{`.
    Close,     ///< `}`.
    Condition, ///< `[$WIN32]`.
};

struct Token {
    TokenKind kind = TokenKind::Word;
    std::string text{};
    std::size_t line = 0; ///< Where it starts, from 1.
};

[[nodiscard]] constexpr auto isSpace(char character) noexcept -> bool {
    return static_cast<unsigned char>(character) <= static_cast<unsigned char>(' ');
}

/// What `\c` stands for in a quoted word with Backslash escapes; none when it is no escape.
[[nodiscard]] constexpr auto escaped(char character) noexcept -> std::optional<char> {
    switch (character) {
    case 'n':
        return '\n';
    case 't':
        return '\t';
    case '\\':
        return '\\';
    case '"':
        return '"';
    default:
        return std::nullopt;
    }
}

/// The tokens of `text`, comments and whitespace left out.
[[nodiscard]] auto tokenize(std::string_view text, KeyValuesEscapes escapes)
    -> Result<std::vector<Token>, KeyValuesError> {
    std::vector<Token> tokens;
    std::size_t line = 1;
    std::size_t at = 0;
    for (;;) {
        while (at < text.size()) {
            if (text[at] == '\n') {
                ++line;
                ++at;
            } else if (isSpace(text[at])) {
                ++at;
            } else if (text.substr(at).starts_with("//")) {
                at = std::min(text.find('\n', at), text.size());
            } else {
                break;
            }
        }
        if (at == text.size()) {
            return tokens;
        }
        const char first = text[at];
        if (first == '{' || first == '}') {
            tokens.push_back({.kind = first == '{' ? TokenKind::Open : TokenKind::Close, .line = line});
            ++at;
            continue;
        }
        if (first == '"') {
            const std::size_t startLine = line;
            std::string word;
            for (++at;; ++at) {
                if (at == text.size()) {
                    return fail(KeyValuesError::UnclosedQuote, std::format("the word of line {}", startLine));
                }
                const char character = text[at];
                if (character == '"') {
                    ++at;
                    break;
                }
                if (character == '\\' && escapes == KeyValuesEscapes::Backslash && at + 1 < text.size()) {
                    if (const std::optional<char> meant = escaped(text[at + 1])) {
                        word += *meant;
                        ++at;
                        continue;
                    }
                }
                line += character == '\n' ? 1U : 0U;
                word += character;
            }
            tokens.push_back({.kind = TokenKind::Word, .text = std::move(word), .line = startLine});
            continue;
        }
        if (first == '[') {
            const std::size_t close = text.find_first_of("]\n", at);
            if (close != std::string_view::npos && text[close] == ']') {
                tokens.push_back({.kind = TokenKind::Condition,
                                  .text = std::string{text.substr(at, close + 1 - at)},
                                  .line = line});
                at = close + 1;
                continue;
            }
        }
        const std::size_t start = at;
        while (at < text.size() && !isSpace(text[at]) && text[at] != '{' && text[at] != '}' &&
               text[at] != '"') {
            ++at;
        }
        tokens.push_back(
            {.kind = TokenKind::Word, .text = std::string{text.substr(start, at - start)}, .line = line});
    }
}

/// The entries of the tokens of a text, block by block.
class Parser {
public:
    explicit Parser(std::span<const Token> tokens) noexcept : m_tokens{tokens} {}

    /// The entries up to the end of the block opened on `openLine` at `depth`, or of the text
    /// without one.
    [[nodiscard]] auto entries(std::size_t depth, std::optional<std::size_t> openLine)
        -> Result<std::vector<KeyValuesEntry>, KeyValuesError> {
        std::vector<KeyValuesEntry> found;
        for (;;) {
            if (m_at == m_tokens.size()) {
                if (openLine) {
                    return fail(KeyValuesError::UnclosedBlock,
                                std::format("the block of line {}", *openLine));
                }
                return found;
            }
            const Token& token = m_tokens[m_at++];
            switch (token.kind) {
            case TokenKind::Close:
                if (!openLine) {
                    return fail(KeyValuesError::StrayBrace,
                                std::format("line {}: a }} that closes no block", token.line));
                }
                return found;
            case TokenKind::Open:
                return fail(KeyValuesError::StrayBrace,
                            std::format("line {}: a {{ without a key", token.line));
            case TokenKind::Condition:
                if (!found.empty()) {
                    found.back().condition = token.text; // after a block's }
                }
                continue;
            case TokenKind::Word:
                break;
            }
            auto parsed = entry(token, depth);
            if (!parsed) {
                return std::unexpected{std::move(parsed.error())};
            }
            found.push_back(std::move(*parsed));
        }
    }

private:
    /// The entry of the key `key`, the tokens after it its value and conditions.
    [[nodiscard]] auto entry(const Token& key, std::size_t depth) -> Result<KeyValuesEntry, KeyValuesError> {
        KeyValuesEntry made{.key = key.text};
        if (m_at < m_tokens.size() && m_tokens[m_at].kind == TokenKind::Condition) {
            made.condition = m_tokens[m_at++].text;
        }
        if (m_at == m_tokens.size() || m_tokens[m_at].kind == TokenKind::Close ||
            m_tokens[m_at].kind == TokenKind::Condition) {
            return fail(KeyValuesError::MissingValue, std::format("line {}: {:?}", key.line, key.text));
        }
        const Token& value = m_tokens[m_at++];
        if (value.kind == TokenKind::Open) {
            if (depth == kKeyValuesMaxDepth) {
                return fail(KeyValuesError::TooDeep, std::format("line {}", value.line));
            }
            auto inner = entries(depth + 1, value.line);
            if (!inner) {
                return std::unexpected{std::move(inner.error())};
            }
            made.entries = std::move(*inner);
            made.isBlock = true;
        } else {
            made.word = value.text;
        }
        if (m_at < m_tokens.size() && m_tokens[m_at].kind == TokenKind::Condition) {
            made.condition = m_tokens[m_at++].text;
        }
        return made;
    }

    std::span<const Token> m_tokens;
    std::size_t m_at = 0;
};

} // namespace

auto KeyValuesEntry::find(std::string_view name) const noexcept -> const KeyValuesEntry* {
    const auto found = std::ranges::find_if(
        entries, [name](const KeyValuesEntry& entry) { return equalsIgnoringCase(entry.key, name); });
    return found == entries.end() ? nullptr : &*found;
}

auto KeyValuesEntry::findWord(std::string_view name) const noexcept -> std::optional<std::string_view> {
    const KeyValuesEntry* found = find(name);
    return found == nullptr || found->isBlock ? std::nullopt : std::optional<std::string_view>{found->word};
}

auto parseKeyValues(std::string_view text, KeyValuesEscapes escapes)
    -> Result<std::vector<KeyValuesEntry>, KeyValuesError> {
    auto tokens = tokenize(text, escapes);
    if (!tokens) {
        return std::unexpected{std::move(tokens.error())};
    }
    return Parser{*tokens}.entries(0, std::nullopt);
}

auto keyValuesText(std::span<const std::byte> bytes) -> Result<std::string, KeyValuesError> {
    constexpr std::array kUtf16Mark{std::byte{0xFF}, std::byte{0xFE}};
    constexpr std::array kUtf8Mark{std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF}};
    const auto characters = [](std::span<const std::byte> part) {
        return part | std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
               std::ranges::to<std::string>();
    };
    if (!std::ranges::starts_with(bytes, kUtf16Mark)) {
        return characters(std::ranges::starts_with(bytes, kUtf8Mark) ? bytes.subspan(kUtf8Mark.size())
                                                                     : bytes);
    }
    const std::span<const std::byte> units = bytes.subspan(kUtf16Mark.size());
    if (units.size() % 2 != 0) {
        return fail(KeyValuesError::BadEncoding, "UTF-16 of an odd number of bytes");
    }
    const auto unitAt = [units](std::size_t index) {
        return static_cast<char32_t>(loadLittleEndian<std::uint16_t>(units.subspan(index)));
    };
    std::string text;
    text.reserve(units.size() / 2);
    for (std::size_t index = 0; index < units.size(); index += 2) {
        char32_t code = unitAt(index);
        if (isLowSurrogate(code)) {
            return fail(KeyValuesError::BadEncoding,
                        std::format("a low surrogate alone at byte {}", index + 2));
        }
        if (isHighSurrogate(code)) {
            const char32_t low = index + 3 < units.size() ? unitAt(index + 2) : char32_t{};
            if (!isLowSurrogate(low)) {
                return fail(KeyValuesError::BadEncoding,
                            std::format("a high surrogate alone at byte {}", index + 2));
            }
            code = characterOfSurrogates(code, low);
            index += 2;
        }
        appendUtf8(text, code);
    }
    return text;
}

} // namespace nx::assets
