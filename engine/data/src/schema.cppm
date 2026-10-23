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
/// An infinite bound is no bound. A Range goes only on fields that hold numbers (also through a
/// quantity, a duration, an array or an optional), with minimum <= maximum; on whole numbers its
/// bounds are whole and within 2^53 of zero, so they compare exactly. A description that breaks this does
/// not compile.
struct Range {
    double minimum = -std::numeric_limits<double>::infinity();
    double maximum = std::numeric_limits<double>::infinity();

    auto operator==(const Range&) const noexcept -> bool = default;
};

namespace detail {

template <class Type>
inline constexpr bool kIsVector = false;

template <class Element, class Allocator>
inline constexpr bool kIsVector<std::vector<Element, Allocator>> = true;

template <class Type>
inline constexpr bool kIsFixedArray = false;

template <class Element, std::size_t Length>
inline constexpr bool kIsFixedArray<std::array<Element, Length>> = true;

/// An array of any length or of a fixed one: written alike, read with their own checks.
template <class Type>
inline constexpr bool kIsArray = kIsVector<Type> || kIsFixedArray<Type>;

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

/// A single value of a data file: a boolean, an integer, a float or a double, a quantity or a
/// duration (written as its number), text, or an enum with Count (written as the enumerator's
/// name with a lowercase first letter: `GoldsrcInstall` is "goldsrcInstall").
template <class Type>
concept DataScalar = std::same_as<Type, bool> || Integer<Type> || std::same_as<Type, float> ||
                     std::same_as<Type, double> || std::same_as<Type, std::string> || CountedEnum<Type> ||
                     detail::kIsQuantity<Type> || detail::kIsDuration<Type>;

/// A described struct that data files hold as a table.
template <class Type>
concept DataTable = meta::Described<Type> && std::default_initializable<Type> && std::movable<Type>;

namespace detail {

template <class Type>
[[nodiscard]] consteval auto isDataValue() noexcept -> bool {
    if constexpr (kIsArray<Type>) {
        return isDataValue<typename Type::value_type>();
    } else if constexpr (meta::Described<Type>) {
        return DataTable<Type>;
    } else {
        return DataScalar<Type>;
    }
}

} // namespace detail

/// What a key holds: a scalar, a table, or an array of them, of any length (std::vector) or of a
/// fixed one (std::array).
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
    /// Whether the value is text given alone, not read from a file (nx.data:fields): its failures
    /// name no line or column, which would be those of the document it was read as.
    bool isGivenText = false;

    [[nodiscard]] auto child(std::string_view key) const -> Location;
    [[nodiscard]] auto element(std::size_t index) const -> Location;
};

/// "<source>:<line>:<column>: <path>: <message>", the detail of every failure; "<source>: <path>:
/// <message>" for text given alone, or at a position of line 0, none.
[[nodiscard]] auto describeAt(const Location& at, SourcePosition position, std::string_view message)
    -> std::string;

/// `code` with the place and `message` as the detail.
[[nodiscard]] auto failAt(DataError code, const Location& at, SourcePosition position,
                          std::string_view message) -> std::unexpected<Failure<DataError>>;

/// As above, with the failure that caused it chained as the cause.
template <class CauseCode>
[[nodiscard]] auto failAt(DataError code, const Location& at, SourcePosition position,
                          std::string_view message, Failure<CauseCode> cause)
    -> std::unexpected<Failure<DataError>> {
    return fail(code, describeAt(at, position, message), std::move(cause));
}

/// WrongType: "expected <expected>, got <what the node is>".
[[nodiscard]] auto wrongType(NodeView node, const Location& at, std::string_view expected)
    -> std::unexpected<Failure<DataError>>;

/// `"a", "b", "c"`: how failures list what they expected.
template <std::ranges::input_range Items>
[[nodiscard]] auto quotedList(const Items& items) -> std::string {
    std::string list;
    for (const auto& item : items) {
        list += std::format("{}\"{}\"", list.empty() ? "" : ", ", item);
    }
    return list;
}

/// UnknownKey for the first key of `table`, in file order, that is not one of `names`.
[[nodiscard]] auto checkKeys(TableView table, const Location& at, std::span<const std::string_view> names)
    -> Result<void, DataError>;

