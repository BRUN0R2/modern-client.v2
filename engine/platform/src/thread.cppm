// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The engine's own threads outside the job system's pool, those that block in OS calls
/// (docs/04-job-system.md, section 5), named for the debuggers and profilers.
export module nx.platform:thread;

import std;

export namespace nx::platform {

/// The longest name a thread keeps everywhere: Linux keeps 15 characters.
inline constexpr std::size_t kMaxThreadNameLength = 15;

/// A thread of its own, named. As it ends it asks its body to stop, through the body's stop token,
/// and waits for the body to return (std::jthread).
class Thread {
public:
    /// No thread.
    Thread() noexcept = default;

    /// Runs `body` on a new thread named `name`: ASCII, at most kMaxThreadNameLength characters. A
    /// failure to create a thread ends the process: std::thread reports it only through an
    /// exception, and the engine is built without them.
    Thread(std::string_view name, std::move_only_function<void(std::stop_token)> body);

    Thread(Thread&&) noexcept = default;
    /// Ends the thread held first, as the destructor does.
    auto operator=(Thread&&) noexcept -> Thread& = default;
    Thread(const Thread&) = delete;
    auto operator=(const Thread&) -> Thread& = delete;
    ~Thread() = default;

    /// Asks the body to stop: its stop token tells it, and the body returns when it looks.
    inline void requestStop() noexcept { m_thread.request_stop(); }

private:
    std::jthread m_thread;
};

} // namespace nx::platform
