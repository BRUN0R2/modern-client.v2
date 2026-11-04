// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

constexpr std::string_view kByteOrderMark = "\xEF\xBB\xBF";
constexpr char kEndOfText = '\0';
constexpr std::uint32_t kMaximumUnicodeEscapeDigits = 6;
constexpr std::uint32_t kHexadecimalBase = 16;
constexpr char32_t kFirstPrintable = 0x20;
constexpr char32_t kDelete = 0x7F;
constexpr unsigned kAsciiLimit = 0x80U;

[[nodiscard]] constexpr auto isDecimalDigit(char character) noexcept -> bool {
    return character >= '0' && character <= '9';
}

[[nodiscard]] constexpr auto isBinaryDigit(char character) noexcept -> bool {
    return character == '0' || character == '1';
}

[[nodiscard]] constexpr auto isHexadecimalDigit(char character) noexcept -> bool {
    return isDecimalDigit(character) || (character >= 'a' && character <= 'f') ||
           (character >= 'A' && character <= 'F');
}

[[nodiscard]] constexpr auto isIdentifierStart(char character) noexcept -> bool {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           character == '_';
}

[[nodiscard]] constexpr auto isIdentifierPart(char character) noexcept -> bool {
    return isIdentifierStart(character) || isDecimalDigit(character);
}

[[nodiscard]] constexpr auto isLineEnd(char character) noexcept -> bool {
    return character == '\n' || character == '\r';
}

/// The characters that change the direction of text: refused everywhere, because a source could then
/// read differently from what it does (rook/spec/grammar.ebnf, section 1).
[[nodiscard]] constexpr auto changesTextDirection(char32_t character) noexcept -> bool {
    constexpr char32_t kFirstEmbedding = 0x202A;
    constexpr char32_t kLastEmbedding = 0x202E;
    constexpr char32_t kFirstIsolate = 0x2066;
    constexpr char32_t kLastIsolate = 0x2069;
    return (character >= kFirstEmbedding && character <= kLastEmbedding) ||
           (character >= kFirstIsolate && character <= kLastIsolate);
}

[[nodiscard]] auto hexadecimalValue(char character) noexcept -> std::uint32_t {
    constexpr std::uint32_t kFirstLetterValue = 10;
    if (isDecimalDigit(character)) {
        return static_cast<std::uint32_t>(character - '0');
    }
    const char lower = character >= 'a' ? character : static_cast<char>(character - 'A' + 'a');
    return kFirstLetterValue + static_cast<std::uint32_t>(lower - 'a');
}

/// Reads one source into tokens. The strings it has opened are a stack: a string holds interpolations,
/// which hold expressions, which hold strings, and an input cannot exhaust a stack of its own memory.
class Lexer {
public:
    explicit Lexer(std::string_view sourceText) : source{sourceText} {
        tokens.reserve(sourceText.size() / 4 + 1);
    }

    [[nodiscard]] auto run() && -> TokenizedSource {
        if (source.starts_with(kByteOrderMark)) {
            offset = static_cast<ByteOffset>(kByteOrderMark.size());
            addToken(TokenKind::Whitespace, 0);
        }
        while (!atEnd()) {
            lexToken();
        }
        if (!interpolations.empty()) {
            reportUnterminatedString(interpolations.back().stringStart);
            abandonStrings();
        }
        addToken(TokenKind::EndOfFile, offset);
        return TokenizedSource{.tokens = std::move(tokens), .diagnostics = std::move(diagnostics)};
    }

private:
    /// An interpolation being read: where its string started, and the brackets open inside it, so that
    /// its `:` and `}` can be told from an inner one's.
    struct Interpolation {
        ByteOffset stringStart = 0;
        std::uint32_t openBrackets = 0;
    };

    [[nodiscard]] auto atEnd() const noexcept -> bool { return offset >= source.size(); }

    [[nodiscard]] auto peek(std::size_t ahead = 0) const noexcept -> char {
        return offset + ahead < source.size() ? source[offset + ahead] : kEndOfText;
    }

    void addToken(TokenKind kind, ByteOffset start) {
        tokens.push_back(Token{.kind = kind, .range = TextRange{.start = start, .end = offset}});
    }

