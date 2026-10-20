// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#if defined(_WIN32)
#include <windows.h> // WIN32_LEAN_AND_MEAN and NOMINMAX come from engine/platform/CMakeLists.txt
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

module nx.platform;

import std;
import nx.core;

namespace nx::platform {

namespace {

/// The most bytes one read asks the operating system for: below the limits of ReadFile (a DWORD)
/// and of read on Linux (0x7ffff000 bytes).
constexpr std::size_t kMaxBytesPerRead = std::size_t{1} << 30;

/// The FileError of an operating system error, or `otherwise` when the error is specific to the
/// operation.
[[nodiscard]] auto classify(std::error_code error, FileError otherwise) noexcept -> FileError {
    if (error == std::errc::no_such_file_or_directory || error == std::errc::not_a_directory) {
        return FileError::NotFound;
    }
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted) {
        return FileError::AccessDenied;
    }
    return otherwise;
}

/// "<path>: <message of the operating system> (error <code>)".
[[nodiscard]] auto osFailureText(const std::filesystem::path& path, std::error_code error) -> std::string {
    return std::format("{}: {} (error {})", toUtf8(path), error.message(), error.value());
}

[[nodiscard]] auto shrankText(const std::filesystem::path& path) -> std::string {
    return std::format("{}: the file shrank while it was read", toUtf8(path));
}

/// What a file is read into: bytes, or the text of a text file.
template <class Buffer>
concept FileBuffer = std::same_as<Buffer, std::vector<std::byte>> || std::same_as<Buffer, std::string>;

#if defined(_WIN32)

[[nodiscard]] auto lastError() noexcept -> std::error_code {
    return std::error_code{static_cast<int>(GetLastError()), std::system_category()};
}

/// Closes a handle at the end of its scope.
class ScopedHandle {
public:
    explicit ScopedHandle(HANDLE handle) noexcept : m_handle{handle} {}
    ScopedHandle(const ScopedHandle&) = delete;
    ScopedHandle(ScopedHandle&&) = delete;
    auto operator=(const ScopedHandle&) -> ScopedHandle& = delete;
    auto operator=(ScopedHandle&&) -> ScopedHandle& = delete;
    ~ScopedHandle() { CloseHandle(m_handle); }

    [[nodiscard]] auto get() const noexcept -> HANDLE { return m_handle; }

private:
    HANDLE m_handle;
};

template <FileBuffer Buffer>
[[nodiscard]] auto readRegularFile(const std::filesystem::path& path) -> Result<Buffer, FileError> {
    // Shared as a POSIX file is: other processes may write, rename or delete it meanwhile.
    const HANDLE handle =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const std::error_code error = lastError();
        return fail(classify(error, FileError::ReadFailed), osFailureText(path, error));
    }
    const ScopedHandle file{handle};

    LARGE_INTEGER size{};
    if (GetFileSizeEx(file.get(), &size) == FALSE) {
        return fail(FileError::ReadFailed, osFailureText(path, lastError()));
    }
    Buffer bytes(narrow<std::size_t>(size.QuadPart), {});
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto request = narrow<DWORD>(std::min(bytes.size() - offset, kMaxBytesPerRead));
        DWORD count = 0;
        if (ReadFile(file.get(), std::span{bytes}.subspan(offset).data(), request, &count, nullptr) ==
            FALSE) {
            return fail(FileError::ReadFailed, osFailureText(path, lastError()));
        }
        if (count == 0) {
            return fail(FileError::ReadFailed, shrankText(path));
        }
        offset += count;
    }
    return bytes;
}

#else

[[nodiscard]] auto lastError() noexcept -> std::error_code {
    return std::error_code{errno, std::generic_category()};
}

/// Closes a file descriptor at the end of its scope.
class ScopedDescriptor {
public:
    explicit ScopedDescriptor(int descriptor) noexcept : m_descriptor{descriptor} {}
    ScopedDescriptor(const ScopedDescriptor&) = delete;
    ScopedDescriptor(ScopedDescriptor&&) = delete;
    auto operator=(const ScopedDescriptor&) -> ScopedDescriptor& = delete;
    auto operator=(ScopedDescriptor&&) -> ScopedDescriptor& = delete;
    ~ScopedDescriptor() { ::close(m_descriptor); }

