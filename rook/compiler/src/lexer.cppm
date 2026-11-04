// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

export module rook.compiler:lexer;

import std;
import :source;
import :token;
import :diagnostic;

export namespace rook::compiler {

/// A source read into tokens.
struct TokenizedSource {
    /// Every token in source order, trivia included, ending in one TokenKind::EndOfFile. Together they
    /// cover every byte of the source, with no gaps, which is what makes the tree lossless.
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

/// Reads `source`, at most kMaximumSourceBytes long, into tokens. It takes any bytes: what is not Rook
/// ends in a diagnostic.
[[nodiscard]] auto tokenize(std::string_view source) -> TokenizedSource;

} // namespace rook::compiler
