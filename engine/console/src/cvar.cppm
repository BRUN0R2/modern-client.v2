// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Typed cvars (docs/09-dedicated-server.md, section 6.3): every cvar has a type (bool, whole
/// number, number, text or enum) and numbers may have a range, but the console still reads and
/// writes them as text, with the classic syntax. Text that is not a value of the type is refused,
/// never turned into 0.
export module nx.console:cvar;

import std;
import nx.core;
import :text;

export namespace nx::console {

/// What a cvar allows and how it is treated (docs/12-ui-hud-and-console.md, section 5).
enum class CvarFlag : std::uint8_t {
    Archive,         ///< Saved to config.cfg (Console::archivedCvars).
    Notify,          ///< Changes are announced to the players; listed in A2S_RULES.
    Cheat,           ///< The console changes it only while cheats are allowed (sv_cheats 1).
    ServerProtected, ///< The value comes from the server: the console never changes it, only code.
    Count,
};

using CvarFlags = Flags<CvarFlag>;

enum class CvarError : std::uint8_t {
    InvalidName,       ///< Registration: a name the console language cannot invoke.
    NameTaken,         ///< Registration: a cvar, command or alias already has the name.
    InvalidDefinition, ///< Registration: an empty range, or a default value the cvar would refuse.
    InvalidValue,      ///< Not a value of the cvar's type.
    OutOfRange,        ///< A number outside the cvar's range.
};

enum class CvarKind : std::uint8_t {
    Bool,
    Integer,
    Float,
    String,
    Enum,
    Count,
};

template <class Type>
concept NumericCvarType = std::same_as<Type, std::int32_t> || std::same_as<Type, float>;

/// Types a cvar can hold. Enums need the Count sentinel; the console accepts an enumerator's
/// value or its name, and writes the value, as the original configs do.
template <class Type>
concept CvarType =
    std::same_as<Type, bool> || NumericCvarType<Type> || std::same_as<Type, std::string> || CountedEnum<Type>;

/// Definition of a cvar.
template <CvarType Type>
struct CvarDesc {
    ConsoleName name;
    Type defaultValue{};
    std::string_view description{};
    CvarFlags flags{};
};

/// Definition of a numeric cvar: also an optional range, inclusive at both ends.
template <NumericCvarType Type>
struct CvarDesc<Type> {
    ConsoleName name;
    Type defaultValue{};
    std::string_view description{};
    CvarFlags flags{};
    std::optional<Type> minimum{};
    std::optional<Type> maximum{};
};

namespace detail {

/// "0" or "1".
[[nodiscard]] auto parseBool(std::string_view text) noexcept -> std::optional<bool>;

/// A whole number in the range of std::int32_t; "10.0" and "1e3" count, "10.5" does not.
[[nodiscard]] auto parseInteger(std::string_view text) noexcept -> std::optional<std::int32_t>;

/// A finite number.
[[nodiscard]] auto parseFloat(std::string_view text) noexcept -> std::optional<float>;

/// Text the console language can write back: no double quote and no line break.
[[nodiscard]] auto isWritableText(std::string_view text) noexcept -> bool;

template <NumericCvarType Type>
[[nodiscard]] auto rangeExpectation(std::string_view noun, std::optional<Type> minimum,
                                    std::optional<Type> maximum) -> std::string {
    const std::string bounds = boundsText(minimum, maximum);
    return bounds.empty() ? std::string{noun} : std::format("{} {}", noun, bounds);
}

template <CountedEnum Enum>
[[nodiscard]] auto enumNumber(Enum value) -> std::int64_t {
    return static_cast<std::int64_t>(std::to_underlying(value));
}

/// A named enumerator before Count.
template <CountedEnum Enum>
[[nodiscard]] constexpr auto isEnumerator(Enum value) noexcept -> bool {
    return std::ranges::contains(enumerators<Enum>(), value);
}

/// The enumerator `text` names, by number or by name (ignoring case).
template <CountedEnum Enum>
[[nodiscard]] auto parseEnum(std::string_view text) -> std::optional<Enum> {
    static constexpr auto kValues = enumerators<Enum>();
    const auto number = parseInteger(text);
    const auto found = std::ranges::find_if(kValues, [number, name = trimmed(text)](Enum value) {
        return number ? enumNumber(value) == *number : equalsIgnoringCase(enumName(value), name);
    });
    return found != kValues.end() ? std::optional{*found} : std::nullopt;
}

} // namespace detail

/// A registered cvar as the console sees it: name, flags and the value as text. The Console owns
/// it; code keeps the typed Cvar<T> that registration returns.
class CvarBase {
public:
    CvarBase(const CvarBase&) = delete;
    CvarBase(CvarBase&&) = delete;
    auto operator=(const CvarBase&) -> CvarBase& = delete;
    auto operator=(CvarBase&&) -> CvarBase& = delete;
    virtual ~CvarBase() = default;

