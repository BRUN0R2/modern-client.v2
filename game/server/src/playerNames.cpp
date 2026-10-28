// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.server;

import std;
import nx.core;

namespace cs {

namespace {

/// What a name's characters the engine reads as its own are blanked into.
constexpr char kBlank = ' ';
/// The characters a name may not hold, blanked (the engine's format and text escapes).
constexpr std::string_view kBlanked = "%&";
/// The character a name may not start with, blanked, as a player's number starts with it ("#12").
constexpr char kNumberSign = '#';
/// The name of the server's own console, which no player may take.
constexpr std::string_view kConsoleName = "console";
/// What a name may not hold at all: a path's way up and the userinfo's quote and separator.
constexpr std::array<std::string_view, 3> kForbidden{"..", "\"", "\\"};

[[nodiscard]] auto isBlank(char character) noexcept -> bool {
    return std::isspace(static_cast<unsigned char>(character)) != 0;
}

/// `requested` blanked and trimmed, or kUnnamed when that leaves no name the engine takes.
[[nodiscard]] auto cleanNameOf(std::string_view requested) -> std::string {
    std::string name{requested};
    std::ranges::replace_if(name, [](char character) { return kBlanked.contains(character); }, kBlank);
    for (char& character : name) {
        if (character != kNumberSign) {
            break;
        }
        character = kBlank;
    }
    const auto first = std::ranges::find_if_not(name, isBlank);
    const auto last = std::ranges::find_if_not(name | std::views::reverse, isBlank).base();
    name = first < last ? std::string{first, last} : std::string{};
    const bool isForbidden = std::ranges::any_of(
        kForbidden, [&name](std::string_view forbidden) { return name.contains(forbidden); });
    if (name.empty() || nx::equalsIgnoringCase(name, kConsoleName) || isForbidden) {
        return std::string{kUnnamed};
    }
    return name;
}

} // namespace

auto playerNameOf(std::string_view requested, std::span<const std::string_view> taken) -> std::string {
    const std::string clean = cleanNameOf(requested);
    // Another's in any case (Q_stricmp).
    const auto isTaken = [taken](std::string_view name) {
        return std::ranges::any_of(
            taken, [name](std::string_view other) { return nx::equalsIgnoringCase(name, other); });
    };
    std::string name = clean;
    for (std::uint32_t duplicate = 1; isTaken(name); ++duplicate) {
        name = std::format("({}){}", duplicate, clean);
    }
    return name;
}

} // namespace cs
