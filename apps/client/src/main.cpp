// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// next: the client of Modern-Client.v2 (app.client).

#include <cstdio>
#include <cstdlib>

import std;
import nx.core;
import app.client;

namespace {

constexpr int kExitUnusable = 2; // the command line, the settings, the content, the window or the GPU

} // namespace

auto main(int argumentCount, char** argumentValues) -> int {
    // UTF-8 everywhere: on Windows, the manifest of the program makes it the code page (nxProgram).
    const auto arguments = std::span{argumentValues, nx::narrow<std::size_t>(argumentCount)} |
                           std::views::drop(1) | std::ranges::to<std::vector<std::string_view>>();
    if (std::ranges::contains(arguments, std::string_view{"--help"})) {
        std::print("{}", app::kClientUsage);
        return EXIT_SUCCESS;
    }
    const std::optional<app::ClientOptions> options = app::parseClientOptions(arguments);
    if (!options) {
        std::print(stderr, "{}", app::kClientUsage);
        return kExitUnusable;
    }
    if (auto ran = app::runClient(*options); !ran) {
        std::println(stderr, "next: {}", nx::describe(ran.error()));
        return ran.error().code == app::ClientError::ValidationFailed ? EXIT_FAILURE : kExitUnusable;
    }
    return EXIT_SUCCESS;
}
