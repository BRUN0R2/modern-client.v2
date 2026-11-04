// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

/// What stands for a character a terminal should not receive: U+FFFD, in UTF-8.
constexpr std::string_view kReplacementCharacter = "\xEF\xBF\xBD";

constexpr char32_t kTab = U'\t';
constexpr std::string_view kTabCells = "    ";

/// How much of a long line a snippet shows, and how much of that stands before what it points at.
constexpr std::uint32_t kSnippetCells = 120;
constexpr std::uint32_t kSnippetCellsBefore = 40;
constexpr std::string_view kSnippetCut = "...";

constexpr std::uint32_t kDecimalBase = 10;

/// The characters of Unicode's control and format blocks that a terminal could act on or hide.
[[nodiscard]] constexpr auto isUnsafeForTerminal(char32_t character) noexcept -> bool {
    constexpr char32_t kFirstPrintable = 0x20;
    constexpr char32_t kDelete = 0x7F;
    constexpr char32_t kLastControl = 0x9F;
    constexpr char32_t kArabicLetterMark = 0x061C;
    constexpr char32_t kFirstZeroWidth = 0x200B;
    constexpr char32_t kLastDirectionMark = 0x200F;
    constexpr char32_t kFirstLineSeparator = 0x2028;
    constexpr char32_t kLastDirectionEmbedding = 0x202E;
    constexpr char32_t kFirstWordJoiner = 0x2060;
    constexpr char32_t kLastDirectionIsolate = 0x2069;
    constexpr char32_t kByteOrderMark = 0xFEFF;
    return character < kFirstPrintable || (character >= kDelete && character <= kLastControl) ||
           character == kArabicLetterMark ||
           (character >= kFirstZeroWidth && character <= kLastDirectionMark) ||
           (character >= kFirstLineSeparator && character <= kLastDirectionEmbedding) ||
           (character >= kFirstWordJoiner && character <= kLastDirectionIsolate) ||
           character == kByteOrderMark;
}

/// The cells of the terminal a character takes in a snippet.
[[nodiscard]] constexpr auto cellsOf(char32_t character) noexcept -> std::uint32_t {
    return character == kTab ? static_cast<std::uint32_t>(kTabCells.size()) : 1;
}

/// Appends the character at `offset` as a terminal may show it, a tab as the cells it takes.
void appendCleaned(std::string& output, std::string_view text, ByteOffset offset,
                   const DecodedCharacter& decoded) {
    if (decoded.isValid && decoded.codePoint == kTab) {
        output += kTabCells;
    } else if (!decoded.isValid || isUnsafeForTerminal(decoded.codePoint)) {
        output += kReplacementCharacter;
    } else {
        output += text.substr(offset, decoded.length);
    }
}

/// One line of a snippet and where the carets go under it, in cells.
struct Snippet {
    std::string text;
    std::uint32_t caretOffset = 0;
    std::uint32_t caretLength = 1;
};

