// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// How a diagnostic reads (docs/14-rook-language.md, section 23), on any source: a source is untrusted,
// and what it holds must not reach a terminal.

#include <gtest/gtest.h>

import std;
import rook.compiler;

namespace {

using rook::compiler::Diagnostic;
using rook::compiler::DiagnosticCode;
using rook::compiler::diagnosticCodeText;
using rook::compiler::LineMap;
using rook::compiler::parse;
using rook::compiler::renderDiagnostic;
using rook::compiler::textForTerminal;
using rook::compiler::TextPosition;
using rook::compiler::TextRange;

[[nodiscard]] auto render(std::string_view source, TextRange range) -> std::string {
    const LineMap lines{source};
    const Diagnostic diagnostic{.code = DiagnosticCode::UnexpectedCharacter,
                                .range = range,
                                .message = "the message",
                                .label = "the label",
                                .fix = "the fix"};
    return renderDiagnostic(diagnostic, "plugins/sample.rook", source, lines);
}

TEST(DiagnosticTest, ReadsAsDocument14Shows) {
    EXPECT_EQ(render("first\n    second third\n", TextRange{.start = 10, .end = 16}),
              R"(error[R0002]: the message
 --> plugins/sample.rook:2:5
  |
2 |     second third
  |     ^^^^^^ the label
  = fix: the fix
)");
}

TEST(DiagnosticTest, ItsGutterFollowsTheLineNumber) {
    std::string source;
    for (int line = 1; line < 120; ++line) {
        source += '\n';
    }
    source += "last";
    EXPECT_EQ(render(source, TextRange{.start = 119, .end = 123}), R"(error[R0002]: the message
   --> plugins/sample.rook:120:1
    |
120 | last
    | ^^^^ the label
    = fix: the fix
)");
}

TEST(DiagnosticTest, ThereIsAlwaysACaret) {
    EXPECT_TRUE(render("abc", TextRange{.start = 1, .end = 1}).contains("  |  ^ the label"));
    EXPECT_TRUE(render("abc", TextRange{.start = 3, .end = 3}).contains("  |    ^ the label"));
    EXPECT_TRUE(render("", TextRange{.start = 0, .end = 0}).contains("  | ^ the label"));
}

TEST(DiagnosticTest, ARangeOverSeveralLinesPointsAtItsFirst) {
    const std::string text = render("ab\ncd\n", TextRange{.start = 1, .end = 5});
    EXPECT_TRUE(text.contains("1 | ab\n"));
    EXPECT_TRUE(text.contains("  |  ^ the label"));
}

TEST(DiagnosticTest, LeavesALineEndOutOfTheSnippet) {
    EXPECT_TRUE(render("abc\r\ndef", TextRange{.start = 0, .end = 3}).contains("1 | abc\n"));
}

TEST(DiagnosticTest, CountsCharactersAndNotBytes) {
    const std::string source = "é€😀 x";
    const auto offset = static_cast<std::uint32_t>(source.size() - 1);
    const std::string text = render(source, TextRange{.start = offset, .end = offset + 1});
    EXPECT_TRUE(text.contains("plugins/sample.rook:1:5")) << text;
    EXPECT_TRUE(text.contains("  |     ^ the label")) << text;
}

TEST(DiagnosticTest, ATabTakesItsCellsInTheSnippetAndUnderIt) {
    const std::string text = render("\tx", TextRange{.start = 1, .end = 2});
    EXPECT_TRUE(text.contains("1 |     x\n")) << text;
    EXPECT_TRUE(text.contains("  |     ^ the label")) << text;
}

TEST(DiagnosticTest, NoControlCharacterOfASourceReachesATerminal) {
    // An escape sequence that would clear the screen, a bell, a direction change and invalid bytes.
    const std::string source = "a\x1B[2J\x07\xE2\x80\xAE\xFF\xC0z";
    const std::string text =
        render(source, TextRange{.start = 0, .end = static_cast<std::uint32_t>(source.size())});
    EXPECT_FALSE(text.contains('\x1B'));
    EXPECT_FALSE(text.contains('\x07'));
    EXPECT_FALSE(text.contains("\xE2\x80\xAE"));
    EXPECT_FALSE(text.contains('\xFF'));
    EXPECT_TRUE(text.contains("a\xEF\xBF\xBD[2J")); // U+FFFD stands in for each
}

TEST(DiagnosticTest, NoControlCharacterOfAPathReachesATerminal) {
    const LineMap lines{""};
    const Diagnostic diagnostic{.code = DiagnosticCode::UnexpectedCharacter, .message = "m", .fix = "f"};
    const std::string text = renderDiagnostic(diagnostic, "evil\x1B[2J.rook", "", lines);
    EXPECT_FALSE(text.contains('\x1B'));
}

TEST(DiagnosticTest, NoDiagnosticRepeatsTheSourcesText) {
    // Whatever a source holds, the words of a diagnostic are the compiler's; only the snippet shows it.
    const auto result = parse("\x1B[2J $ fn fn() { \"\\\x1B\" }");
    ASSERT_FALSE(result.diagnostics.empty());
    for (const Diagnostic& diagnostic : result.diagnostics) {
        EXPECT_FALSE(diagnostic.message.contains('\x1B'));
        EXPECT_FALSE(diagnostic.label.contains('\x1B'));
        EXPECT_FALSE(diagnostic.fix.contains('\x1B'));
    }
}

TEST(DiagnosticTest, CutsALongLineAroundWhatItPointsAt) {
    constexpr std::size_t kLength = 1'000'000;
    std::string source(kLength, 'a');
    source[kLength / 2] = 'X';
    const auto middle = static_cast<std::uint32_t>(kLength / 2);
    const std::string text = render(source, TextRange{.start = middle, .end = middle + 1});
    EXPECT_LT(text.size(), std::size_t{500}) << "a line this long does not reach the terminal whole";
    EXPECT_TRUE(text.contains('X'));
    EXPECT_TRUE(text.contains("..."));

    // The caret stands under what it points at, whatever the cut left before it.
    std::size_t snippetColumn = 0;
    std::size_t caretColumn = 0;
    std::size_t lineStart = 0;
    for (std::size_t lineEnd = text.find('\n'); lineEnd != std::string::npos;
         lineEnd = text.find('\n', lineStart)) {
        const std::string_view line = std::string_view{text}.substr(lineStart, lineEnd - lineStart);
        if (const std::size_t mark = line.find('X'); mark != std::string_view::npos) {
            snippetColumn = mark;
        }
        if (const std::size_t mark = line.find('^'); mark != std::string_view::npos) {
            caretColumn = mark;
        }
        lineStart = lineEnd + 1;
    }
    EXPECT_NE(snippetColumn, 0U);
    EXPECT_EQ(snippetColumn, caretColumn);
}

TEST(DiagnosticTest, ACaretIsNeverLongerThanTheSnippet) {
    constexpr std::size_t kLength = 100'000;
    const std::string source(kLength, 'a');
    const std::string text =
        render(source, TextRange{.start = 0, .end = static_cast<std::uint32_t>(kLength)});
    EXPECT_LT(text.size(), std::size_t{500});
}

TEST(DiagnosticTest, WritesItsCodeWithFourDigits) {
    EXPECT_EQ(diagnosticCodeText(DiagnosticCode::InvalidUtf8), "R0001");
    EXPECT_EQ(diagnosticCodeText(DiagnosticCode::ConditionWithoutParentheses), "R0030");
}

TEST(DiagnosticTest, EveryCodeIsListedOnce) {
    auto codes = rook::compiler::kDiagnosticCodes;
    std::ranges::sort(codes);
    EXPECT_EQ(std::ranges::adjacent_find(codes), codes.end());
}

TEST(LineMapTest, FindsLinesAndColumns) {
    const std::string_view source = "ab\n\ncd";
    const LineMap lines{source};
    EXPECT_EQ(lines.positionOf(0), (TextPosition{.line = 1, .column = 1}));
    EXPECT_EQ(lines.positionOf(1), (TextPosition{.line = 1, .column = 2}));
    EXPECT_EQ(lines.positionOf(2), (TextPosition{.line = 1, .column = 3}));
    EXPECT_EQ(lines.positionOf(3), (TextPosition{.line = 2, .column = 1}));
    EXPECT_EQ(lines.positionOf(4), (TextPosition{.line = 3, .column = 1}));
    EXPECT_EQ(lines.positionOf(6), (TextPosition{.line = 3, .column = 3}));
    EXPECT_EQ(lines.positionOf(1000), (TextPosition{.line = 3, .column = 3}));
}

TEST(LineMapTest, FindsALinesBytesWithoutItsEnd) {
    const std::string_view source = "ab\r\ncd\n\nef";
    const LineMap lines{source};
    EXPECT_EQ(lines.lineAround(1), (TextRange{.start = 0, .end = 2}));
    EXPECT_EQ(lines.lineAround(5), (TextRange{.start = 4, .end = 6}));
    EXPECT_EQ(lines.lineAround(7), (TextRange{.start = 7, .end = 7}));
    EXPECT_EQ(lines.lineAround(9), (TextRange{.start = 8, .end = 10}));
}

TEST(TextForTerminalTest, KeepsWhatIsPrintable) {
    EXPECT_EQ(textForTerminal("plugins/ação/plugin.rook"), "plugins/ação/plugin.rook");
    EXPECT_EQ(textForTerminal("a\tb"), "a b");
    EXPECT_EQ(textForTerminal(""), "");
}

} // namespace
