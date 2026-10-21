// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

// nxcheck: checks the Counter-Strike 1.6 installation with the readers of nx.assets and reports
// what each format read and every file that failed.

#include <cstdio>
#include <cstdlib>

import std;
import nx.core;
import nx.jobs;
import nx.platform;
import nx.assets;
import tool.nxcheck;

namespace {

constexpr int kExitFailedFiles = 1; // some file failed its check
constexpr int kExitUnusable = 2;    // the arguments, the content description or the installation

constexpr std::string_view kUsage = R"(usage: nxcheck [--install <folder>] [--data <folder>]

Reads every file of the Counter-Strike 1.6 installation through the VFS, with the readers of
nx.assets, and reports what each format read and every file that failed.

  --install <folder>  the installation: the folder with cstrike/ and valve/ (default: the one
                      Steam installed)
  --data <folder>     the data/ folder of the project (default: the one next to nxcheck)
)";

struct Options {
    std::optional<std::filesystem::path> install;
    std::optional<std::filesystem::path> data;
};

/// The options in `arguments`, the command line without the program; nullopt when they are not
/// valid options.
[[nodiscard]] auto parseOptions(std::span<const std::string_view> arguments) -> std::optional<Options> {
    Options options;
    constexpr std::size_t kWordsPerOption = 2; // the option, then its value
    for (std::size_t index = 0; index < arguments.size(); index += kWordsPerOption) {
        const std::string_view option = arguments[index];
        std::optional<std::filesystem::path>* value = option == "--install" ? &options.install
                                                      : option == "--data"  ? &options.data
                                                                            : nullptr;
        // An unknown or repeated option, or the last one without its value.
        if (value == nullptr || value->has_value() || index + 1 == arguments.size()) {
            return std::nullopt;
        }
        *value = nx::platform::fromUtf8(arguments[index + 1]);
    }
    return options;
}

/// `bytes` in MiB, with one decimal.
[[nodiscard]] auto mebibytes(std::uint64_t bytes) -> std::string {
    constexpr double kBytesPerMebibyte = 1024.0 * 1024.0;
    return std::format("{:.1f} MiB", static_cast<double>(bytes) / kBytesPerMebibyte);
}

/// `text` with every line after the first indented by `indent`.
[[nodiscard]] auto indentLines(std::string_view text, std::string_view indent) -> std::string {
    std::string indented;
    for (const char character : text) {
        indented += character;
        if (character == '\n') {
            indented += indent;
        }
    }
    return indented;
}

/// Prints what each format read, the extensions without a reader and every failure, with the file
/// of `vfs` it is (relative to `install`); returns how many files failed.
[[nodiscard]] auto report(std::span<const nxcheck::FileResult> results, const nx::assets::Vfs& vfs,
                          const std::filesystem::path& install) -> std::size_t {
    struct Totals {
        std::size_t files = 0;
        std::size_t failed = 0;
        std::uint64_t bytes = 0;
    };
    nx::EnumArray<nxcheck::Format, Totals> totals;
    std::map<std::string, std::size_t> unread; // by lowercase extension
    for (const nxcheck::FileResult& result : results) {
        if (!result.format) {
            const std::string_view extension =
                std::string_view{result.path}.substr(result.path.rfind('.') + 1);
            ++unread[extension | std::views::transform(nx::toLowerAscii) | std::ranges::to<std::string>()];
            continue;
        }
        Totals& total = totals[*result.format];
        ++total.files;
        if (result.failure) {
            ++total.failed;
        }
        total.bytes += result.bytes;
    }

    std::println("{:<14} {:>7} {:>7} {:>12}", "format", "files", "failed", "read");
    for (const nxcheck::Format format : nx::EnumArray<nxcheck::Format, Totals>::keys()) {
        const Totals& total = totals[format];
        std::println("{:<14} {:>7} {:>7} {:>12}", nx::enumName(format), total.files, total.failed,
                     mebibytes(total.bytes));
    }
    if (!unread.empty()) {
        const auto counts = unread | std::views::transform([](const auto& entry) {
                                return std::format("{} {}", entry.second, entry.first);
                            });
        std::println("no reader yet: {}",
                     counts | std::views::join_with(std::string_view{", "}) | std::ranges::to<std::string>());
    }

    std::size_t failures = 0;
    for (const nxcheck::FileResult& result : results) {
        if (result.failure) {
            const auto native =
                nx::assets::VfsPath::parse(result.path).and_then([&vfs](const nx::assets::VfsPath& path) {
                    return vfs.nativePath(path);
                });
            std::println("\nFAILED {}\n  {}",
                         native ? nx::platform::toUtf8(native->lexically_relative(install)) : result.path,
                         indentLines(nx::describe(*result.failure), "  "));
            ++failures;
        }
    }
    std::println("\n{} files, {} failed", results.size(), failures);
    return failures;
}