    void report(DiagnosticCode code, TextRange range, std::string message, std::string label,
                std::string fix) {
        addDiagnostic(diagnostics, Diagnostic{.code = code,
                                              .range = range,
                                              .message = std::move(message),
                                              .label = std::move(label),
                                              .fix = std::move(fix)});
    }

    void lexToken() {
        const ByteOffset start = offset;
        const char character = peek();
        if (!interpolations.empty() && isLineEnd(character)) {
            reportUnterminatedString(interpolations.back().stringStart);
            abandonStrings();
            return;
        }
        if (character == ' ' || character == '\t' || isLineEnd(character)) {
            lexWhitespace();
        } else if (isDecimalDigit(character)) {
            lexNumber();
        } else if (character == 'r' && peek(1) == '"') {
            lexRawString();
        } else if (isIdentifierStart(character)) {
            lexWord();
        } else if (character == '"') {
            offset += 1;
            addToken(TokenKind::StringStart, start);
            lexStringBody(start);
        } else if (character == '\'') {
            lexQuote();
        } else if (character == '/' && peek(1) == '/') {
            lexLineComment();
        } else if (character == '/' && peek(1) == '*') {
            lexBlockComment();
        } else if (character == '#') {
            lexDirective();
        } else if (static_cast<unsigned char>(character) >= kAsciiLimit) {
            lexOutsideAscii();
        } else {
            lexPunctuation();
        }
    }

