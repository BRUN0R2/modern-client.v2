// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The fields of a described value by their key paths (docs/01-architecture.md, section 8): the
/// keys a data file gives a value, from its root, joined by dots ("firing.cycle"). A field is set
/// from text as a data file's value is read, with its type and Range, and written back as a data
/// file writes it: what a console or a plugin changes of a value made from data goes by the names
/// its file gives it, one schema for both.
export module nx.data:fields;

import std;
import nx.core;
import nx.meta;
import :document;
import :schema;

export namespace nx::data {

/// A field of a value, by its key path, and its value as a data file writes it.
struct FieldText {
    std::string keyPath;
    std::string value;

    auto operator==(const FieldText&) const -> bool = default;
};

namespace detail {

/// `text` as a TOML string: quoted, its quotes, backslashes and control characters escaped.
[[nodiscard]] auto quotedText(std::string_view text) -> std::string;

/// The first key of `keyPath`, and what follows its dot (empty for the last key).
[[nodiscard]] auto splitKeyPath(std::string_view keyPath) noexcept
    -> std::pair<std::string_view, std::string_view>;

/// The root of `document`, which must hold the key `value` alone: what `text` set. Errors:
/// UnknownKey (more than the value: a line break in `text`).
[[nodiscard]] auto writtenValue(const Document& document, const Location& at) -> Result<NodeView, DataError>;

/// What a field holds: its value, or an optional's.
template <class Field>
struct Held {
    using Type = Field;
};

template <class Value>
struct Held<std::optional<Value>> {
    using Type = Value;
};

template <class Field>
using HeldValue = typename Held<Field>::Type;

template <class Value>
inline constexpr bool kIsWrittenAsText = std::same_as<Value, std::string> || CountedEnum<Value>;

/// `text` read as a value of `Value`: text and an enumerator's name as they are, unless quoted;
/// anything else, and quoted text, as TOML.
template <DataValue Value>
[[nodiscard]] auto readWritten(std::string_view text, const Location& at, const std::optional<Range>& range)
    -> Result<Value, DataError> {
    const bool isVerbatim = kIsWrittenAsText<Value> && !text.starts_with('"') && !text.starts_with('\'');
    const std::string source = std::format("value = {}", isVerbatim ? quotedText(text) : std::string{text});
    return Document::parse(source, at.source).and_then([&](const Document& document) {
        return writtenValue(document, at).and_then([&](NodeView node) {
            return readValue<Value>(node, at, range);
        });
    });
}

template <DataValue Value>
void writeValue(std::string& out, const Value& value);

template <DataTable Type>
void writeTable(std::string& out, const Type& table) {
    std::string_view separator = "{ ";
    meta::forEachField<Type>([&]<class Field>(Field /*field*/) {
        using FieldValue = typename Field::ValueType;
        const FieldValue& field = Field::get(table);
        if constexpr (kIsOptional<FieldValue>) {
            if (!field) {
                return;
            }
        }
        out += separator;
        out += Field::kName;
        out += " = ";
        if constexpr (kIsOptional<FieldValue>) {
            writeValue(out, *field);
        } else {
            writeValue(out, field);
        }
        separator = ", ";
    });
    out += separator == "{ " ? "{}" : " }";
}

template <DataValue Value>
void writeValue(std::string& out, const Value& value) {
    if constexpr (std::same_as<Value, bool>) {
        out += value ? "true" : "false";
    } else if constexpr (Integer<Value> || std::floating_point<Value>) {
        std::format_to(std::back_inserter(out), "{}", value);
    } else if constexpr (std::same_as<Value, std::string>) {
        out += quotedText(value);
    } else if constexpr (CountedEnum<Value>) {
        out += quotedText(dataSpelling(enumName(value)));
    } else if constexpr (kIsQuantity<Value>) {
        writeValue(out, value.value());
    } else if constexpr (kIsDuration<Value>) {
        writeValue(out, value.count());
    } else if constexpr (kIsArray<Value>) {
        out += '[';
        std::string_view separator;
        for (const auto& element : value) {
            out += separator;
            writeValue(out, element);
            separator = ", ";
        }
        out += ']';
    } else {
        writeTable(out, value);
    }
}

template <DataValue Value>
[[nodiscard]] auto written(const Value& value) -> std::string {
    std::string out;
    writeValue(out, value);
    return out;
}

/// UnknownKey for `key`, of the table at `at`, which has no such field.
[[nodiscard]] auto unknownField(const Location& at, std::string_view key)
    -> std::unexpected<Failure<DataError>>;

/// UnknownKey for `key`, of the table at `at`, a field with no fields: `rest` cannot follow it.
[[nodiscard]] auto notATable(const Location& at, std::string_view key) -> std::unexpected<Failure<DataError>>;

template <DataTable Type>
[[nodiscard]] auto assignAt(Type& object, std::string_view keyPath, std::string_view text, const Location& at)
    -> Result<void, DataError> {
    const auto [key, rest] = splitKeyPath(keyPath);
    std::optional<Result<void, DataError>> result;
    meta::forEachField<Type>([&]<class Field>(Field /*field*/) {
        if (result || Field::kName != key) {
            return;
        }
        using FieldValue = typename Field::ValueType;
        using Value = HeldValue<FieldValue>;
        FieldValue& field = Field::get(object);
        const Location here = at.child(key);
        if (rest.empty()) {
            result = readWritten<Value>(text, here, rangeOf<Field>()).transform([&field](Value&& value) {
                field = std::move(value);
            });
        } else if constexpr (DataTable<Value>) {
            // An absent table starts from its defaults, and is kept only when the rest is set.
            Value table = [&field] {
                if constexpr (kIsOptional<FieldValue>) {
                    return field.value_or(Value{});
                } else {
                    return field;
                }
            }();
            result =
                assignAt(table, rest, text, here).transform([&field, &table] { field = std::move(table); });
        } else {
            result = Result<void, DataError>{notATable(at, key)};
        }
    });
    if (!result) {
        return unknownField(at, key);
    }
    return std::move(*result);
}

template <DataTable Type>
[[nodiscard]] auto formatAt(const Type& object, std::string_view keyPath, const Location& at)
    -> Result<std::optional<std::string>, DataError> {
    const auto [key, rest] = splitKeyPath(keyPath);
    using Formatted = Result<std::optional<std::string>, DataError>;
    std::optional<Formatted> result;
    meta::forEachField<Type>([&]<class Field>(Field /*field*/) {
        if (result || Field::kName != key) {
            return;
        }
        using FieldValue = typename Field::ValueType;
        using Value = HeldValue<FieldValue>;
        const FieldValue& field = Field::get(object);
        const Value* const value = [&field]() -> const Value* {
            if constexpr (kIsOptional<FieldValue>) {
                return field ? &*field : nullptr;
            } else {
                return &field;
            }
        }();
        if (value == nullptr) {
            result = Formatted{std::nullopt};
        } else if (rest.empty()) {
            result = Formatted{written(*value)};
        } else if constexpr (DataTable<Value>) {
            result = formatAt(*value, rest, at.child(key));
        } else {
            result = Formatted{notATable(at, key)};
        }
    });
    if (!result) {
        return unknownField(at, key);
    }
    return std::move(*result);
}

template <DataTable Type>
void listFields(const Type& object, const Location& at, std::vector<FieldText>& fields) {
    meta::forEachField<Type>([&]<class Field>(Field /*field*/) {
        using FieldValue = typename Field::ValueType;
        using Value = HeldValue<FieldValue>;
        const FieldValue& field = Field::get(object);
        if constexpr (kIsOptional<FieldValue>) {
            if (!field) {
                return;
            }
        }
        const Value& value = [&field]() -> const Value& {
            if constexpr (kIsOptional<FieldValue>) {
                return *field;
            } else {
                return field;
            }
        }();
        const Location here = at.child(Field::kName);
        if constexpr (DataTable<Value>) {
            listFields(value, here, fields);
        } else {
            fields.push_back(FieldText{.keyPath = here.path, .value = written(value)});
        }
    });
}

} // namespace detail

/// Sets the field of `object` at `keyPath` to `text`, read as a data file reads that key's value:
/// text and an enumerator's name as they are ("Drakar III"), or quoted as TOML writes them;
/// anything else as TOML ("0.08", "[1, 2]", "{ ammo = \"9mm\", clipSize = 30 }"), with the
/// field's type and Range. What formatField and listFields write sets the same value back. A table along the
/// path that is absent (an std::optional) starts from its defaults. `sourceName` ("console") names where the
/// text came from in failures. On a failure `object` is as it was. Errors: UnknownKey (a key of the path that
/// is no field, or one past a field that is no table; text past the value), Syntax, WrongType, OutOfRange,
/// UnknownName, MissingKey (a table written whole without a required key).
template <DataTable Type>
[[nodiscard]] auto assignField(Type& object, std::string_view keyPath, std::string_view text,
                               std::string_view sourceName) -> Result<void, DataError> {
    return detail::assignAt(object, keyPath, text,
                            detail::Location{.source = sourceName, .path = {}, .isGivenText = true});
}

/// The field of `object` at `keyPath` as a data file writes it: none when it, or a table along the
/// path, is absent. Errors: UnknownKey (a key of the path that is no field, or one past a field
/// that is no table).
template <DataTable Type>
[[nodiscard]] auto formatField(const Type& object, std::string_view keyPath, std::string_view sourceName)
    -> Result<std::optional<std::string>, DataError> {
    return detail::formatAt(object, keyPath,
                            detail::Location{.source = sourceName, .path = {}, .isGivenText = true});
}

/// Every field of `object` that holds a value, by its key path, in declaration order: the fields
/// of its tables in their place, the absent ones left out, arrays whole.
template <DataTable Type>
[[nodiscard]] auto listFields(const Type& object) -> std::vector<FieldText> {
    std::vector<FieldText> fields;
    detail::listFields(object, detail::Location{.source = {}, .path = {}, .isGivenText = true}, fields);
    return fields;
}

} // namespace nx::data
