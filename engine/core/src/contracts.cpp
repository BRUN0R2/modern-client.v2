module;

// stderr is a macro and macros are not exported by `import std;`. The FILE* overload of
// std::println is used because the std::ostream one is unavailable without exceptions.
#include <cstdio>

module nx.core;

import std;

namespace nx {

namespace {

// constinit: ready before any dynamic initializer can report a violation.
constinit std::atomic<ContractViolationHandler> activeHandler{&enforceContract};

} // namespace

void enforceContract(const ContractViolation& violation) noexcept {
    std::println(stderr, "nx: {} violated: {}\n    at {}:{} in {}", enumName(violation.kind),
                 violation.description, violation.where.file_name(), violation.where.line(),
                 violation.where.function_name());
    std::fflush(stderr);
    std::abort();
}

auto setContractViolationHandler(ContractViolationHandler handler) noexcept -> ContractViolationHandler {
    const auto replacement = handler != nullptr ? handler : &enforceContract;
    // acq_rel: publishes the new handler to reporters and observes the one it replaces.
    return activeHandler.exchange(replacement, std::memory_order_acq_rel);
}

namespace detail {

void reportContractViolation(ContractKind kind, std::string_view description,
                             std::source_location where) noexcept {
    // acquire: pairs with the release half of the exchange in setContractViolationHandler.
    const auto handler = activeHandler.load(std::memory_order_acquire);
    handler(ContractViolation{.kind = kind, .description = description, .where = where});
}

} // namespace detail

} // namespace nx
