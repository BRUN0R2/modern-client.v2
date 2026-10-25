// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.data;

import std;
import nx.core;

namespace nx::data::detail {

namespace {

/// The key `text` sets, in the document it is read from.
constexpr std::string_view kValueKey = "value";

} // namespace

auto quotedText(std::string_view text) -> std::string {
    std::string quoted{'"'};
    for (const char character : text) {
        if (character == '"' || character == '\\') {
            quoted += '\\';
            quoted += character;
        } else if (isAsciiControl(character)) { // written as an escape
            std::format_to(std::back_inserter(quoted), "\\u{:04X}", static_cast<unsigned char>(character));
        } else {
            quoted += character;
        }
    }
    quoted += '"';
    return quoted;
}

auto splitKeyPath(std::string_view keyPath) noexcept -> std::pair<std::string_view, std::string_view> {
    const std::size_t dot = keyPath.find('.');
    if (dot == std::string_view::npos) {
        return {keyPath, {}};
    }
    return {keyPath.substr(0, dot), keyPath.substr(dot + 1)};
}

auto writtenValue(const Document& document, const Location& at) -> Result<NodeView, DataError> {
    const TableView root = document.root();
    const std::vector<TableEntry> entries = root.entries();
    const std::optional<NodeView> value = root.find(kValueKey);
    if (entries.size() != 1 || !value) {
        return failAt(DataError::UnknownKey, at, root.position(), "more than one value");
    }
    return *value;
}

auto unknownField(const Location& at, std::string_view key) -> std::unexpected<Failure<DataError>> {
    return failAt(DataError::UnknownKey, at, SourcePosition{}, std::format("unknown key \"{}\"", key));
}

auto notATable(const Location& at, std::string_view key) -> std::unexpected<Failure<DataError>> {
    return failAt(DataError::UnknownKey, at, SourcePosition{}, std::format("\"{}\" holds no keys", key));
}

} // namespace nx::data::detail