    [[nodiscard]] auto name() const noexcept -> std::string_view { return m_name; }
    [[nodiscard]] auto description() const noexcept -> std::string_view { return m_description; }
    [[nodiscard]] auto flags() const noexcept -> CvarFlags { return m_flags; }

    /// 0 after registration, plus one for every assignment that changed the value: consumers of
    /// Notify cvars compare revisions to see changes without callbacks.
    [[nodiscard]] auto revision() const noexcept -> std::uint64_t { return m_revision; }

    [[nodiscard]] virtual auto kind() const noexcept -> CvarKind = 0;

    /// The value as the console writes it; assign() reads it back.
    [[nodiscard]] virtual auto text() const -> std::string = 0;
    [[nodiscard]] virtual auto defaultText() const -> std::string = 0;

    /// What assign() accepts, for messages: "a whole number in [0, 60]".
    [[nodiscard]] virtual auto expectation() const -> std::string = 0;

    /// Parses `input` and assigns it. Flags are the console's policy and are not checked here.
    virtual auto assign(std::string_view input) -> Result<void, CvarError> = 0;

protected:
    CvarBase(std::string_view cvarName, std::string_view cvarDescription, CvarFlags cvarFlags)
        : m_name{cvarName}, m_description{cvarDescription}, m_flags{cvarFlags} {}

    void countChange() noexcept { ++m_revision; }

    /// `code` with the detail "<name>: expected <expectation>, got "<input>"".
    [[nodiscard]] auto refuse(CvarError code, std::string_view input,
                              std::source_location where = std::source_location::current()) const
        -> std::unexpected<Failure<CvarError>> {
        return fail(code, std::format("{}: expected {}, got \"{}\"", m_name, expectation(), input), where);
    }

private:
    std::string m_name;
    std::string m_description;
    CvarFlags m_flags;
    std::uint64_t m_revision = 0;
};

/// The cvar of type `Type`, owned by the Console.
template <CvarType Type>
class TypedCvar final : public CvarBase {
public:
    explicit TypedCvar(const CvarDesc<Type>& desc)
        : CvarBase{desc.name.text(), desc.description, desc.flags}, m_value{desc.defaultValue},
          m_default{desc.defaultValue} {
        if constexpr (NumericCvarType<Type>) {
            m_range = Range{.minimum = desc.minimum, .maximum = desc.maximum};
        }
    }

    /// InvalidDefinition when the range is empty or the cvar would refuse its own default value.
    [[nodiscard]] auto checkDefinition() const -> Result<void, CvarError> {
        if constexpr (NumericCvarType<Type>) {
            if (m_range.minimum && m_range.maximum && *m_range.minimum > *m_range.maximum) {
                return fail(CvarError::InvalidDefinition, std::format("{}: empty range", name()));
            }
        }
        if (const auto valid = validate(m_default); !valid) {
            return fail(CvarError::InvalidDefinition, std::format("{}: invalid default value", name()),
                        valid.error());
        }
        return {};
    }

    [[nodiscard]] auto value() const noexcept -> const Type& { return m_value; }

    /// Assigns `newValue` when the cvar accepts it (range, representable text).
    auto set(Type newValue) -> Result<void, CvarError> {
        if (auto valid = validate(newValue); !valid) {
            return valid;
        }
        if (newValue != m_value) {
            m_value = std::move(newValue);
            countChange();
        }
        return {};
    }

    [[nodiscard]] auto kind() const noexcept -> CvarKind override {
        if constexpr (std::same_as<Type, bool>) {
            return CvarKind::Bool;
        } else if constexpr (std::same_as<Type, std::int32_t>) {
            return CvarKind::Integer;
        } else if constexpr (std::same_as<Type, float>) {
            return CvarKind::Float;
        } else if constexpr (std::same_as<Type, std::string>) {
            return CvarKind::String;
        } else {
            return CvarKind::Enum;
        }
    }