/// The line `line` of `source`, cleaned and cut around `range` when it is long.
[[nodiscard]] auto buildSnippet(std::string_view source, TextRange line, TextRange range) -> Snippet {
    const ByteOffset pointStart = std::clamp(range.start, line.start, line.end);
    const ByteOffset pointEnd = std::clamp(range.end, pointStart, line.end);

    std::uint32_t cellsBeforePoint = 0;
    std::uint32_t cellsOfPoint = 0;
    std::uint32_t cellsOfLine = 0;
    for (ByteOffset offset = line.start; offset < line.end;) {
        const DecodedCharacter decoded = decodeCharacter(source, offset);
        const std::uint32_t cells = decoded.isValid ? cellsOf(decoded.codePoint) : 1;
        if (offset < pointStart) {
            cellsBeforePoint += cells;
        } else if (offset < pointEnd) {
            cellsOfPoint += cells;
        }
        cellsOfLine += cells;
        offset += decoded.length;
    }

    const std::uint32_t firstCell = cellsOfLine <= kSnippetCells || cellsBeforePoint <= kSnippetCellsBefore
                                        ? 0
                                        : cellsBeforePoint - kSnippetCellsBefore;
    const std::uint32_t lastCell = std::min(cellsOfLine, firstCell + kSnippetCells);

    Snippet snippet;
    if (firstCell > 0) {
        snippet.text += kSnippetCut;
    }
    std::uint32_t cell = 0;
    for (ByteOffset offset = line.start; offset < line.end && cell < lastCell;) {
        const DecodedCharacter decoded = decodeCharacter(source, offset);
        if (cell >= firstCell) {
            appendCleaned(snippet.text, source, offset, decoded);
        }
        cell += decoded.isValid ? cellsOf(decoded.codePoint) : 1;
        offset += decoded.length;
    }
    if (lastCell < cellsOfLine) {
        snippet.text += kSnippetCut;
    }

    const auto cutWidth = firstCell > 0 ? static_cast<std::uint32_t>(kSnippetCut.size()) : 0;
    snippet.caretOffset = cellsBeforePoint - firstCell + cutWidth;
    const std::uint32_t lastCaretCell = std::min(cellsBeforePoint + cellsOfPoint, lastCell);
    snippet.caretLength = lastCaretCell > cellsBeforePoint ? lastCaretCell - cellsBeforePoint : 1;
    return snippet;
}

[[nodiscard]] auto decimalWidth(std::uint32_t number) noexcept -> std::size_t {
    std::size_t width = 1;
    for (std::uint32_t rest = number / kDecimalBase; rest > 0; rest /= kDecimalBase) {
        ++width;
    }
    return width;
}

} // namespace

void addDiagnostic(std::vector<Diagnostic>& diagnostics, Diagnostic diagnostic) {
    if (diagnostics.size() < kMaximumDiagnostics) {
        diagnostics.push_back(std::move(diagnostic));
    } else if (diagnostics.size() == kMaximumDiagnostics) {
        diagnostics.push_back(Diagnostic{.code = DiagnosticCode::TooManyDiagnostics,
                                         .range = diagnostic.range,
                                         .message = std::format("over {} errors here", kMaximumDiagnostics),
                                         .label = "the ones from here on are left out",
                                         .fix = "fix those above and run this again"});
    }
}

auto diagnosticCodeText(DiagnosticCode code) -> std::string {
    return std::format("R{:04}", std::to_underlying(code));
}

auto textForTerminal(std::string_view text) -> std::string {
    std::string output;
    output.reserve(text.size());
    for (ByteOffset offset = 0; offset < text.size();) {
        const DecodedCharacter decoded = decodeCharacter(text, offset);
        if (decoded.isValid && decoded.codePoint == kTab) {
            output += ' ';
        } else {
            appendCleaned(output, text, offset, decoded);
        }
        offset += decoded.length;
    }
    return output;
}

auto renderDiagnostic(const Diagnostic& diagnostic, std::string_view path, std::string_view source,
                      const LineMap& lines) -> std::string {
    const TextPosition position = lines.positionOf(diagnostic.range.start);
    const Snippet snippet = buildSnippet(source, lines.lineAround(diagnostic.range.start), diagnostic.range);
    const std::size_t gutter = decimalWidth(position.line);
    const std::string carets(snippet.caretLength, '^');

    std::string output =
        std::format("error[{}]: {}\n", diagnosticCodeText(diagnostic.code), diagnostic.message);
    output += std::format("{:>{}}--> {}:{}:{}\n", "", gutter, textForTerminal(path), position.line,
                          position.column);
    output += std::format("{:>{}} |\n", "", gutter);
    output += std::format("{:>{}} | {}\n", position.line, gutter, snippet.text);
    output += std::format("{:>{}} | {:>{}}{}", "", gutter, "", snippet.caretOffset, carets);
    if (!diagnostic.label.empty()) {
        output += std::format(" {}", diagnostic.label);
    }
    output += '\n';
    output += std::format("{:>{}} = fix: {}\n", "", gutter, diagnostic.fix);
    return output;
}

} // namespace rook::compiler