/// OutOfRange when the whole number `value` is outside `range`, compared exactly.
[[nodiscard]] auto checkWholeRange(std::int64_t value, const std::optional<Range>& range, const Location& at,
                                   SourcePosition position) -> Result<void, DataError>;

[[nodiscard]] auto readBoolean(NodeView node, const Location& at) -> Result<bool, DataError>;
[[nodiscard]] auto readText(NodeView node, const Location& at) -> Result<std::string, DataError>;

/// A finite number, whole or not, of magnitude up to `largest` and within `range`.
[[nodiscard]] auto readNumber(NodeView node, const Location& at, double largest,
                              const std::optional<Range>& range) -> Result<double, DataError>;

/// How data files write an enumerator: its name with a lowercase first letter.
[[nodiscard]] auto dataSpelling(std::string_view enumeratorName) -> std::string;

/// Whether `text` is the data spelling of `enumeratorName`.
[[nodiscard]] auto isDataSpelling(std::string_view enumeratorName, std::string_view text) noexcept -> bool;

/// Every integer up to 2^53 in magnitude is exactly a double.
inline constexpr double kExactWholeLimit = 9'007'199'254'740'992.0;

/// Whether `bound` is infinite (no bound) or a whole number a double holds exactly.
[[nodiscard]] constexpr auto isWholeBound(double bound) noexcept -> bool {
    if (bound == std::numeric_limits<double>::infinity() ||
        bound == -std::numeric_limits<double>::infinity()) {
        return true;
    }
    return bound >= -kExactWholeLimit && bound <= kExactWholeLimit &&
           static_cast<double>(static_cast<std::int64_t>(bound)) == bound;
}

/// The number type a field holds: itself, or a quantity's or duration's representation, through
/// arrays and optionals.
template <class Value>
[[nodiscard]] consteval auto numberTypeOf() noexcept {
    if constexpr (kIsArray<Value> || kIsOptional<Value>) {
        return numberTypeOf<typename Value::value_type>();
    } else if constexpr (kIsQuantity<Value>) {
        return std::type_identity<typename Value::Representation>{};
    } else if constexpr (kIsDuration<Value>) {
        return std::type_identity<typename Value::rep>{};
    } else {
        return std::type_identity<Value>{};
    }
}

template <class Value>
using NumberType = typename decltype(numberTypeOf<Value>())::type;

/// The Range of a field, checked when its description compiles. constexpr, not consteval: MSVC
/// cannot materialize a std::optional returned by a consteval call inside a template.
template <class Field>
[[nodiscard]] constexpr auto rangeOf() noexcept -> std::optional<Range> {
    constexpr std::optional<Range> kRange = Field::template annotation<Range>();
    if constexpr (kRange.has_value()) {
        using Number = NumberType<typename Field::ValueType>;
        static_assert(Integer<Number> || std::floating_point<Number>,
                      "nx::data: a Range bounds a number; this field holds none");
        static_assert(kRange->minimum <= kRange->maximum,
                      "nx::data: a Range needs minimum <= maximum, neither NaN");
        static_assert(!Integer<Number> || (isWholeBound(kRange->minimum) && isWholeBound(kRange->maximum)),
                      "nx::data: the Range of a whole-number field needs whole bounds within 2^53 of zero");
    }
    return kRange;
}

template <DataTable Type>
[[nodiscard]] consteval auto fieldNames() noexcept {
    std::array<std::string_view, meta::fieldCount<Type>()> names{};
    std::size_t index = 0;
    meta::forEachField<Type>(
        [&names, &index]<class Field>(Field /*field*/) { names[index++] = Field::kName; });
    return names;
}

template <DataValue Value>
[[nodiscard]] auto readValue(NodeView node, const Location& at, const std::optional<Range>& range)
    -> Result<Value, DataError>;

template <DataTable Type>
[[nodiscard]] auto readTable(TableView table, const Location& at) -> Result<Type, DataError>;

