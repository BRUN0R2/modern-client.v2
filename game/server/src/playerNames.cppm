// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The names the server gives its players (docs/10-counter-strike-gameplay.md, section 2), as the
/// original engine gives them to the clients that connect or change their name (SV_CheckUserInfo,
/// SV_ExtractFromUserinfo, SV_CheckForDuplicateNames; ReHLDS, GPL, consulted: docs/20 section 3).
/// The original's two ways disagree on a name that starts with '#' (its connection leaves it, a change
/// of name blanks it): here both blank it. Its 31 characters at most come with the network's limits
/// (docs/08).
export module cs.server:playerNames;

import std;

export namespace cs {

/// The name of a player whose own is not one: empty, the console's, or with what the engine's text
/// cannot hold.
inline constexpr std::string_view kUnnamed = "unnamed";

/// The name the server gives a player asking for `requested`, the others in the game having `taken`:
/// its '%' and '&' blanked, the '#'s it starts with too, so that it resembles no player's number, and
/// the blanks about it gone; kUnnamed for none left, "console" in any case, or one holding "..", '"' or
/// '\\'; then, while another has it in any case, the first of "(1)name", "(2)name"... no other has.
[[nodiscard]] auto playerNameOf(std::string_view requested, std::span<const std::string_view> taken)
    -> std::string;

} // namespace cs
