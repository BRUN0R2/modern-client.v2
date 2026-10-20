// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: replaces the contract violation handler with one that records violations and
// returns ("observe"), so tests can assert that a check fired without aborting the process.
// Include after `import nx.core;`.

namespace nx_test {

struct RecordedViolations {
    int count = 0;
    nx::ContractViolation last;
};

inline auto recordedViolations() noexcept -> RecordedViolations& {
    static RecordedViolations recorded;
    return recorded;
}

inline void recordViolation(const nx::ContractViolation& violation) noexcept {
    auto& recorded = recordedViolations();
    ++recorded.count;
    recorded.last = violation;
}

/// Installs the recording handler for the lifetime of the object and restores the previous one.
class ContractRecorder {
public:
    ContractRecorder() noexcept : m_previous{nx::setContractViolationHandler(&recordViolation)} {
        recordedViolations() = {};
    }

    ~ContractRecorder() { nx::setContractViolationHandler(m_previous); }

    ContractRecorder(const ContractRecorder&) = delete;
    ContractRecorder(ContractRecorder&&) = delete;
    auto operator=(const ContractRecorder&) -> ContractRecorder& = delete;
    auto operator=(ContractRecorder&&) -> ContractRecorder& = delete;

    [[nodiscard]] static auto count() noexcept -> int { return recordedViolations().count; }
    [[nodiscard]] static auto last() noexcept -> const nx::ContractViolation& {
        return recordedViolations().last;
    }

private:
    nx::ContractViolationHandler m_previous;
};

} // namespace nx_test
