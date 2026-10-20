// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test doubles for the console: an output that records what it gets and in-memory config files.
// Include after `import nx.console;`.

namespace nx_test {

class RecordingOutput final : public nx::console::ConsoleOutput {
public:
    void print(std::string_view line) override { lines.emplace_back(line); }
    void reportFailure(const nx::Failure<nx::console::ConsoleError>& failure) override {
        failures.push_back(failure);
    }

    /// Codes of the reported failures, in order.
    [[nodiscard]] auto failureCodes() const -> std::vector<nx::console::ConsoleError> {
        std::vector<nx::console::ConsoleError> codes;
        for (const auto& failure : failures) {
            codes.push_back(failure.code);
        }
        return codes;
    }

    void clear() {
        lines.clear();
        failures.clear();
    }

    std::vector<std::string> lines;
    std::vector<nx::Failure<nx::console::ConsoleError>> failures;
};

class MemoryFiles final : public nx::console::ConfigFiles {
public:
    [[nodiscard]] auto read(std::string_view path)
        -> nx::Result<std::string, nx::console::ConfigFileError> override {
        const auto found = files.find(std::string{path});
        if (found == files.end()) {
            return nx::fail(nx::console::ConfigFileError::NotFound, std::string{path});
        }
        return found->second;
    }

    std::map<std::string, std::string> files;
};

} // namespace nx_test