    [[nodiscard]] auto text() const -> std::string override { return format(m_value); }
    [[nodiscard]] auto defaultText() const -> std::string override { return format(m_default); }

    [[nodiscard]] auto expectation() const -> std::string override {
        if constexpr (std::same_as<Type, bool>) {
            return "0 or 1";
        } else if constexpr (std::same_as<Type, std::int32_t>) {
            return detail::rangeExpectation("a whole number", m_range.minimum, m_range.maximum);
        } else if constexpr (std::same_as<Type, float>) {
            return detail::rangeExpectation("a number", m_range.minimum, m_range.maximum);
        } else if constexpr (std::same_as<Type, std::string>) {
            return "text without double quotes or line breaks";
        } else {
            std::string choices = "one of";
            std::string_view separator = " ";
            for (const Type enumerator : enumerators<Type>()) {
                choices +=
                    std::format("{}{} ({})", separator, detail::enumNumber(enumerator), enumName(enumerator));
                separator = ", ";
            }
            return choices;
        }
    }

    auto assign(std::string_view input) -> Result<void, CvarError> override {
        const std::optional<Type> parsed = parse(input);
        if (!parsed) {
            return refuse(CvarError::InvalidValue, input);
        }
        return set(*parsed);
    }

private:
    struct Range {
        std::optional<Type> minimum{};
        std::optional<Type> maximum{};
    };
    struct NoRange {};

    [[nodiscard]] static auto parse(std::string_view input) -> std::optional<Type> {
        if constexpr (std::same_as<Type, bool>) {
            return detail::parseBool(input);
        } else if constexpr (std::same_as<Type, std::int32_t>) {
            return detail::parseInteger(input);
        } else if constexpr (std::same_as<Type, float>) {
            return detail::parseFloat(input);
        } else if constexpr (std::same_as<Type, std::string>) {
            return std::string{input};
        } else {
            return detail::parseEnum<Type>(input);
        }
    }

    [[nodiscard]] static auto format(const Type& value) -> std::string {
        if constexpr (std::same_as<Type, bool>) {
            return value ? "1" : "0";
        } else if constexpr (NumericCvarType<Type>) {
            return std::format("{}", value);
        } else if constexpr (std::same_as<Type, std::string>) {
            return value;
        } else {
            return std::format("{}", detail::enumNumber(value));
        }
    }

    [[nodiscard]] auto validate(const Type& candidate) const -> Result<void, CvarError> {
        if constexpr (std::same_as<Type, float>) {
            if (!std::isfinite(candidate)) {
                return refuse(CvarError::InvalidValue, format(candidate));
            }
        }
        if constexpr (NumericCvarType<Type>) {
            if ((m_range.minimum && candidate < *m_range.minimum) ||
                (m_range.maximum && candidate > *m_range.maximum)) {
                return refuse(CvarError::OutOfRange, format(candidate));
            }
        } else if constexpr (std::same_as<Type, std::string>) {
            if (!detail::isWritableText(candidate)) {
                return refuse(CvarError::InvalidValue, candidate);
            }
        } else if constexpr (CountedEnum<Type>) {
            if (!detail::isEnumerator(candidate)) {
                return refuse(CvarError::InvalidValue, format(candidate));
            }
        }
        return {};
    }

    Type m_value;
    Type m_default;
    std::conditional_t<NumericCvarType<Type>, Range, NoRange> m_range{};
};

/// Typed handle to a registered cvar: what code reads every tick and sets directly. Setting from
/// code checks the type and range but not the flags, which only restrict the console. Valid as long
/// as the Console that registered the cvar.
template <CvarType Type>
class Cvar {
public:
    explicit Cvar(TypedCvar<Type>& cvar) noexcept : m_cvar{&cvar} {}

    [[nodiscard]] auto value() const noexcept -> const Type& { return m_cvar->value(); }
    auto set(Type newValue) const -> Result<void, CvarError> { return m_cvar->set(std::move(newValue)); }

    [[nodiscard]] auto name() const noexcept -> std::string_view { return m_cvar->name(); }
    [[nodiscard]] auto flags() const noexcept -> CvarFlags { return m_cvar->flags(); }
    [[nodiscard]] auto revision() const noexcept -> std::uint64_t { return m_cvar->revision(); }

private:
    TypedCvar<Type>* m_cvar;
};

} // namespace nx::console
