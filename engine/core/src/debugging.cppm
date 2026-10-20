// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Debugger and call-stack support for diagnostics: the facade over C++26 <debugging> (P2546) and
/// C++23 <stacktrace> (docs/02-coding-standards.md, section 1.1). Feature-test macros pick the
/// standard library when it has the feature and the platform's own primitives otherwise.
export module nx.core:debugging;

import std;

export namespace nx {

/// Whether a debugger is attached to the process right now.
[[nodiscard]] auto isDebuggerPresent() noexcept -> bool;

/// Stops in the attached debugger, which may resume execution; does nothing without a debugger.
void breakpointIfDebugging() noexcept;

/// The call stack of the caller, one indented frame per line, innermost first. Frames show
/// function names and source lines when the build has debug information. Toolchains without
/// <stacktrace> get one line saying that the stack trace is unavailable.
[[nodiscard]] auto currentStackTrace() -> std::string;

} // namespace nx
