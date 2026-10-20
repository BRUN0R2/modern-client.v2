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

/// A finite bound as a value; an infinite one is no bound.
[[nodiscard]] auto finiteBound(double bound) noexcept -> std::optional<double> {
    return std::isfinite(bound) ? std::optional{bound} : std::nullopt;
}

/// A bound of a whole-number field, exact by the check rangeOf() makes when the description
/// compiles.
[[nodiscard]] auto wholeBound(double bound) noexcept -> std::optional<std::int64_t> {
    return finiteBound(bound).transform([](double finite) { return static_cast<std::int64_t>(finite); });
}

} // namespace

auto Location::child(std::string_view key) const -> Location {
    return Location{.source = source,
                    .path = path.empty() ? std::string{key} : std::format("{}.{}", path, key)};
}

auto Location::element(std::size_t index) const -> Location {
    return Location{.source = source, .path = std::format("{}[{}]", path, index)};
}

auto describeAt(const Location& at, SourcePosition position, std::string_view message) -> std::string {
    if (at.path.empty()) {
        return std::format("{}:{}:{}: {}", at.source, position.line, position.column, message);
    }
    return std::format("{}:{}:{}: {}: {}", at.source, position.line, position.column, at.path, message);
}

auto failAt(DataError code, const Location& at, SourcePosition position, std::string_view message)
    -> std::unexpected<Failure<DataError>> {
    return fail(code, describeAt(at, position, message));
}

auto wrongType(NodeView node, const Location& at, std::string_view expected)
    -> std::unexpected<Failure<DataError>> {
    return failAt(DataError::WrongType, at, node.position(),
                  std::format("expected {}, got {}", expected, describeKind(node.kind())));
}

auto checkKeys(TableView table, const Location& at, std::span<const std::string_view> names)
    -> Result<void, DataError> {
    for (const TableEntry& entry : table.entries()) {
        if (std::ranges::contains(names, entry.key)) {
            continue;
        }
        return failAt(DataError::UnknownKey, at, entry.position,
                      names.empty() ? std::format("unknown key \"{}\"; the table takes no keys", entry.key)
                                    : std::format("unknown key \"{}\"; expected one of {}", entry.key,
                                                  quotedList(names)));
    }
    return {};
}

auto checkWholeRange(std::int64_t value, const std::optional<Range>& range, const Location& at,
                     SourcePosition position) -> Result<void, DataError> {
    if (!range) {
        return {};
    }
    const auto minimum = wholeBound(range->minimum);
    const auto maximum = wholeBound(range->maximum);
    if ((minimum && value < *minimum) || (maximum && value > *maximum)) {
        return failAt(DataError::OutOfRange, at, position,
                      std::format("expected a whole number {}, got {}", boundsText(minimum, maximum), value));
    }
    return {};
}

auto readBoolean(NodeView node, const Location& at) -> Result<bool, DataError> {
    if (const auto value = node.asBool()) {
        return *value;
    }
    return wrongType(node, at, "a boolean");
}

auto readText(NodeView node, const Location& at) -> Result<std::string, DataError> {
    if (const auto text = node.asString()) {
        return std::string{*text};
    }
    return wrongType(node, at, "text");
}

auto readNumber(NodeView node, const Location& at, double largest, const std::optional<Range>& range)
    -> Result<double, DataError> {
    std::optional<double> number = node.asFloat();
    if (const auto whole = node.asInteger(); !number && whole) {
        number = static_cast<double>(*whole); // a whole number is a valid number of a floating field
    }
    if (!number) {
        return wrongType(node, at, "a number");
    }
    if (!std::isfinite(*number) || std::abs(*number) > largest) {
        return failAt(DataError::OutOfRange, at, node.position(),
                      std::format("{} is not a finite number of the field's precision", *number));
    }
    if (range && (*number < range->minimum || *number > range->maximum)) {
        return failAt(DataError::OutOfRange, at, node.position(),
                      std::format("expected a number {}, got {}",
                                  boundsText(finiteBound(range->minimum), finiteBound(range->maximum)),
                                  *number));
    }
    return *number;
}

auto dataSpelling(std::string_view enumeratorName) -> std::string {
    std::string spelling{enumeratorName};
    if (!spelling.empty()) {
        spelling.front() = toLowerAscii(spelling.front());
    }
    return spelling;
}

auto isDataSpelling(std::string_view enumeratorName, std::string_view text) noexcept -> bool {
    return !text.empty() && text.size() == enumeratorName.size() &&
           text.front() == toLowerAscii(enumeratorName.front()) && text.substr(1) == enumeratorName.substr(1);
}

} // namespace nx::data::detail
