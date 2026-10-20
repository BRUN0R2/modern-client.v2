// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// TOML schemas derived from described structs (docs/01-architecture.md, section 8): the keys of a
/// table are the fields of its struct, and a value must have the field's type and range.
export module nx.data:schema;

import std;
import nx.core;
import nx.meta;
import :document;

export namespace nx::data {

/// Inclusive bounds of a numeric field, as an annotation of its description:
/// `nx::meta::field<&Weapon::damage>("damage", nx::data::Range{.minimum = 0.0, .maximum = 500.0})`.
struct Range {
    double minimum = -std::numeric_limits<double>::infinity();
    double maximum = std::numeric_limits<double>::infinity();

    auto operator==(const Range&) const -> bool = default;
};

namespace detail {

template <class Type>
inline constexpr bool kIsVector = false;

template <class Element, class Allocator>
inline constexpr bool kIsVector<std::vector<Element, Allocator>> = true;

template <class Type>
inline constexpr bool kIsOptional = false;

template <class Value>
inline constexpr bool kIsOptional<std::optional<Value>> = true;

template <class Type>
inline constexpr bool kIsQuantity = false;

template <class Representation, class Tag>
inline constexpr bool kIsQuantity<Quantity<Representation, Tag>> = true;

template <class Type>
inline constexpr bool kIsDuration = false;

template <class Representation, class Period>
inline constexpr bool kIsDuration<std::chrono::duration<Representation, Period>> = true;

} // namespace detail

/// A single value of a data file: a boolean, an integer, a floating-point number, a quantity or a
/// duration (written as its number), text, or an enum with Count (written as the enumerator's
/// name with a lowercase first letter: `GoldsrcInstall` is "goldsrcInstall").
template <class Type>
concept DataScalar = std::same_as<Type, bool> || Integer<Type> || std::floating_point<Type> ||
                     std::same_as<Type, std::string> || CountedEnum<Type> || detail::kIsQuantity<Type> ||
                     detail::kIsDuration<Type>;

/// A described struct that data files hold as a table.
template <class Type>
concept DataTable = meta::Described<Type> && std::default_initializable<Type> && std::movable<Type>;

namespace detail {

template <class Type>
[[nodiscard]] consteval auto isDataValue() noexcept -> bool {
    if constexpr (kIsVector<Type>) {
        return isDataValue<typename Type::value_type>();
    } else if constexpr (meta::Described<Type>) {
        return DataTable<Type>;
    } else {
        return DataScalar<Type>;
    }
}

} // namespace detail

/// What a key holds: a scalar, a table, or an array of them.
template <class Type>
concept DataValue = detail::isDataValue<Type>();

/// What a field may be: a value, or an optional value whose key may be absent. Fields of any other
/// type are required: a missing key is a failure, never a default.
template <class Type>
concept DataField = DataValue<Type> || (detail::kIsOptional<Type> && DataValue<typename Type::value_type>);

namespace detail {

/// Where a value is: its file and its key path from the root ("goldsrcInstall.path", "sources[2]").
struct Location {
    std::string_view source;
    std::string path;

