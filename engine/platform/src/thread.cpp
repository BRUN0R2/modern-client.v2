// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#if defined(_WIN32)
#include <windows.h> // WIN32_LEAN_AND_MEAN and NOMINMAX come from engine/platform/CMakeLists.txt
#else
#include <pthread.h>
#endif

module nx.platform;

import std;
import nx.core;

namespace nx::platform {

namespace {

/// `name`, which must be a thread's name.
[[nodiscard]] auto checkedName(std::string_view name) -> std::string {
    expects(!name.empty() && name.size() <= kMaxThreadNameLength,
            "Thread: a name of 1 to kMaxThreadNameLength characters");
    expects(std::ranges::all_of(name, [](char letter) { return letter > ' ' && letter <= '~'; }),
            "Thread: a name of printable ASCII characters");
    return std::string{name};
}

/// Names the calling thread for the debuggers and profilers; a refusal changes nothing else.
void nameThisThread(const std::string& name) {
#if defined(_WIN32)
    const std::wstring wide(name.begin(), name.end()); // ASCII: each character as it is
    std::ignore = SetThreadDescription(GetCurrentThread(), wide.c_str());
#else
    std::ignore = pthread_setname_np(pthread_self(), name.c_str());
#endif
}

} // namespace

Thread::Thread(std::string_view name, std::move_only_function<void(std::stop_token)> body)
    : m_thread{[named = checkedName(name), run = std::move(body)](std::stop_token stop) mutable {
          nameThisThread(named);
          run(std::move(stop));
      }} {}

} // namespace nx::platform