template <Integer Value>
[[nodiscard]] auto readInteger(NodeView node, const Location& at, const std::optional<Range>& range)
    -> Result<Value, DataError> {
    const auto number = node.asInteger();
    if (!number) {
        return wrongType(node, at, "a whole number");
    }
    auto value = tryNarrow<Value>(*number);
    if (!value) {
        constexpr std::optional<Value> kLowest = std::numeric_limits<Value>::min();
        constexpr std::optional<Value> kHighest = std::numeric_limits<Value>::max();
        return failAt(
            DataError::OutOfRange, at, node.position(),
            std::format("expected a whole number {}, got {}", boundsText(kLowest, kHighest), *number),
            std::move(value.error()));
    }
    return checkWholeRange(*number, range, at, node.position()).transform([&value] { return *value; });
}

template <CountedEnum Enum>
[[nodiscard]] auto readEnum(NodeView node, const Location& at) -> Result<Enum, DataError> {
    static constexpr auto kValues = enumerators<Enum>();
    const auto text = node.asString();
    if (!text) {
        return wrongType(node, at, "the name of a value");
    }
    const auto found =
        std::ranges::find_if(kValues, [&text](Enum value) { return isDataSpelling(enumName(value), *text); });
    if (found != kValues.end()) {
        return *found;
    }
    const auto spellings =
        kValues | std::views::transform([](Enum value) { return dataSpelling(enumName(value)); });
    return failAt(DataError::UnknownName, at, node.position(),
                  std::format("expected one of {}, got \"{}\"", quotedList(spellings), *text));
}

template <class Value>
    requires kIsVector<Value>
[[nodiscard]] auto readArray(NodeView node, const Location& at, const std::optional<Range>& range)
    -> Result<Value, DataError> {
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
}

template <class Value>
    requires kIsFixedArray<Value>
[[nodiscard]] auto readFixedArray(NodeView node, const Location& at, const std::optional<Range>& range)
    -> Result<Value, DataError> {
    const auto array = node.asArray();
    if (!array) {
        return wrongType(node, at, "an array");
    }
    const std::vector<NodeView> elements = array->elements();
    if (elements.size() != std::tuple_size_v<Value>) {
        return failAt(DataError::WrongLength, at, node.position(),
                      std::format("expected {} elements, got {}", std::tuple_size_v<Value>, elements.size()));
    }
    Value values{};
    for (std::size_t index = 0; index < elements.size(); ++index) {
        auto value = readValue<typename Value::value_type>(elements[index], at.element(index), range);
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        values[index] = std::move(*value);
    }
    return values;
}

template <DataTable Type>
[[nodiscard]] auto readNestedTable(NodeView node, const Location& at) -> Result<Type, DataError> {
    const auto table = node.asTable();
    if (!table) {
        return wrongType(node, at, "a table");
    }
    return readTable<Type>(*table, at);
}

template <DataValue Value>
[[nodiscard]] auto readValue(NodeView node, const Location& at, const std::optional<Range>& range)
    -> Result<Value, DataError> {
    if constexpr (std::same_as<Value, bool>) {
        return readBoolean(node, at);
    } else if constexpr (Integer<Value>) {
        return readInteger<Value>(node, at, range);
    } else if constexpr (std::floating_point<Value>) {
        return readNumber(node, at, static_cast<double>(std::numeric_limits<Value>::max()), range)
            .transform([](double number) { return static_cast<Value>(number); });
    } else if constexpr (std::same_as<Value, std::string>) {
        return readText(node, at);
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
        return readArray<Value>(node, at, range);
    } else if constexpr (kIsFixedArray<Value>) {
        return readFixedArray<Value>(node, at, range);
    } else {
        return readNestedTable<Value>(node, at);
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
        auto read = readField<FieldValue>(table, at, Field::kName, rangeOf<Field>());
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

/// The enumerator of `Enum` that `text` names as data files write it ("counterTerrorists"); none.
template <CountedEnum Enum>
[[nodiscard]] auto enumeratorNamed(std::string_view text) -> std::optional<Enum> {
    static constexpr auto kValues = enumerators<Enum>();
    const auto found = std::ranges::find_if(
        kValues, [text](Enum value) { return detail::isDataSpelling(enumName(value), text); });
    return found == kValues.end() ? std::nullopt : std::optional{*found};
}

/// `value` as data files write it: its enumerator's name with a lowercase first letter.
template <CountedEnum Enum>
[[nodiscard]] auto dataNameOf(Enum value) -> std::string {
    return detail::dataSpelling(enumName(value));
}

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