[[nodiscard]] auto run(const Options& options) -> int {
    std::filesystem::path data;
    if (options.data) {
        data = *options.data;
    } else {
        auto executable = nx::platform::executablePath();
        if (!executable) {
            std::println(stderr, "nxcheck: {}", nx::describe(executable.error()));
            return kExitUnusable;
        }
        data = executable->parent_path() / "data";
    }

    const std::filesystem::path descriptionFile = data / "content" / "goldsrcCstrike.toml";
    auto text = nx::platform::readTextFile(descriptionFile);
    if (!text) {
        std::println(stderr, "nxcheck: {}", nx::describe(text.error()));
        return kExitUnusable;
    }
    auto description = nx::assets::readGoldsrcInstallDesc(*text, nx::platform::toUtf8(descriptionFile));
    if (!description) {
        std::println(stderr, "nxcheck: {}", nx::describe(description.error()));
        return kExitUnusable;
    }

    auto install = options.install
                       ? nx::assets::GoldsrcInstall::open(*description, *options.install)
                       : nx::assets::detectGoldsrcInstall(*description, nx::platform::steamFolders());
    if (!install) {
        std::println(stderr, "nxcheck: {}", nx::describe(install.error()));
        return kExitUnusable;
    }
    auto vfs = nx::assets::Vfs::create(install->mounts());
    if (!vfs) {
        std::println(stderr, "nxcheck: {}", nx::describe(vfs.error()));
        return kExitUnusable;
    }
    auto jobs = nx::jobs::JobSystem::create();
    if (!jobs) {
        std::println(stderr, "nxcheck: {}", nx::describe(jobs.error()));
        return kExitUnusable;
    }

    std::println("nxcheck: {}", nx::platform::toUtf8(install->folder()));
    const nx::platform::Stopwatch stopwatch;
    const std::vector<nxcheck::FileResult> results = nxcheck::checkContent(*vfs, **jobs);
    const std::chrono::duration<double> elapsed = stopwatch.elapsed();
    std::println("checked in {:.2f} s with {} workers\n", elapsed.count(), (*jobs)->workerCount());
    return report(results, *vfs, install->folder()) == 0 ? EXIT_SUCCESS : kExitFailedFiles;
}

} // namespace

auto main(int argumentCount, char** argumentValues) -> int {
    // UTF-8 everywhere: on Windows, the manifest of the program makes it the code page (nxProgram).
    const auto arguments = std::span{argumentValues, nx::narrow<std::size_t>(argumentCount)} |
                           std::views::drop(1) | std::ranges::to<std::vector<std::string_view>>();
    if (std::ranges::contains(arguments, std::string_view{"--help"})) {
        std::print("{}", kUsage);
        return EXIT_SUCCESS;
    }
    const std::optional<Options> options = parseOptions(arguments);
    if (!options) {
        std::print(stderr, "{}", kUsage);
        return kExitUnusable;
    }
    return run(*options);
}
