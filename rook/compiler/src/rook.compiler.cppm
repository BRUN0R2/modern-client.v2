// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

/// rook.compiler — the compiler of the Rook language as a library (docs/14-rook-language.md, section 23).
///
/// Today its lexer, which reads any bytes into tokens that cover them all and diagnostics with a code, a
/// snippet and a fix. It depends only on the C++ standard library, and it treats its input as untrusted:
/// what is not Rook ends in a diagnostic, never in a crash.
export module rook.compiler;

export import :source;
export import :token;
export import :diagnostic;
export import :lexer;
