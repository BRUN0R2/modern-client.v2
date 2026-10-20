// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.data;

import std;
import nx.core;

namespace nx::data::detail {

namespace {

[[nodiscard]] auto describeKind(NodeKind kind) noexcept -> std::string_view {
    switch (kind) {
    case NodeKind::Table:
        return "a table";
    case NodeKind::Array:
        return "an array";
    case NodeKind::String:
        return "text";
    case NodeKind::Integer:
        return "a whole number";
    case NodeKind::Float:
        return "a number with a fraction";
    case NodeKind::Boolean:
        return "a boolean";
    case NodeKind::Date:
        return "a date";
    case NodeKind::Time:
        return "a time";
    case NodeKind::DateTime:
        return "a date and time";
    case NodeKind::Count:
        break;
    }
    return "an unknown value";
}

/// "[0, 100]", "at least 0" or "at most 100".
[[nodiscard]] auto describeRange(const Range& range) -> std::string {
    const bool hasMinimum = std::isfinite(range.minimum);
    const bool hasMaximum = std::isfinite(range.maximum);
    if (hasMinimum && hasMaximum) {
        return std::format("in [{}, {}]", range.minimum, range.maximum);
    }
    if (hasMinimum) {
        return std::format("of at least {}", range.minimum);
    }
    return std::format("of at most {}", range.maximum);
}

} // namespace

auto Location::child(std::string_view key) const -> Location {
    return Location{.source = source,
                    .path = path.empty() ? std::string{key} : std::format("{}.{}", path, key)};
}

auto Location::element(std::size_t index) const -> Location {
    return Location{.source = source, .path = std::format("{}[{}]", path, index)};
}

auto failAt(DataError code, const Location& at, SourcePosition position, std::string_view message)
    -> std::unexpected<Failure<DataError>> {
    if (at.path.empty()) {
        return fail(code, std::format("{}:{}:{}: {}", at.source, position.line, position.column, message));
    }
    return fail(code,
                std::format("{}:{}:{}: {}: {}", at.source, position.line, position.column, at.path, message));
}

auto wrongType(NodeView node, const Location& at, std::string_view expected)
    -> std::unexpected<Failure<DataError>> {
    return failAt(DataError::WrongType, at, node.position(),
                  std::format("expected {}, got {}", expected, describeKind(node.kind())));
}

auto checkRange(double value, std::string_view text, const std::optional<Range>& range, const Location& at,
                SourcePosition position) -> Result<void, DataError> {
    if (range && (value < range->minimum || value > range->maximum)) {
        return failAt(DataError::OutOfRange, at, position,
                      std::format("expected a number {}, got {}", describeRange(*range), text));
    }
    return {};
}

auto checkKeys(TableView table, const Location& at, std::span<const std::string_view> names)
    -> Result<void, DataError> {
    for (const TableEntry& entry : table.entries()) {
        if (std::ranges::contains(names, entry.key)) {
            continue;
        }
        std::string expected;
        for (const std::string_view name : names) {
            expected += std::format("{}\"{}\"", expected.empty() ? "" : ", ", name);
        }
        return failAt(DataError::UnknownKey, at, entry.position,
                      names.empty()
                          ? std::format("unknown key \"{}\"; the table takes no keys", entry.key)
                          : std::format("unknown key \"{}\"; expected one of {}", entry.key, expected));
    }
    return {};
}

auto dataSpelling(std::string_view enumeratorName) -> std::string {
    std::string spelling{enumeratorName};
    if (!spelling.empty() && spelling.front() >= 'A' && spelling.front() <= 'Z') {
        spelling.front() = static_cast<char>(spelling.front() - 'A' + 'a');
    }
    return spelling;
}

} // namespace nx::data::detail
