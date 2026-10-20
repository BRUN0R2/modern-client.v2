// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

// Feature-test macros (not exported by `import std;`) and the platform primitives behind the
// facade. Headers stay in this implementation unit, out of the exported interface.
#include <version>

#if defined(__cpp_lib_debugging)
#include <debugging>
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__linux__)
#include <csignal>
#else
#error "nx.core:debugging: add the debugger primitives of this platform"
#endif

module nx.core;

import std;

namespace nx {

namespace {

#if !defined(__cpp_lib_debugging) && defined(__linux__)
/// Linux reports the tracing process (a debugger) in /proc/self/status: "TracerPid:\t<pid>", 0 when
/// nothing traces the process.
[[nodiscard]] auto hasTracer() noexcept -> bool {
    constexpr std::string_view kTracerField = "TracerPid:";
    std::ifstream status{"/proc/self/status"};
    std::string line;
    while (std::getline(status, line)) {
        if (line.starts_with(kTracerField)) {
            const std::string_view value = std::string_view{line}.substr(kTracerField.size());
            return value.find_first_not_of(" \t0") != std::string_view::npos;
        }
    }
    return false;
}
#endif

} // namespace

auto isDebuggerPresent() noexcept -> bool {
#if defined(__cpp_lib_debugging)
    return std::is_debugger_present();
#elif defined(_WIN32)
    return IsDebuggerPresent() != FALSE;
#else
    return hasTracer();
#endif
}

void breakpointIfDebugging() noexcept {
#if defined(__cpp_lib_debugging)
    std::breakpoint_if_debugging();
#else
    if (!isDebuggerPresent()) {
        return;
    }
#if defined(_WIN32)
    __debugbreak();
#else
    std::ignore = std::raise(SIGTRAP); // the debugger stops on the signal and may resume
#endif
#endif
}

auto currentStackTrace() -> std::string {
#if defined(__cpp_lib_stacktrace)
    constexpr std::size_t kThisFunction = 1;
    const auto trace = std::stacktrace::current(kThisFunction);
    std::string text;
    for (std::size_t index = 0; index < trace.size(); ++index) {
        std::format_to(std::back_inserter(text), "    #{} {}\n", index, std::to_string(trace[index]));
    }
    return text;
#else
    return "    (stack trace unavailable: this standard library has no <stacktrace>)\n";
#endif
}

} // namespace nx
