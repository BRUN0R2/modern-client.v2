// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

static_assert(std::ranges::is_sorted(kReservedWords, {}, &ReservedWord::spelling));

/// How a diagnostic names each kind of token, in the order of the enum.
constexpr std::array<std::string_view, static_cast<std::size_t>(TokenKind::Count)> kDescriptions{
    "whitespace",
    "a comment",
    "a documentation comment",
    "a comment",
    "an invalid character",
    "an identifier",
    "a label",
    "a number",
    "a number",
    "a character",
    "a string",
    "a string",
    "the text of a string",
    "`{`",
    "a format specifier",
    "`}`",
    "the end of a string",
    "`abstract`",
    "`async`",
    "`await`",
    "`break`",
    "`class`",
    "`component`",
    "`const`",
    "`continue`",
    "`defer`",
    "`dimension`",
    "`else`",
    "`enum`",
    "`err`",
    "`error`",
    "`false`",
    "`fn`",
    "`for`",
    "`forward`",
    "`handle`",
    "`if`",
    "`impl`",
    "`import`",
    "`in`",
    "`inout`",
    "`interface`",
    "`is`",
    "`let`",
    "`loop`",
    "`match`",
    "`narrows`",
    "`newtype`",
    "`none`",
    "`ok`",
    "`open`",
    "`override`",
    "`persist`",
    "`plugin`",
    "`pub`",
    "`pure`",
    "`range`",
    "`readonly`",
    "`return`",
    "`self`",
    "`some`",
    "`struct`",
    "`suffix`",
    "`super`",
    "`true`",
    "`type`",
    "`var`",
    "`vars`",
    "`void`",
    "`while`",
    "`(`",
    "`)`",
    "`{`",
    "`}`",
    "`[`",
    "`]`",
    "`,`",
    "`;`",
    "`:`",
    "`.`",
    "`..`",
    "`..=`",
    "`?`",
    "`??`",
    "`=`",
    "`==`",
    "`!=`",
    "`<`",
    "`<=`",
    "`>`",
    "`>=`",
    "`+`",
    "`-`",
    "`*`",
    "`/`",
    "`%`",
    "`!`",
    "`&&`",
    "`&`",
    "`||`",
    "`|`",
    "`@`",
    "`->`",
    "`=>`",
    "`_`",
    "the end of the file",
};

} // namespace

auto reservedWordKind(std::string_view text) noexcept -> std::optional<TokenKind> {
    const auto found = std::ranges::lower_bound(kReservedWords, text, {}, &ReservedWord::spelling);
    if (found == kReservedWords.end() || found->spelling != text) {
        return std::nullopt;
    }
    return found->kind;
}

auto describeToken(TokenKind kind) noexcept -> std::string_view {
    return kDescriptions[static_cast<std::size_t>(kind)];
}

} // namespace rook::compiler
