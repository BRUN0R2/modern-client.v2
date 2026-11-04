// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: MIT

module rook.compiler;

import std;

namespace rook::compiler {

namespace {

static_assert(std::ranges::is_sorted(kReservedWords, {}, &ReservedWord::spelling));

} // namespace

auto reservedWordKind(std::string_view text) noexcept -> std::optional<TokenKind> {
    const auto found = std::ranges::lower_bound(kReservedWords, text, {}, &ReservedWord::spelling);
    if (found == kReservedWords.end() || found->spelling != text) {
        return std::nullopt;
    }
    return found->kind;
}

} // namespace rook::compiler