    void lexWhitespace() {
        const ByteOffset start = offset;
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || isLineEnd(peek()))) {
            if (!interpolations.empty() && isLineEnd(peek())) {
                break;
            }
            offset += 1;
        }
        addToken(TokenKind::Whitespace, start);
    }

    void lexWord() {
        const ByteOffset start = offset;
        while (isIdentifierPart(peek())) {
            offset += 1;
        }
        const std::string_view word = source.substr(start, offset - start);
        if (word == "_") {
            addToken(TokenKind::Underscore, start);
        } else {
            addToken(reservedWordKind(word).value_or(TokenKind::Identifier), start);
        }
    }

    /// A predicate over the digits of a number's base.
    using DigitTest = bool (*)(char) noexcept;

    /// Reads digits and their separators, and returns how many digits. It reports a separator that is
    /// not between two digits, and a decimal digit outside the base, which it reads on: `0b102` is one
    /// token.
    auto lexDigits(DigitTest isDigit) -> std::uint32_t {
        std::uint32_t digits = 0;
        bool afterSeparator = false;
        while (true) {
            const char character = peek();
            if (character == '_') {
                const ByteOffset separator = offset;
                offset += 1;
                const bool isBeforeAnother = peek() == '_';
                if (digits == 0 || afterSeparator ||
                    (!isBeforeAnother && !isDigit(peek()) && !isDecimalDigit(peek()))) {
                    report(DiagnosticCode::MisplacedDigitSeparator, TextRange{separator, offset},
                           "a digit separator stands between two digits", "not between digits",
                           "write `_` between two digits, or remove it");
                }
                afterSeparator = true;
            } else if (isDigit(character)) {
                offset += 1;
                digits += 1;
                afterSeparator = false;
            } else if (isDecimalDigit(character)) {
                report(DiagnosticCode::InvalidDigit, TextRange{offset, offset + 1},
                       "this digit does not belong to the number's base", "not a digit of this base",
                       "write `0b` numbers with 0 and 1, and `0x` numbers with 0 to 9 and a to f");
                offset += 1;
                digits += 1;
                afterSeparator = false;
            } else {
                return digits;
            }
        }
    }

    void lexPrefixedNumber(DigitTest isDigit) {
        const ByteOffset start = offset;
        offset += 2;
        if (lexDigits(isDigit) == 0) {
            report(DiagnosticCode::MissingDigits, TextRange{start, offset}, "this number has no digits",
                   "only a base", "write the digits after the base, as `0xFF` or `0b1010`");
        }
    }

    void lexNumber() {
        const ByteOffset start = offset;
        auto kind = TokenKind::IntegerLiteral;
        if (peek() == '0' && peek(1) == 'x') {
            lexPrefixedNumber(isHexadecimalDigit);
        } else if (peek() == '0' && peek(1) == 'b') {
            lexPrefixedNumber(isBinaryDigit);
        } else {
            lexDigits(isDecimalDigit);
            if (peek() == '.' && isDecimalDigit(peek(1))) {
                kind = TokenKind::FloatLiteral;
                offset += 1;
                lexDigits(isDecimalDigit);
            }
            if (isExponentAhead()) {
                kind = TokenKind::FloatLiteral;
                offset += peek(1) == '+' || peek(1) == '-' ? 2U : 1U;
                lexDigits(isDecimalDigit);
            }
        }
        // The suffix is the unit, which the checker looks up: to the lexer it is part of the number.
        while (isIdentifierPart(peek())) {
            offset += 1;
        }
        addToken(kind, start);
    }

    [[nodiscard]] auto isExponentAhead() const noexcept -> bool {
        if (peek() != 'e' && peek() != 'E') {
            return false;
        }
        const std::size_t digitAt = peek(1) == '+' || peek(1) == '-' ? 2 : 1;
        return isDecimalDigit(peek(digitAt));
    }

    void reportInvalidUtf8(TextRange range) {
        report(DiagnosticCode::InvalidUtf8, range, "this is not UTF-8", "invalid bytes",
               "save the file as UTF-8");
    }

    void reportTextDirection(TextRange range) {
        report(DiagnosticCode::TextDirectionCharacter, range,
               "an invisible character here changes the direction of text",
               "the source can read differently from what it does", "remove this character");
    }

    /// Reports what no text of a source may hold, comments and strings included, in `text`.
    void checkText(ByteOffset start, ByteOffset end) {
        for (ByteOffset cursor = start; cursor < end;) {
            const DecodedCharacter decoded = decodeCharacter(source, cursor);
            if (!decoded.isValid) {
                const ByteOffset first = cursor;
                while (cursor < end && !decodeCharacter(source, cursor).isValid) {
                    cursor += 1;
                }
                reportInvalidUtf8(TextRange{first, cursor});
                continue;
            }
            if (changesTextDirection(decoded.codePoint)) {
                reportTextDirection(TextRange{cursor, cursor + decoded.length});
            } else if ((decoded.codePoint < kFirstPrintable && decoded.codePoint != U'\t') ||
                       decoded.codePoint == kDelete) {
                report(DiagnosticCode::UnexpectedCharacter, TextRange{cursor, cursor + decoded.length},
                       std::format("unexpected control character U+{:04X}",
                                   static_cast<std::uint32_t>(decoded.codePoint)),
                       "not allowed in a source", "remove this character");
            }
            cursor += decoded.length;
        }
    }

    void lexLineComment() {
        const ByteOffset start = offset;
        const std::size_t lineFeed = source.find('\n', offset);
        std::size_t end = lineFeed == std::string_view::npos ? source.size() : lineFeed;
        if (end > start && source[end - 1] == '\r') {
            end -= 1;
        }
        offset = static_cast<ByteOffset>(end);
        checkText(start, offset);
        const std::string_view text = source.substr(start, offset - start);
        const bool isDocumentation = text.starts_with("///") && !text.starts_with("////");
        addToken(isDocumentation ? TokenKind::DocumentationComment : TokenKind::LineComment, start);
    }

    void lexBlockComment() {
        const ByteOffset start = offset;
        const std::size_t close = source.find("*/", offset + 2);
        if (close == std::string_view::npos) {
            offset = static_cast<ByteOffset>(source.size());
            checkBlockCommentText(start, offset);
            report(DiagnosticCode::UnterminatedBlockComment, TextRange{start, start + 2},
                   "this comment never ends", "opened here", "close it with `*/`");
        } else {
            offset = static_cast<ByteOffset>(close + 2);
            checkBlockCommentText(start, offset);
        }
        addToken(TokenKind::BlockComment, start);
    }

    /// A block comment may span lines: its line ends are not control characters.
    void checkBlockCommentText(ByteOffset start, ByteOffset end) {
        ByteOffset lineStart = start;
        for (std::size_t lineFeed = source.find('\n', start);
             lineFeed != std::string_view::npos && lineFeed < end;
             lineFeed = source.find('\n', lineFeed + 1)) {
            const bool hasCarriageReturn = lineFeed > lineStart && source[lineFeed - 1] == '\r';
            checkText(lineStart, static_cast<ByteOffset>(lineFeed - (hasCarriageReturn ? 1 : 0)));
            lineStart = static_cast<ByteOffset>(lineFeed + 1);
        }
        checkText(lineStart, end);
    }

    /// `#include` and the like are not Rook: one diagnostic for the line, with what to write instead.
    void lexDirective() {
        const ByteOffset start = offset;
        if (!isIdentifierStart(peek(1))) {
            offset += 1;
            report(DiagnosticCode::UnexpectedCharacter, TextRange{start, offset}, "unexpected character `#`",
                   "not part of Rook", "remove this character");
            addToken(TokenKind::Invalid, start);
            return;
        }
        while (!atEnd() && !isLineEnd(peek())) {
            offset += 1;
        }
        report(DiagnosticCode::PreprocessorDirective, TextRange{start, offset}, "Rook has no preprocessor",
               "a directive of Pawn", "import a module with `import module.name;`");
        addToken(TokenKind::Invalid, start);
    }

    void lexOutsideAscii() {
        const ByteOffset start = offset;
        const DecodedCharacter decoded = decodeCharacter(source, offset);
        if (!decoded.isValid) {
            while (!atEnd() && !decodeCharacter(source, offset).isValid) {
                offset += 1;
            }
            reportInvalidUtf8(TextRange{start, offset});
        } else {
            offset += decoded.length;
            if (changesTextDirection(decoded.codePoint)) {
                reportTextDirection(TextRange{start, offset});
            } else {
                report(DiagnosticCode::UnexpectedCharacter, TextRange{start, offset},
                       std::format("unexpected character U+{:04X}",
                                   static_cast<std::uint32_t>(decoded.codePoint)),
                       "only strings and comments hold such characters",
                       "remove this character, or write a name in ASCII");
            }
        }
        addToken(TokenKind::Invalid, start);
    }

    /// An escape after a backslash, in a string or a character. Returns false at a line end, where a
    /// backslash escapes nothing and the text is unterminated.
    auto lexEscape() -> bool {
        const ByteOffset start = offset;
        offset += 1;
        const char character = peek();
        if (atEnd() || isLineEnd(character)) {
            return false;
        }
        switch (character) {
        case 'n':
        case 'r':
        case 't':
        case '0':
        case '\\':
        case '\'':
        case '"':
        case '{':
            offset += 1;
            return true;
        case 'u':
            lexUnicodeEscape(start);
            return true;
        default:
            break;
        }
        offset += decodeCharacter(source, offset).length;
        report(DiagnosticCode::UnknownEscape, TextRange{start, offset}, "this escape does not exist",
               "unknown escape", R"(the escapes are \n \r \t \0 \\ \' \" \{ and \u{...})");
        return true;
    }

    void lexUnicodeEscape(ByteOffset start) {
        offset += 1;
        const auto invalid = [&] {
            report(DiagnosticCode::InvalidUnicodeEscape, TextRange{start, offset},
                   "this Unicode escape is not valid", "not a character",
                   R"(write a character's number in hexadecimal, as \u{1F600})");
        };
        if (peek() != '{') {
            invalid();
            return;
        }
        offset += 1;
        std::uint32_t digits = 0;
        std::uint32_t value = 0;
        while (isHexadecimalDigit(peek())) {
            if (digits < kMaximumUnicodeEscapeDigits) {
                value = value * kHexadecimalBase + hexadecimalValue(peek());
            }
            digits += 1;
            offset += 1;
        }
        if (peek() != '}') {
            invalid();
            return;
        }
        offset += 1;
        if (digits == 0 || digits > kMaximumUnicodeEscapeDigits || !isScalarValue(value)) {
            invalid();
        }
    }

    void reportUnterminatedString(ByteOffset stringStart) {
        report(DiagnosticCode::UnterminatedString, TextRange{stringStart, stringStart + 1},
               "this string does not end on its line", "opened here",
               "close it with `\"` before the line ends");
    }

    /// Closes, with tokens of no length, every string and interpolation still open.
    void abandonStrings() {
        while (!interpolations.empty()) {
            interpolations.pop_back();
            addToken(TokenKind::InterpolationEnd, offset);
            addToken(TokenKind::StringEnd, offset);
        }
    }

    /// Reads a string's text until its end or its next interpolation.
    void lexStringBody(ByteOffset stringStart) {
        const ByteOffset start = offset;
        while (true) {
            const char character = peek();
            if (atEnd() || isLineEnd(character)) {
                addStringText(start);
                reportUnterminatedString(stringStart);
                addToken(TokenKind::StringEnd, offset);
                abandonStrings();
                return;
            }
            if (character == '"') {
                addStringText(start);
                const ByteOffset quote = offset;
                offset += 1;
                addToken(TokenKind::StringEnd, quote);
                return;
            }
            if (character == '{') {
                addStringText(start);
                const ByteOffset brace = offset;
                offset += 1;
                addToken(TokenKind::InterpolationStart, brace);
                interpolations.push_back(Interpolation{.stringStart = stringStart});
                return;
            }
            if (character == '\\') {
                if (!lexEscape()) {
                    continue; // the line ends after the backslash, which the top of the loop reports
                }
            } else {
                offset += decodeCharacter(source, offset).length;
            }
        }
    }

    void addStringText(ByteOffset start) {
        if (offset > start) {
            checkText(start, offset);
            addToken(TokenKind::StringText, start);
        }
    }

    void lexRawString() {
        const ByteOffset start = offset;
        offset += 2;
        while (!atEnd() && peek() != '"' && !isLineEnd(peek())) {
            offset += 1;
        }
        checkText(start, offset);
        if (peek() == '"') {
            offset += 1;
        } else {
            reportUnterminatedString(start);
        }
        addToken(TokenKind::RawStringLiteral, start);
    }

    /// A quote starts a label or a character: `'outer` closes no quote after its name, `'a'` does.
    void lexQuote() {
        const ByteOffset start = offset;
        if (isIdentifierStart(peek(1))) {
            std::size_t ahead = 1;
            while (isIdentifierPart(peek(ahead))) {
                ahead += 1;
            }
            if (peek(ahead) != '\'') {
                offset += static_cast<ByteOffset>(ahead);
                addToken(TokenKind::Label, start);
                return;
            }
        }
        lexCharacter();
    }

    void lexCharacter() {
        const ByteOffset start = offset;
        offset += 1;
        std::uint32_t characters = 0;
        while (!atEnd() && peek() != '\'' && !isLineEnd(peek())) {
            characters += 1;
            if (peek() == '\\') {
                lexEscape();
            } else {
                offset += decodeCharacter(source, offset).length;
            }
        }
        checkText(start, offset);
        if (peek() != '\'') {
            report(DiagnosticCode::UnterminatedCharacter, TextRange{start, start + 1},
                   "this character does not end on its line", "opened here", "close it with `'`");
        } else {
            offset += 1;
            if (characters != 1) {
                report(DiagnosticCode::CharacterLiteralLength, TextRange{start, offset},
                       "a character literal holds exactly one character", "not one character",
                       "write one character between the quotes, or a string between `\"`");
            }
        }
        addToken(TokenKind::CharacterLiteral, start);
    }

    void addPunctuation(TokenKind kind, std::uint32_t length) {
        const ByteOffset start = offset;
        offset += length;
        addToken(kind, start);
    }

    void lexPunctuation() {
        const char character = peek();
        const char next = peek(1);
        switch (character) {
        case '(':
        case '[':
            openBracket();
            return addPunctuation(character == '(' ? TokenKind::LeftParenthesis : TokenKind::LeftBracket, 1);
        case '{':
            openBracket();
            return addPunctuation(TokenKind::LeftBrace, 1);
        case ')':
        case ']':
            closeBracket();
            return addPunctuation(character == ')' ? TokenKind::RightParenthesis : TokenKind::RightBracket,
                                  1);
        case '}':
            return lexClosingBrace();
        case ':':
            return lexColon();
        case ',':
            return addPunctuation(TokenKind::Comma, 1);
        case ';':
            return addPunctuation(TokenKind::Semicolon, 1);
        case '.':
            if (next == '.') {
                return peek(2) == '=' ? addPunctuation(TokenKind::DotDotEqual, 3)
                                      : addPunctuation(TokenKind::DotDot, 2);
            }
            return addPunctuation(TokenKind::Dot, 1);
        case '?':
            return next == '?' ? addPunctuation(TokenKind::QuestionQuestion, 2)
                               : addPunctuation(TokenKind::Question, 1);
        case '=':
            if (next == '=') {
                return addPunctuation(TokenKind::EqualEqual, 2);
            }
            return next == '>' ? addPunctuation(TokenKind::FatArrow, 2) : addPunctuation(TokenKind::Equal, 1);
        case '!':
            return next == '=' ? addPunctuation(TokenKind::BangEqual, 2) : addPunctuation(TokenKind::Bang, 1);
        case '<':
            return next == '=' ? addPunctuation(TokenKind::LessEqual, 2) : addPunctuation(TokenKind::Less, 1);
        case '>':
            return addPunctuation(TokenKind::Greater, 1);
        case '+':
            return addPunctuation(TokenKind::Plus, 1);
        case '-':
            return next == '>' ? addPunctuation(TokenKind::Arrow, 2) : addPunctuation(TokenKind::Minus, 1);
        case '*':
            return addPunctuation(TokenKind::Star, 1);
        case '/':
            return addPunctuation(TokenKind::Slash, 1);
        case '%':
            return addPunctuation(TokenKind::Percent, 1);
        case '&':
            return next == '&' ? addPunctuation(TokenKind::AmpersandAmpersand, 2)
                               : addPunctuation(TokenKind::Ampersand, 1);
        case '|':
            return next == '|' ? addPunctuation(TokenKind::PipePipe, 2) : addPunctuation(TokenKind::Pipe, 1);
        case '@':
            return addPunctuation(TokenKind::At, 1);
        default:
            return lexUnexpectedAscii();
        }
    }

    void lexUnexpectedAscii() {
        const ByteOffset start = offset;
        const auto character = static_cast<unsigned char>(peek());
        offset += 1;
        report(DiagnosticCode::UnexpectedCharacter, TextRange{start, offset},
               std::format("unexpected character U+{:04X}", static_cast<std::uint32_t>(character)),
               "not part of Rook", "remove this character");
        addToken(TokenKind::Invalid, start);
    }

    void openBracket() {
        if (!interpolations.empty()) {
            interpolations.back().openBrackets += 1;
        }
    }

    void closeBracket() {
        if (!interpolations.empty() && interpolations.back().openBrackets > 0) {
            interpolations.back().openBrackets -= 1;
        }
    }

    void lexClosingBrace() {
        if (!interpolations.empty() && interpolations.back().openBrackets == 0) {
            const ByteOffset stringStart = interpolations.back().stringStart;
            interpolations.pop_back();
            addPunctuation(TokenKind::InterpolationEnd, 1);
            lexStringBody(stringStart);
            return;
        }
        closeBracket();
        addPunctuation(TokenKind::RightBrace, 1);
    }

    /// In an interpolation, outside any bracket, a `:` starts the format specifier.
    void lexColon() {
        addPunctuation(TokenKind::Colon, 1);
        if (interpolations.empty() || interpolations.back().openBrackets > 0) {
            return;
        }
        const ByteOffset start = offset;
        while (!atEnd() && peek() != '}' && peek() != '"' && !isLineEnd(peek())) {
            offset += 1;
        }
        if (offset > start) {
            checkText(start, offset);
            addToken(TokenKind::FormatSpecifier, start);
        }
    }

    std::string_view source;
    ByteOffset offset = 0;
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
    std::vector<Interpolation> interpolations;
};

} // namespace

auto tokenize(std::string_view source) -> TokenizedSource {
    return Lexer{source}.run();
}

} // namespace rook::compiler