    [[nodiscard]] auto child(std::string_view key) const -> Location;
    [[nodiscard]] auto element(std::size_t index) const -> Location;
};

/// `code` with "<source>:<line>:<column>: <path>: <message>" as the detail.
[[nodiscard]] auto failAt(DataError code, const Location& at, SourcePosition position,
                          std::string_view message) -> std::unexpected<Failure<DataError>>;

/// WrongType: "expected <expected>, got <what the node is>".
[[nodiscard]] auto wrongType(NodeView node, const Location& at, std::string_view expected)
    -> std::unexpected<Failure<DataError>>;

/// OutOfRange when `value` (shown as `text`) is outside `range`.
[[nodiscard]] auto checkRange(double value, std::string_view text, const std::optional<Range>& range,
                              const Location& at, SourcePosition position) -> Result<void, DataError>;

/// UnknownKey for the first key of `table`, in file order, that is not one of `names`.
[[nodiscard]] auto checkKeys(TableView table, const Location& at, std::span<const std::string_view> names)
    -> Result<void, DataError>;

/// How data files write an enumerator: its name with a lowercase first letter.
[[nodiscard]] auto dataSpelling(std::string_view enumeratorName) -> std::string;

template <DataTable Type>
[[nodiscard]] consteval auto fieldNames() noexcept {
    std::array<std::string_view, meta::fieldCount<Type>()> names{};
    std::size_t index = 0;
    meta::forEachField<Type>(
        [&names, &index]<class Field>(Field /*field*/) { names[index++] = Field::kName; });
    return names;
}

template <DataTable Type>
[[nodiscard]] auto readTable(TableView table, const Location& at) -> Result<Type, DataError>;

template <CountedEnum Enum>
[[nodiscard]] auto readEnum(NodeView node, const Location& at) -> Result<Enum, DataError> {
    const auto text = node.asString();
    if (!text) {
        return wrongType(node, at, "the name of a value");
    }
    std::string choices;
    for (std::size_t index = 0; index < enumCount<Enum>(); ++index) {
        const auto value = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(index));
        const std::string_view name = enumName(value);
        if (name.empty()) {
            continue;
        }
        const std::string spelling = dataSpelling(name);
        if (spelling == *text) {
            return value;
        }
        choices += std::format("{}\"{}\"", choices.empty() ? "" : ", ", spelling);
    }
    return failAt(DataError::UnknownName, at, node.position(),
                  std::format("expected one of {}, got \"{}\"", choices, *text));
}

template <DataValue Value>
[[nodiscard]] auto readValue(NodeView node, const Location& at, const std::optional<Range>& range)
    -> Result<Value, DataError> {
    if constexpr (std::same_as<Value, bool>) {
        if (const auto value = node.asBool()) {
            return *value;
        }
        return wrongType(node, at, "a boolean");
    } else if constexpr (Integer<Value>) {
        const auto number = node.asInteger();
        if (!number) {
            return wrongType(node, at, "a whole number");
        }
        auto value = tryNarrow<Value>(*number);
        if (!value) {
            return failAt(DataError::OutOfRange, at, node.position(), value.error().detail);
        }
        return checkRange(static_cast<double>(*number), std::format("{}", *number), range, at,
                          node.position())
            .transform([&value] { return *value; });
    } else if constexpr (std::floating_point<Value>) {
        std::optional<double> number = node.asFloat();
        if (const auto whole = node.asInteger(); !number && whole) {
            number = static_cast<double>(*whole);
        }
        if (!number) {
            return wrongType(node, at, "a number");
        }
        if (!std::isfinite(*number) ||
            std::abs(*number) > static_cast<double>(std::numeric_limits<Value>::max())) {
            return failAt(DataError::OutOfRange, at, node.position(),
                          std::format("{} is not a finite number of the field's precision", *number));
        }
        return checkRange(*number, std::format("{}", *number), range, at, node.position())
            .transform([&number] { return static_cast<Value>(*number); });
    } else if constexpr (std::same_as<Value, std::string>) {
        if (const auto text = node.asString()) {
            return std::string{*text};
        }
        return wrongType(node, at, "text");
    } else if constexpr (CountedEnum<Value>) {
        return readEnum<Value>(node, at);
    } else if constexpr (kIsQuantity<Value>) {
        return readValue<typename Value::Representation>(node, at, range)
            .transform([](typename Value::Representation number) { return Value{number}; });
    } else if constexpr (kIsDuration<Value>) {
        return readValue<typename Value::rep>(node, at, range).transform([](typename Value::rep count) {
            return Value{count};
        });
    } else if constexpr (kIsVector<Value>) {
        const auto array = node.asArray();
        if (!array) {
            return wrongType(node, at, "an array");
        }
        Value values;
        std::size_t index = 0;
        for (const NodeView element : array->elements()) {
            auto value = readValue<typename Value::value_type>(element, at.element(index++), range);
            if (!value) {
                return std::unexpected(std::move(value.error()));
            }
            values.push_back(std::move(*value));
        }
        return values;
    } else {
        const auto table = node.asTable();
        if (!table) {
            return wrongType(node, at, "a table");
        }
        return readTable<Value>(*table, at);
    }
}

template <DataField Value>
[[nodiscard]] auto readField(TableView table, const Location& at, std::string_view key,
                             const std::optional<Range>& range) -> Result<Value, DataError> {
    const auto node = table.find(key);
    if constexpr (kIsOptional<Value>) {
        if (!node) {
            return Value{};
        }
        return readValue<typename Value::value_type>(*node, at.child(key), range)
            .transform([](typename Value::value_type&& value) { return Value{std::move(value)}; });
    } else {
        if (!node) {
            return failAt(DataError::MissingKey, at, table.position(),
                          std::format("missing key \"{}\"", key));
        }
        return readValue<Value>(*node, at.child(key), range);
    }
}

template <DataTable Type>
[[nodiscard]] auto readTable(TableView table, const Location& at) -> Result<Type, DataError> {
    static constexpr auto kNames = fieldNames<Type>();
    if (auto known = checkKeys(table, at, kNames); !known) {
        return std::unexpected(std::move(known.error()));
    }
    Type value{};
    std::optional<Failure<DataError>> failure;
    meta::forEachField<Type>([&]<class Field>(Field /*field*/) {
        using FieldValue = typename Field::ValueType;
        static_assert(DataField<FieldValue>, "nx::data: a field of a type that data files cannot hold");
        if (failure) {
            return;
        }
        auto read = readField<FieldValue>(table, at, Field::kName, Field::template annotation<Range>());
        if (!read) {
            failure = std::move(read.error());
            return;
        }
        Field::get(value) = std::move(*read);
    });
    if (failure) {
        return std::unexpected(std::move(*failure));
    }
    return value;
}

} // namespace detail

/// Reads `text`, the TOML file `sourceName` (named in failures), as a `Type`. Every key must be a
/// field of `Type` (nested tables are described structs too), every field that is not
/// std::optional must have its key, and every value must have its field's type and Range. The
/// first problem is the failure, as "<file>:<line>:<column>: <key path>: <problem>": a syntax
/// error, then unknown keys in file order, then the fields in declaration order.
template <DataTable Type>
[[nodiscard]] auto parseToml(std::string_view text, std::string_view sourceName) -> Result<Type, DataError> {
    return Document::parse(text, sourceName).and_then([sourceName](const Document& document) {
        return detail::readTable<Type>(document.root(), detail::Location{.source = sourceName, .path = {}});
    });
}

} // namespace nx::data
