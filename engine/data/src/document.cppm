// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// A parsed TOML document, seen through the engine's own views: no type of the TOML library
/// appears in this interface (docs/02-coding-standards.md, section 2).
export module nx.data:document;

import std;
import nx.core;

export namespace nx::data {

/// What can be wrong with a data file; every failure names the file, line, column and key path.
enum class DataError : std::uint8_t {
    Syntax,      ///< Not valid TOML; the detail has the parser's message.
    UnknownKey,  ///< A key the schema does not declare (a typo, or a key of another version).
    MissingKey,  ///< A required field without its key.
    WrongType,   ///< A value of another type than the field's.
    OutOfRange,  ///< A number outside the field's type or its Range, or not finite.
    UnknownName, ///< A string that names no enumerator of the field's enum.
};

/// What a TOML value is.
enum class NodeKind : std::uint8_t {
    Table,
    Array,
    String,
    Integer,
    Float,
    Boolean,
    Date,
    Time,
    DateTime,
    Count,
};

/// Where a node starts in its file, both counted from 1.
struct SourcePosition {
    std::uint32_t line = 0;
    std::uint32_t column = 0;

    auto operator<=>(const SourcePosition&) const noexcept = default;
};

namespace detail {

/// Builds views from the TOML library's nodes; defined with the implementation.
struct NodeAccess;

} // namespace detail

class TableView;
class ArrayView;

/// One value of a document. Valid while its Document lives. Each `as*` gives the value when the
/// node is of that kind, and nullopt otherwise: nothing converts between kinds.
class NodeView {
public:
    [[nodiscard]] auto kind() const noexcept -> NodeKind;
    /// Where the value starts in its file.
    [[nodiscard]] auto position() const noexcept -> SourcePosition;

    [[nodiscard]] auto asBool() const noexcept -> std::optional<bool>;
    [[nodiscard]] auto asInteger() const noexcept -> std::optional<std::int64_t>;
    /// Floats only; an integer is not a float here.
    [[nodiscard]] auto asFloat() const noexcept -> std::optional<double>;
    /// Valid while the Document lives.
    [[nodiscard]] auto asString() const noexcept -> std::optional<std::string_view>;
    [[nodiscard]] auto asTable() const noexcept -> std::optional<TableView>;
    [[nodiscard]] auto asArray() const noexcept -> std::optional<ArrayView>;

private:
    friend struct detail::NodeAccess;
    explicit NodeView(const void* node) noexcept : m_node{node} {}

    const void* m_node; // the library's node, never null
};

/// A key of a table, where it is written, and its value.
struct TableEntry {
    std::string_view key;
    SourcePosition position;
    NodeView value;
};

/// A table (`[name]`, an inline table, or the document root). Valid while its Document lives.
class TableView {
public:
    [[nodiscard]] auto position() const noexcept -> SourcePosition;
    [[nodiscard]] auto find(std::string_view key) const noexcept -> std::optional<NodeView>;

    /// Every key with its value, in the order they appear in the file.
    [[nodiscard]] auto entries() const -> std::vector<TableEntry>;

private:
    friend struct detail::NodeAccess;
    explicit TableView(const void* table) noexcept : m_table{table} {}

    const void* m_table; // the library's table, never null
};

/// An array. Valid while its Document lives.
class ArrayView {
public:
    [[nodiscard]] auto position() const noexcept -> SourcePosition;

    /// The elements, in order.
    [[nodiscard]] auto elements() const -> std::vector<NodeView>;

private:
    friend struct detail::NodeAccess;
    explicit ArrayView(const void* array) noexcept : m_array{array} {}

    const void* m_array; // the library's array, never null
};

/// A parsed TOML document. It owns the values its views look at.
class Document {
public:
    /// Parses `text`; `sourceName` ("content.toml") names the file in failures. Error: Syntax, with
    /// "<sourceName>:<line>:<column>: <what the parser expected>" as the detail.
    [[nodiscard]] static auto parse(std::string_view text, std::string_view sourceName)
        -> Result<Document, DataError>;

    Document(Document&& other) noexcept;
    auto operator=(Document&& other) noexcept -> Document&;
    Document(const Document&) = delete;
    auto operator=(const Document&) -> Document& = delete;
    ~Document();

    /// The top-level table. The document must not be a moved-from one.
    [[nodiscard]] auto root() const noexcept -> TableView;
    [[nodiscard]] auto sourceName() const noexcept -> std::string_view { return m_sourceName; }

private:
    struct Tree; // the library's root table

    Document(std::unique_ptr<Tree> tree, std::string sourceName) noexcept;

    std::unique_ptr<Tree> m_tree;
    std::string m_sourceName;
};

} // namespace nx::data
