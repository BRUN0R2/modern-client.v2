// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// Contract checks: the portable stand-in for C++26 `pre`, `post` and `contract_assert`
/// (docs/02-coding-standards.md, section 6.5).
///
/// A violated contract is a programming error, never an expected failure (those are
/// nx::Result). The installed handler decides the semantic: returning from it continues
/// execution ("observe"); terminating stops it ("enforce", the default).
export module nx.core:contracts;

import std;

export namespace nx {

/// Which kind of contract a violation broke.
enum class ContractKind : std::uint8_t {
    Precondition,
    Postcondition,
    Invariant,
    Count,
};

/// What a handler receives: the kind, the description written at the check, and where it is.
struct ContractViolation {
    ContractKind kind = ContractKind::Invariant;
    std::string_view description;
    std::source_location where;
};

/// The process-wide reaction to violations. Returning lets execution continue ("observe"): the
/// code then takes the defined path written next to each check (docs/02, section 6.5).
using ContractViolationHandler = void (*)(const ContractViolation& violation) noexcept;

/// Default handler ("enforce"): writes the violation and the call stack to stderr, stops in the
/// debugger when one is attached, then aborts the process.
void enforceContract(const ContractViolation& violation) noexcept;

/// Installs `handler` for the whole process and returns the previous one; `nullptr` restores
/// enforceContract. One of the two sanctioned pieces of mutable global state in the engine, with
/// the default logger (docs/01-architecture.md, section 2.1), mirroring the process-wide C++26
/// contract violation handler.
auto setContractViolationHandler(ContractViolationHandler handler) noexcept -> ContractViolationHandler;

namespace detail {

void reportContractViolation(ContractKind kind, std::string_view description,
                             std::source_location where) noexcept;

} // namespace detail

/// Precondition: what the caller must guarantee.
constexpr void expects(bool condition, std::string_view description,
                       std::source_location where = std::source_location::current()) noexcept {
    if (!condition) [[unlikely]] {
        detail::reportContractViolation(ContractKind::Precondition, description, where);
    }
}

/// Postcondition: what the function guarantees on return.
constexpr void ensures(bool condition, std::string_view description,
                       std::source_location where = std::source_location::current()) noexcept {
    if (!condition) [[unlikely]] {
        detail::reportContractViolation(ContractKind::Postcondition, description, where);
    }
}

/// Invariant: what must hold at this point of the code.
constexpr void invariant(bool condition, std::string_view description,
                         std::source_location where = std::source_location::current()) noexcept {
    if (!condition) [[unlikely]] {
        detail::reportContractViolation(ContractKind::Invariant, description, where);
    }
}

} // namespace nx
