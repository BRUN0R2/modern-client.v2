// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// The lexer: what the conformance suite's sources cannot say, because it takes bytes a file of the
// repository does not hold (line ends other than LF) or a token by token view.

#include <gtest/gtest.h>

import std;
import rook.compiler;

namespace {

using rook::compiler::DiagnosticCode;
using rook::compiler::reservedWordKind;
using rook::compiler::TextRange;
using rook::compiler::Token;
using rook::compiler::tokenize;
using rook::compiler::TokenKind;

/// The kinds of the tokens the parser reads of `source`, the end of the file left out.
[[nodiscard]] auto significantKinds(std::string_view source) -> std::vector<TokenKind> {
    std::vector<TokenKind> kinds;
    for (const Token& token : tokenize(source).tokens) {
        if (!rook::compiler::isTrivia(token.kind) && token.kind != TokenKind::EndOfFile) {
            kinds.push_back(token.kind);
        }
    }
    return kinds;
}

[[nodiscard]] auto codesOf(std::string_view source) -> std::vector<DiagnosticCode> {
    std::vector<DiagnosticCode> codes;
    for (const auto& diagnostic : tokenize(source).diagnostics) {
        codes.push_back(diagnostic.code);
    }
    return codes;
}

/// Tokens must follow each other with no gap and no overlap, from the first byte to the last.
void expectEveryByteCovered(std::string_view source) {
    const auto tokens = tokenize(source).tokens;
    ASSERT_FALSE(tokens.empty());
    EXPECT_EQ(tokens.back().kind, TokenKind::EndOfFile);
    std::uint32_t expectedStart = 0;
    for (const Token& token : tokens) {
        EXPECT_EQ(token.range.start, expectedStart) << source;
        EXPECT_GE(token.range.end, token.range.start) << source;
        expectedStart = token.range.end;
    }
    EXPECT_EQ(expectedStart, source.size()) << source;
}

TEST(LexerTest, CoversEveryByteOfAnySource) {
    using namespace std::string_view_literals;
    const std::array sources{
        ""sv,
        "fn main() {}\n"sv,
        "\xEF\xBB\xBF fn main() {}"sv,
        "\r\n\r\n let\tx = 1;\r\n"sv,
        "\"unterminated"sv,
        "\"{a"sv,
        "\"{\"{\"{"sv,
        "\"a{b:<5}c\\n{d}\""sv,
        "'"sv,
        "'\\"sv,
        "\"\\"sv,
        "\"\\u{"sv,
        "/* never ends"sv,
        "0x 0b 1__0 1_ 1e 1e+ 1.5e"sv,
        "\xFF\xFE\x80"sv,
        "\xF0\x9F"sv,
        "a\0b"sv,
        "#include <x>\n#"sv,
        "r\"raw"sv,
    };
    for (const std::string_view source : sources) {
        expectEveryByteCovered(source);
    }
}

TEST(LexerTest, ReadsReservedWordsAndNames) {
    EXPECT_EQ(significantKinds("fn fnord _ _a"), (std::vector{TokenKind::KeywordFn, TokenKind::Identifier,
                                                              TokenKind::Underscore, TokenKind::Identifier}));
    EXPECT_EQ(reservedWordKind("while"), TokenKind::KeywordWhile);
    EXPECT_EQ(reservedWordKind("whiles"), std::nullopt);
    EXPECT_EQ(reservedWordKind("extends"), std::nullopt); // contextual
    EXPECT_EQ(reservedWordKind(""), std::nullopt);
}

TEST(LexerTest, EveryReservedWordReadsAsItsToken) {
    for (const rook::compiler::ReservedWord& word : rook::compiler::kReservedWords) {
        EXPECT_EQ(significantKinds(word.spelling), std::vector{word.kind}) << word.spelling;
    }
}

TEST(LexerTest, KeepsAGreaterSignAlone) {
    EXPECT_EQ(significantKinds(">="), (std::vector{TokenKind::Greater, TokenKind::Equal}));
    EXPECT_EQ(significantKinds(">>"), (std::vector{TokenKind::Greater, TokenKind::Greater}));
}

TEST(LexerTest, TakesTheLongestPunctuation) {
    EXPECT_EQ(
        significantKinds("..= .. . ?? ? == = => -> - != ! <= < && & || |"),
        (std::vector{TokenKind::DotDotEqual, TokenKind::DotDot, TokenKind::Dot, TokenKind::QuestionQuestion,
                     TokenKind::Question, TokenKind::EqualEqual, TokenKind::Equal, TokenKind::FatArrow,
                     TokenKind::Arrow, TokenKind::Minus, TokenKind::BangEqual, TokenKind::Bang,
                     TokenKind::LessEqual, TokenKind::Less, TokenKind::AmpersandAmpersand,
                     TokenKind::Ampersand, TokenKind::PipePipe, TokenKind::Pipe}));
}

TEST(LexerTest, TellsNumbersFromWhatFollowsThem) {
    EXPECT_EQ(significantKinds("0..3"),
              (std::vector{TokenKind::IntegerLiteral, TokenKind::DotDot, TokenKind::IntegerLiteral}));
    EXPECT_EQ(significantKinds("1.size"),
              (std::vector{TokenKind::IntegerLiteral, TokenKind::Dot, TokenKind::Identifier}));
    EXPECT_EQ(significantKinds("1.5"), std::vector{TokenKind::FloatLiteral});
    EXPECT_EQ(significantKinds("1.5e3"), std::vector{TokenKind::FloatLiteral});
    EXPECT_EQ(significantKinds("2e-3"), std::vector{TokenKind::FloatLiteral});
    EXPECT_EQ(significantKinds("250ms"), std::vector{TokenKind::IntegerLiteral});
    EXPECT_EQ(significantKinds("1.5s"), std::vector{TokenKind::FloatLiteral});
    EXPECT_EQ(significantKinds("1e"), std::vector{TokenKind::IntegerLiteral}); // the suffix `e`
    EXPECT_TRUE(codesOf("1_000 0xFF_FF 0b10_10 1_000.000_1").empty());
}

TEST(LexerTest, TellsALabelFromACharacter) {
    EXPECT_EQ(significantKinds("'outer"), std::vector{TokenKind::Label});
    EXPECT_EQ(significantKinds("'a'"), std::vector{TokenKind::CharacterLiteral});
    EXPECT_EQ(significantKinds("'\\n'"), std::vector{TokenKind::CharacterLiteral});
    EXPECT_EQ(significantKinds("'é'"), std::vector{TokenKind::CharacterLiteral});
    EXPECT_TRUE(codesOf("'é' '\\u{1F600}' 'a'").empty());
}

TEST(LexerTest, ReadsAStringWithItsInterpolations) {
    EXPECT_EQ(significantKinds(R"("a{b:<5}c{d}")"),
              (std::vector{TokenKind::StringStart, TokenKind::StringText, TokenKind::InterpolationStart,
                           TokenKind::Identifier, TokenKind::Colon, TokenKind::FormatSpecifier,
                           TokenKind::InterpolationEnd, TokenKind::StringText, TokenKind::InterpolationStart,
                           TokenKind::Identifier, TokenKind::InterpolationEnd, TokenKind::StringEnd}));
}

TEST(LexerTest, TellsAnInterpolationsColonAndBraceFromInnerOnes) {
    EXPECT_EQ(significantKinds(R"("{f(a: 1)}")"),
              (std::vector{TokenKind::StringStart, TokenKind::InterpolationStart, TokenKind::Identifier,
                           TokenKind::LeftParenthesis, TokenKind::Identifier, TokenKind::Colon,
                           TokenKind::IntegerLiteral, TokenKind::RightParenthesis,
                           TokenKind::InterpolationEnd, TokenKind::StringEnd}));
    EXPECT_EQ(
        significantKinds(R"("{A { b: 1 }}")"),
        (std::vector{TokenKind::StringStart, TokenKind::InterpolationStart, TokenKind::Identifier,
                     TokenKind::LeftBrace, TokenKind::Identifier, TokenKind::Colon, TokenKind::IntegerLiteral,
                     TokenKind::RightBrace, TokenKind::InterpolationEnd, TokenKind::StringEnd}));
}

TEST(LexerTest, ClosesEveryStringItOpens) {
    // Tokens of no length close what the source leaves open, so the parser never meets one half of a pair.
    const auto kinds = significantKinds(R"("a{"b{c)");
    EXPECT_EQ(std::ranges::count(kinds, TokenKind::StringStart),
              std::ranges::count(kinds, TokenKind::StringEnd));
    EXPECT_EQ(std::ranges::count(kinds, TokenKind::InterpolationStart),
              std::ranges::count(kinds, TokenKind::InterpolationEnd));
    EXPECT_EQ(codesOf(R"("a{"b{c)"), std::vector{DiagnosticCode::UnterminatedString});
}

TEST(LexerTest, TreatsAByteOrderMarkAsWhitespaceAtTheStartOnly) {
    EXPECT_TRUE(codesOf("\xEF\xBB\xBF fn").empty());
    EXPECT_EQ(codesOf("fn \xEF\xBB\xBF"), std::vector{DiagnosticCode::UnexpectedCharacter});
}

TEST(LexerTest, TakesEitherLineEnd) {
    EXPECT_TRUE(codesOf("// a comment\r\n/* over\r\n   lines */\r\nfn\r\n").empty());
    const auto tokens = tokenize("// a comment\r\n").tokens;
    EXPECT_EQ(tokens.front().kind, TokenKind::LineComment);
    EXPECT_EQ(tokens.front().range, (TextRange{.start = 0, .end = 12})); // the line end is whitespace
    EXPECT_EQ(codesOf("\"text\r\n"), std::vector{DiagnosticCode::UnterminatedString});
}

TEST(LexerTest, RefusesALoneCarriageReturnInAComment) {
    // Some editors end a line there, and what follows would then read as code it is not.
    EXPECT_EQ(codesOf("// hidden\rcode"), std::vector{DiagnosticCode::UnexpectedCharacter});
    EXPECT_EQ(codesOf("/* hidden\rcode */"), std::vector{DiagnosticCode::UnexpectedCharacter});
}

TEST(LexerTest, RefusesATextDirectionCharacterEverywhere) {
    constexpr std::string_view kReverse = "\xE2\x80\xAE"; // U+202E
    for (const std::string_view container : {"// {}", "/* {} */", "\"{}\"", "r\"{}\"", "{}"}) {
        const std::string source = std::vformat(container, std::make_format_args(kReverse));
        EXPECT_EQ(codesOf(source), std::vector{DiagnosticCode::TextDirectionCharacter}) << source;
    }
}

TEST(LexerTest, RefusesInvalidUtf8Everywhere) {
    constexpr std::string_view kOverlong = "\xC0\x80";
    constexpr std::string_view kSurrogate = "\xED\xA0\x80";
    constexpr std::string_view kPastTheEnd = "\xF4\x90\x80\x80";
    for (const std::string_view bytes : {kOverlong, kSurrogate, kPastTheEnd}) {
        for (const std::string_view container : {"// {}", "\"{}\"", "{}"}) {
            const std::string source = std::vformat(container, std::make_format_args(bytes));
            EXPECT_EQ(codesOf(source), std::vector{DiagnosticCode::InvalidUtf8}) << source;
        }
    }
}

TEST(LexerTest, AcceptsEveryValidUtf8LengthInAString) {
    EXPECT_TRUE(codesOf("\"a é ação € 😀 \u{10FFFF}\"").empty());
}

} // namespace
