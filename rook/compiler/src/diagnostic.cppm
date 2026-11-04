// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

export module rook.compiler:diagnostic;

import std;
import :source;

export namespace rook::compiler {

/// How many diagnostics the lexer keeps of one source: past these one says the
/// rest are left out, so that a source of nothing but errors does not fill the memory with their words.
inline constexpr std::size_t kMaximumDiagnostics = 1000;

/// The codes of syntax, R0001 to R0099 (docs/14-rook-language.md, section 23), by their number.
enum class DiagnosticCode : std::uint16_t {
    // The characters of a source.
    InvalidUtf8 = 1,
    UnexpectedCharacter = 2,
    PreprocessorDirective = 3,
    UnterminatedBlockComment = 4,
    TextDirectionCharacter = 5,
    UnterminatedString = 6,
    UnknownEscape = 7,
    InvalidUnicodeEscape = 8,
    CharacterLiteralLength = 9,
    UnterminatedCharacter = 10,
    MissingDigits = 11,
    InvalidDigit = 12,
    MisplacedDigitSeparator = 13,
    TooManyDiagnostics = 15,
};

/// Every code, for what must cover them all.
inline constexpr std::array kDiagnosticCodes{
    DiagnosticCode::InvalidUtf8,
    DiagnosticCode::UnexpectedCharacter,
    DiagnosticCode::PreprocessorDirective,
    DiagnosticCode::UnterminatedBlockComment,
    DiagnosticCode::TextDirectionCharacter,
    DiagnosticCode::UnterminatedString,
    DiagnosticCode::UnknownEscape,
    DiagnosticCode::InvalidUnicodeEscape,
    DiagnosticCode::CharacterLiteralLength,
    DiagnosticCode::UnterminatedCharacter,
    DiagnosticCode::MissingDigits,
    DiagnosticCode::InvalidDigit,
    DiagnosticCode::MisplacedDigitSeparator,
    DiagnosticCode::TooManyDiagnostics,
};

/// An error in a source. Every diagnostic carries its fix, and nothing of the source's own text goes
/// into `message`, `label` or `fix`: a source is untrusted, and only the snippet shows it, cleaned.
struct Diagnostic {
    DiagnosticCode code = DiagnosticCode::UnexpectedCharacter;
    TextRange range{};
    std::string message{};
    /// What the carets point at, under the snippet.
    std::string label{};
    std::string fix{};
};

/// The code as it is written, "R0030".
[[nodiscard]] auto diagnosticCodeText(DiagnosticCode code) -> std::string;

/// The diagnostic as a person reads it (docs/14-rook-language.md, section 23): its code and message,
/// where it is, the line with carets and the fix. `lines` must be the map of `source`.
[[nodiscard]] auto renderDiagnostic(const Diagnostic& diagnostic, std::string_view path,
                                    std::string_view source, const LineMap& lines) -> std::string;

/// `text` with what could reach out of a terminal replaced: control characters, invisible ones, those
/// that change the direction of text, and bytes that are not UTF-8.
[[nodiscard]] auto textForTerminal(std::string_view text) -> std::string;

} // namespace rook::compiler

namespace rook::compiler {

/// Adds `diagnostic` to those of one pass, up to kMaximumDiagnostics.
void addDiagnostic(std::vector<Diagnostic>& diagnostics, Diagnostic diagnostic);

} // namespace rook::compiler
