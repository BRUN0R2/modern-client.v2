// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// The TOML library stays in this unit (docs/02-coding-standards.md, section 2). It is built without
// exceptions, like the engine, so parsing returns a result instead of throwing.
#include <toml++/toml.hpp>

module nx.data;

import std;
import nx.core;

namespace nx::data {

struct Document::Tree {
    toml::table table;
};

namespace detail {

struct NodeAccess {
    [[nodiscard]] static auto node(const NodeView& view) noexcept -> const toml::node& {
        return *static_cast<const toml::node*>(view.m_node);
    }
    [[nodiscard]] static auto table(const TableView& view) noexcept -> const toml::table& {
        return *static_cast<const toml::table*>(view.m_table);
    }
    [[nodiscard]] static auto array(const ArrayView& view) noexcept -> const toml::array& {
        return *static_cast<const toml::array*>(view.m_array);
    }
    [[nodiscard]] static auto view(const toml::node& node) noexcept -> NodeView { return NodeView{&node}; }
    [[nodiscard]] static auto view(const toml::table& table) noexcept -> TableView {
        return TableView{&table};
    }
    [[nodiscard]] static auto view(const toml::array& array) noexcept -> ArrayView {
        return ArrayView{&array};
    }
};

} // namespace detail

namespace {

using detail::NodeAccess;

[[nodiscard]] auto positionOf(const toml::source_region& region) noexcept -> SourcePosition {
    return SourcePosition{.line = region.begin.line, .column = region.begin.column};
}

} // namespace

auto NodeView::kind() const noexcept -> NodeKind {
    switch (NodeAccess::node(*this).type()) {
    case toml::node_type::table:
        return NodeKind::Table;
    case toml::node_type::array:
        return NodeKind::Array;
    case toml::node_type::string:
        return NodeKind::String;
    case toml::node_type::integer:
        return NodeKind::Integer;
    case toml::node_type::floating_point:
        return NodeKind::Float;
    case toml::node_type::boolean:
        return NodeKind::Boolean;
    case toml::node_type::date:
        return NodeKind::Date;
    case toml::node_type::time:
        return NodeKind::Time;
    case toml::node_type::date_time:
        return NodeKind::DateTime;
    case toml::node_type::none:
        break;
    }
    invariant(false, "NodeView: a parsed document has no node without a type");
    return NodeKind::Count;
}

auto NodeView::position() const noexcept -> SourcePosition {
    return positionOf(NodeAccess::node(*this).source());
}

auto NodeView::asBool() const noexcept -> std::optional<bool> {
    return NodeAccess::node(*this).value_exact<bool>();
}

auto NodeView::asInteger() const noexcept -> std::optional<std::int64_t> {
    return NodeAccess::node(*this).value_exact<std::int64_t>();
}

auto NodeView::asFloat() const noexcept -> std::optional<double> {
    return NodeAccess::node(*this).value_exact<double>();
}

auto NodeView::asString() const noexcept -> std::optional<std::string_view> {
    const auto* const text = NodeAccess::node(*this).as_string();
    return text != nullptr ? std::optional<std::string_view>{text->get()} : std::nullopt;
}

auto NodeView::asTable() const noexcept -> std::optional<TableView> {
    const auto* const table = NodeAccess::node(*this).as_table();
    return table != nullptr ? std::optional{NodeAccess::view(*table)} : std::nullopt;
}

auto NodeView::asArray() const noexcept -> std::optional<ArrayView> {
    const auto* const array = NodeAccess::node(*this).as_array();
    return array != nullptr ? std::optional{NodeAccess::view(*array)} : std::nullopt;
}

auto TableView::position() const noexcept -> SourcePosition {
    return positionOf(NodeAccess::table(*this).source());
}

auto TableView::find(std::string_view key) const noexcept -> std::optional<NodeView> {
    const auto* const node = NodeAccess::table(*this).get(key);
    return node != nullptr ? std::optional{NodeAccess::view(*node)} : std::nullopt;
}

auto TableView::entries() const -> std::vector<TableEntry> {
    std::vector<TableEntry> entries;
    for (const auto& [key, node] : NodeAccess::table(*this)) {
        entries.push_back(TableEntry{
            .key = key.str(), .position = positionOf(key.source()), .value = NodeAccess::view(node)});
    }
    std::ranges::sort(entries, std::less{}, &TableEntry::position);
    return entries;
}

auto ArrayView::position() const noexcept -> SourcePosition {
    return positionOf(NodeAccess::array(*this).source());
}

auto ArrayView::elements() const -> std::vector<NodeView> {
    std::vector<NodeView> elements;
    for (const toml::node& element : NodeAccess::array(*this)) {
        elements.push_back(NodeAccess::view(element));
    }
    return elements;
}

auto Document::parse(std::string_view text, std::string_view sourceName) -> Result<Document, DataError> {
    toml::parse_result parsed = toml::parse(text, sourceName);
    if (!parsed) {
        const toml::parse_error& error = parsed.error();
        const SourcePosition position = positionOf(error.source());
        return fail(DataError::Syntax, std::format("{}:{}:{}: {}", sourceName, position.line, position.column,
                                                   error.description()));
    }
    auto tree = std::make_unique<Tree>(Tree{.table = std::move(parsed).table()});
    return Document{std::move(tree), std::string{sourceName}};
}

Document::Document(std::unique_ptr<Tree> tree, std::string sourceName) noexcept
    : m_tree{std::move(tree)}, m_sourceName{std::move(sourceName)} {}

Document::Document(Document&& other) noexcept = default;
auto Document::operator=(Document&& other) noexcept -> Document& = default;
Document::~Document() = default;

auto Document::root() const noexcept -> TableView {
    if (m_tree == nullptr) {
        expects(false, "Document::root: the document was moved from");
        static const toml::table kEmpty; // read as an empty document
        return NodeAccess::view(kEmpty);
    }
    return NodeAccess::view(m_tree->table);
}

} // namespace nx::data