    [[nodiscard]] auto get() const noexcept -> int { return m_descriptor; }

private:
    int m_descriptor;
};

template <FileBuffer Buffer>
[[nodiscard]] auto readRegularFile(const std::filesystem::path& path) -> Result<Buffer, FileError> {
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        const std::error_code error = lastError();
        return fail(classify(error, FileError::ReadFailed), osFailureText(path, error));
    }
    const ScopedDescriptor file{descriptor};

    struct stat info{};
    if (::fstat(file.get(), &info) != 0) {
        return fail(FileError::ReadFailed, osFailureText(path, lastError()));
    }
    Buffer bytes(narrow<std::size_t>(info.st_size), {});
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ::ssize_t count = ::read(file.get(), std::span{bytes}.subspan(offset).data(),
                                       std::min(bytes.size() - offset, kMaxBytesPerRead));
        if (count < 0) {
            if (errno == EINTR) {
                continue; // a signal interrupted the read before any byte arrived
            }
            return fail(FileError::ReadFailed, osFailureText(path, lastError()));
        }
        if (count == 0) {
            return fail(FileError::ReadFailed, shrankText(path));
        }
        offset += narrow<std::size_t>(count);
    }
    return bytes;
}

#endif

/// readFile and readTextFile: checks that `path` is a regular file, then reads it.
template <FileBuffer Buffer>
[[nodiscard]] auto readWholeFile(const std::filesystem::path& path) -> Result<Buffer, FileError> {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (status.type() == std::filesystem::file_type::not_found) {
        return fail(FileError::NotFound, toUtf8(path));
    }
    if (error) {
        return fail(classify(error, FileError::ReadFailed), osFailureText(path, error));
    }
    if (!std::filesystem::is_regular_file(status)) {
        return fail(FileError::NotAFile, toUtf8(path));
    }
    return readRegularFile<Buffer>(path);
}

} // namespace

auto toUtf8(const std::filesystem::path& path) -> std::string {
    return path.generic_u8string() |
           std::views::transform([](char8_t unit) { return static_cast<char>(unit); }) |
           std::ranges::to<std::string>();
}

auto fromUtf8(std::string_view text) -> std::filesystem::path {
    return std::filesystem::path{text |
                                 std::views::transform([](char unit) { return static_cast<char8_t>(unit); }) |
                                 std::ranges::to<std::u8string>()};
}

auto listFiles(const std::filesystem::path& directory)
    -> Result<std::vector<std::filesystem::path>, FileError> {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(directory, error);
    if (status.type() == std::filesystem::file_type::not_found) {
        return fail(FileError::NotFound, toUtf8(directory));
    }
    if (error) {
        return fail(classify(error, FileError::ListFailed), osFailureText(directory, error));
    }
    if (!std::filesystem::is_directory(status)) {
        return fail(FileError::NotADirectory, toUtf8(directory));
    }

    std::vector<std::filesystem::path> files;
    for (std::filesystem::recursive_directory_iterator entry{directory,
                                                             std::filesystem::directory_options::none, error};
         !error && entry != std::filesystem::recursive_directory_iterator{}; entry.increment(error)) {
        std::error_code kindError;
        const std::filesystem::file_status kind = entry->status(kindError);
        if (kind.type() == std::filesystem::file_type::not_found) {
            continue; // a dangling link
        }
        if (kindError) {
            return fail(classify(kindError, FileError::ListFailed), osFailureText(entry->path(), kindError));
        }
        if (std::filesystem::is_regular_file(kind)) {
            files.push_back(entry->path().lexically_relative(directory));
        }
    }
    if (error) {
        return fail(classify(error, FileError::ListFailed), osFailureText(directory, error));
    }
    std::ranges::sort(files);
    return files;
}

auto readFile(const std::filesystem::path& path) -> Result<std::vector<std::byte>, FileError> {
    return readWholeFile<std::vector<std::byte>>(path);
}

auto readTextFile(const std::filesystem::path& path) -> Result<std::string, FileError> {
    return readWholeFile<std::string>(path);
}

} // namespace nx::platform
