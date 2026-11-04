// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

// The parser on any bytes (docs/adr/0033-rook-compiler-and-types.md, section 1): malformed source always
// ends in a diagnostic, never in a crash, and the tree writes every byte back. What it must hold it
// checks itself, ending the process where it does not, which is what a fuzzer reports.

import std;
import rook.compiler;

#include "fuzzTarget.hpp"

namespace {

namespace compiler = rook::compiler;

/// How many diagnostics of one input to render: the first ones show the same as the rest, and an input
/// of nothing but errors would otherwise spend its time here.
constexpr std::size_t kRenderedDiagnostics = 16;

void require(bool condition) {
    if (!condition) {
        std::abort();
    }
}

/// The tree's tokens, in the order its nodes hold them, are the source.
void requireLossless(const compiler::SyntaxTree& tree, std::string_view source) {
    std::size_t offset = 0;
    std::vector<std::span<const compiler::SyntaxElement>> pending{tree.children(tree.root())};
    while (!pending.empty()) {
        if (pending.back().empty()) {
            pending.pop_back();
            continue;
        }
        const compiler::SyntaxElement element = pending.back().front();
        pending.back() = pending.back().subspan(1);
        if (!element.isToken()) {
            pending.push_back(tree.children(element.node()));
            continue;
        }
        const compiler::Token& token = tree.token(element.token());
        require(token.range.start == offset);
        require(token.range.end >= token.range.start && token.range.end <= source.size());
        offset = token.range.end;
    }
    require(offset == source.size());
}

void requireDiagnosticsInBounds(const compiler::ParseResult& result, std::string_view source) {
    const compiler::LineMap lines{result.tree.source()};
    std::size_t rendered = 0;
    for (const compiler::Diagnostic& diagnostic : result.diagnostics) {
        require(diagnostic.range.start <= diagnostic.range.end && diagnostic.range.end <= source.size());
        require(!diagnostic.message.empty() && !diagnostic.fix.empty());
        if (rendered < kRenderedDiagnostics) {
            rendered += 1;
            require(
                !compiler::renderDiagnostic(diagnostic, "fuzz.rook", result.tree.source(), lines).empty());
        }
    }
}

} // namespace

void rook::fuzzing::fuzz(std::span<const std::byte> input) {
    const std::string_view source{reinterpret_cast<const char*>(input.data()), input.size()};
    const compiler::ParseResult result = compiler::parse(source);
    requireLossless(result.tree, source);
    requireDiagnosticsInBounds(result, source);
    require(!compiler::printSyntaxTree(result.tree).empty());
}
