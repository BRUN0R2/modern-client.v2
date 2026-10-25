// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.platform;

namespace app {

namespace {

/// The option of `options` that `word` names when it takes a folder or a file; none for another word.
[[nodiscard]] auto pathOptionOf(ClientOptions& options, std::string_view word) noexcept
    -> std::optional<std::filesystem::path>* {
    if (word == "-basedir") {
        return &options.install;
    }
    if (word == "--content-config") {
        return &options.contentConfig;
    }
    if (word == "--data") {
        return &options.data;
    }
    return nullptr;
}

} // namespace

auto parseClientOptions(std::span<const std::string_view> arguments) -> std::optional<ClientOptions> {
    const auto isOption = [](std::string_view word) {
        return word.starts_with('+') || word.starts_with('-');
    };
    ClientOptions options;
    std::set<std::string_view> seen;
    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view word = arguments[index];
        if (word.starts_with('+')) {
            options.bootCommands += word.substr(1);
            for (; index + 1 < arguments.size() && !isOption(arguments[index + 1]); ++index) {
                options.bootCommands += ' ';
                options.bootCommands += arguments[index + 1];
            }
            options.bootCommands += '\n';
            continue;
        }
        if (!seen.insert(word).second) {
            return std::nullopt; // given twice
        }
        if (word == "--validation") {
            options.isValidationEnabled = true;
            continue;
        }
        std::optional<std::filesystem::path>* const path = pathOptionOf(options, word);
        if (path == nullptr || index + 1 == arguments.size()) {
            return std::nullopt; // not an option, or its folder or file missing
        }
        *path = nx::platform::fromUtf8(arguments[++index]);
    }
    return options;
}

} // namespace app
