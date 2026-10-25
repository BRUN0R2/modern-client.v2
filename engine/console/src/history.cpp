// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.console;

import std;
import nx.core;

namespace nx::console {

CommandHistory::CommandHistory(std::size_t capacity) noexcept : m_capacity{capacity} {
    expects(capacity > 0, "CommandHistory: room for a line at least");
    if (m_capacity == 0) {
        m_capacity = 1; // the handler let execution continue ("observe")
    }
}

void CommandHistory::add(std::string_view line) {
    m_walk.reset();
    if (trimmed(line).empty() || (!m_lines.empty() && m_lines.back() == line)) {
        return;
    }
    if (m_lines.size() == m_capacity) {
        m_lines.erase(m_lines.begin());
    }
    m_lines.emplace_back(line);
}

auto CommandHistory::older() noexcept -> std::optional<std::string_view> {
    if (m_lines.empty()) {
        return std::nullopt;
    }
    m_walk = m_walk ? (*m_walk == 0 ? std::size_t{0} : *m_walk - 1) : m_lines.size() - 1;
    return m_lines[*m_walk];
}

auto CommandHistory::newer() noexcept -> std::optional<std::string_view> {
    if (!m_walk || *m_walk + 1 == m_lines.size()) {
        m_walk.reset();
        return std::nullopt;
    }
    return m_lines[++*m_walk];
}

} // namespace nx::console
