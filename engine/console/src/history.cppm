// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the player typed into the console (docs/12-ui-hud-and-console.md, section 5), as its input
/// line walks it with the up and down keys: the lines sent, the newest last.
export module nx.console:history;

import std;
import nx.core;

export namespace nx::console {

/// The lines a history keeps: older ones are forgotten.
inline constexpr std::size_t kHistoryLines = 128;

/// The lines sent from the console's input line, and where a walk through them is.
class CommandHistory {
public:
    /// A history of `capacity` lines, at least one.
    explicit CommandHistory(std::size_t capacity = kHistoryLines) noexcept;

    /// Keeps `line`, unless it is blank or the newest line again; the oldest line goes when there is no
    /// room. A walk starts again from the newest.
    void add(std::string_view line);

    /// The line before the one the walk is on, the newest when the walk has not started; the oldest
    /// again at the oldest. None without lines.
    [[nodiscard]] auto older() noexcept -> std::optional<std::string_view>;
    /// The line after the one the walk is on; none past the newest, back to the line being typed,
    /// where the walk ends.
    [[nodiscard]] auto newer() noexcept -> std::optional<std::string_view>;

    /// The lines kept, the newest last.
    [[nodiscard]] inline auto lines() const noexcept -> std::span<const std::string> { return m_lines; }

private:
    std::vector<std::string> m_lines;
    std::size_t m_capacity;
    std::optional<std::size_t> m_walk; ///< The line the walk is on.
};

} // namespace nx::console
