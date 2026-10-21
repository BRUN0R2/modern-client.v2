// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Included after `import std;` and `import nx.core;`.

namespace nx::test {

/// Records the messages logged while it lives: a logger of its own is the default meanwhile.
class LogRecorder {
public:
    LogRecorder() : m_scope{m_logger} { m_logger.addSink(std::make_unique<Recorder>(m_messages)); }

    LogRecorder(const LogRecorder&) = delete;
    LogRecorder(LogRecorder&&) = delete;
    auto operator=(const LogRecorder&) -> LogRecorder& = delete;
    auto operator=(LogRecorder&&) -> LogRecorder& = delete;
    ~LogRecorder() = default;

    /// What was logged at `level`, in order.
    [[nodiscard]] auto messagesAt(nx::log::Level level) const -> std::vector<std::string> {
        return m_messages |
               std::views::filter([level](const auto& message) { return message.first == level; }) |
               std::views::values | std::ranges::to<std::vector>();
    }

private:
    using Messages = std::vector<std::pair<nx::log::Level, std::string>>;

    class Recorder final : public nx::log::Sink {
    public:
        explicit Recorder(Messages& messages) noexcept : m_messages{&messages} {}
        void write(const nx::log::Record& record) noexcept override {
            m_messages->emplace_back(record.level, std::string{record.message});
        }

    private:
        Messages* m_messages;
    };

    Messages m_messages;
    nx::log::Logger m_logger;
    nx::log::DefaultLoggerScope m_scope;
};

} // namespace nx::test
