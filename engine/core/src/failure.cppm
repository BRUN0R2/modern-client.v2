// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Typed failures (docs/02-coding-standards.md, section 6; ADR-0005).
///
/// Expected failures are values: nx::Result<T, Code> is std::expected<T, nx::Failure<Code>>,
/// where Code is the reporting module's own `enum class`. There is no generic error code and
/// no exceptions. Crossing a layer translates the code and keeps the original as the cause.
export module nx.core:failure;

import std;
import :introspection;

export namespace nx {

/// Error codes are scoped enums owned by the module that reports them.
template <class Code>
concept ErrorCode = ScopedEnum<Code>;

/// One link of a failure chain after its code type has been erased.
struct CauseRecord {
    std::string_view domain; ///< Unqualified name of the code enum, e.g. "BspError".
    std::string_view code;   ///< Enumerator name; empty when the value has no enumerator.
    std::int64_t value = 0;  ///< Numeric value of the code.
    std::string detail;      ///< Context: the offending value, path or limit.
    std::source_location where;
};

/// Underlying causes of a failure, outermost first. Allocates only on error paths.
class Cause {
public:
    [[nodiscard]] inline auto empty() const noexcept -> bool { return m_records.empty(); }
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return m_records.size(); }
    [[nodiscard]] inline auto records() const noexcept -> std::span<const CauseRecord> { return m_records; }

    inline void push(CauseRecord record) { m_records.push_back(std::move(record)); }
    inline void append(const Cause& other) {
        m_records.insert(m_records.end(), other.m_records.begin(), other.m_records.end());
    }

private:
    std::vector<CauseRecord> m_records;
};

/// An expected failure: its code, human-readable context, where it happened and what caused it.
template <ErrorCode Code>
struct Failure {
    Code code{};
    std::string detail;
    std::source_location where;
    Cause cause;
};

/// What a function that can fail returns: a value, or the Failure (docs/02, section 6). Each such
/// function is [[nodiscard]]: std::expected is not marked by the standard.
template <class Value, ErrorCode Code>
using Result = std::expected<Value, Failure<Code>>;

namespace detail {

/// How failures print a code: its enum's short name, its enumerator's name and its value.
struct CodeIdentity {
    std::string_view domain;
    std::string_view code;
    std::int64_t value = 0;
};

template <ErrorCode Code>
[[nodiscard]] constexpr auto identify(Code code) noexcept -> CodeIdentity {
    static_assert(std::in_range<std::int64_t>(std::numeric_limits<std::underlying_type_t<Code>>::max()),
                  "nx: the values of an error code must fit in std::int64_t");
    return CodeIdentity{.domain = shortTypeName<Code>(),
                        .code = enumName(code),
                        .value = static_cast<std::int64_t>(std::to_underlying(code))};
}

} // namespace detail

/// Erases the code type of `failure`, keeping its names, value, detail and location.
template <ErrorCode Code>
[[nodiscard]] auto toCauseRecord(Failure<Code>&& failure) -> CauseRecord {
    const detail::CodeIdentity identity = detail::identify(failure.code);
    return CauseRecord{
        .domain = identity.domain,
        .code = identity.code,
        .value = identity.value,
        .detail = std::move(failure.detail),
        .where = failure.where,
    };
}

/// Builds the error side of a Result: `return nx::fail(BspError::TooSmall, "12 bytes");`.
template <ErrorCode Code>
[[nodiscard]] auto fail(Code code, std::string detail = {},
                        std::source_location where = std::source_location::current())
    -> std::unexpected<Failure<Code>> {
    return std::unexpected(
        Failure<Code>{.code = code, .detail = std::move(detail), .where = where, .cause = {}});
}

/// Translates `inner` into this layer's `code`, keeping `inner` (and its own causes) as the cause.
template <ErrorCode Code, ErrorCode InnerCode>
[[nodiscard]] auto fail(Code code, std::string detail, Failure<InnerCode> inner,
                        std::source_location where = std::source_location::current())
    -> std::unexpected<Failure<Code>> {
    const Cause innerCauses = std::move(inner.cause);
    Cause cause;
    cause.push(toCauseRecord(std::move(inner)));
    cause.append(innerCauses);
    return std::unexpected(
        Failure<Code>{.code = code, .detail = std::move(detail), .where = where, .cause = std::move(cause)});
}

/// Same as above, without extra detail at this layer.
template <ErrorCode Code, ErrorCode InnerCode>
[[nodiscard]] auto fail(Code code, Failure<InnerCode> inner,
                        std::source_location where = std::source_location::current())
    -> std::unexpected<Failure<Code>> {
    return fail(code, std::string{}, std::move(inner), where);
}

namespace detail {

void appendFailureLine(std::string& out, std::string_view domain, std::string_view code, std::int64_t value,
                       std::string_view context, const std::source_location& where);

} // namespace detail

/// Multi-line description of a failure and its causes:
///     BspError::UnsupportedVersion: version 29 [bsp.cpp:42]
///       caused by: VfsError::NotFound: maps/x.bsp [vfs.cpp:10]
template <ErrorCode Code>
[[nodiscard]] auto describe(const Failure<Code>& failure) -> std::string {
    std::string text;
    const detail::CodeIdentity identity = detail::identify(failure.code);
    detail::appendFailureLine(text, identity.domain, identity.code, identity.value, failure.detail,
                              failure.where);
    for (const auto& record : failure.cause.records()) {
        text += "\n  caused by: ";
        detail::appendFailureLine(text, record.domain, record.code, record.value, record.detail,
                                  record.where);
    }
    return text;
}

} // namespace nx

/// `std::format("{}", failure)` prints nx::describe(failure).
template <nx::ErrorCode Code>
struct std::formatter<nx::Failure<Code>> : std::formatter<std::string_view> {
    template <class FormatContext>
    auto format(const nx::Failure<Code>& failure, FormatContext& context) const {
        return std::formatter<std::string_view>::format(nx::describe(failure), context);
    }
};
