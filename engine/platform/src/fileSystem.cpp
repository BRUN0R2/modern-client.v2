// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#if defined(_WIN32)
#include <shlobj.h>  // SHGetKnownFolderPath, from shell32
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

/// What writeFile appends to a file's name to write beside it first.
constexpr std::string_view kPartialSuffix = ".partial";

/// The folder of the user's own files, in the folder the system gives each user's settings.
#if defined(_WIN32)
constexpr std::string_view kUserFolder = "Modern-Client.v2";
#else
constexpr std::string_view kUserFolder = "modern-client.v2";
#endif

/// The most bytes one read or write asks the operating system for: below the limits of ReadFile and
/// WriteFile (a DWORD) and of read and write on Linux (0x7ffff000 bytes).
constexpr std::size_t kMaxBytesPerCall = std::size_t{1} << 30;

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

[[nodiscard]] auto wroteNothingText(const std::filesystem::path& path) -> std::string {
    return std::format("{}: the operating system wrote nothing", toUtf8(path));
}

/// What a file is read into: bytes, or the text of a text file.
template <class Buffer>
concept FileBuffer = std::same_as<Buffer, std::vector<std::byte>> || std::same_as<Buffer, std::string>;

/// What of a file is read: `size` bytes from `offset`, fewer when the file ends sooner; all of it by
/// default.
struct FilePart {
    std::uint64_t offset = 0;
    std::uint64_t size = std::numeric_limits<std::uint64_t>::max();
};

/// The bytes `part` asks for of a file of `fileSize` bytes: from its offset, none past the file's end.
[[nodiscard]] constexpr auto partWithin(FilePart part, std::uint64_t fileSize) noexcept -> FilePart {
    const std::uint64_t start = std::min(part.offset, fileSize);
    return {.offset = start, .size = std::min(part.size, fileSize - start)};
}

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
[[nodiscard]] auto readRegularFile(const std::filesystem::path& path, FilePart part)
    -> Result<Buffer, FileError> {
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
    const FilePart within = partWithin(part, narrow<std::uint64_t>(size.QuadPart));
    if (LARGE_INTEGER start{.QuadPart = narrow<LONGLONG>(within.offset)};
        within.offset > 0 && SetFilePointerEx(file.get(), start, nullptr, FILE_BEGIN) == FALSE) {
        return fail(FileError::ReadFailed, osFailureText(path, lastError()));
    }
    Buffer bytes(narrow<std::size_t>(within.size), {});
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto request = narrow<DWORD>(std::min(bytes.size() - offset, kMaxBytesPerCall));
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

[[nodiscard]] auto writeRegularFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
    -> Result<void, FileError> {
    const HANDLE handle =
        CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const std::error_code error = lastError();
        return fail(classify(error, FileError::WriteFailed), osFailureText(path, error));
    }
    const ScopedHandle file{handle};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto request = narrow<DWORD>(std::min(bytes.size() - offset, kMaxBytesPerCall));
        DWORD count = 0;
        if (WriteFile(file.get(), bytes.subspan(offset).data(), request, &count, nullptr) == FALSE) {
            return fail(FileError::WriteFailed, osFailureText(path, lastError()));
        }
        if (count == 0) {
            return fail(FileError::WriteFailed, wroteNothingText(path));
        }
        offset += count;
    }
    if (FlushFileBuffers(file.get()) == FALSE) {
        return fail(FileError::WriteFailed, osFailureText(path, lastError()));
    }
    return {};
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
[[nodiscard]] auto readRegularFile(const std::filesystem::path& path, FilePart part)
    -> Result<Buffer, FileError> {
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
    const FilePart within = partWithin(part, narrow<std::uint64_t>(info.st_size));
    if (within.offset > 0 && ::lseek(file.get(), narrow<::off_t>(within.offset), SEEK_SET) < 0) {
        return fail(FileError::ReadFailed, osFailureText(path, lastError()));
    }
    Buffer bytes(narrow<std::size_t>(within.size), {});
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ::ssize_t count = ::read(file.get(), std::span{bytes}.subspan(offset).data(),
                                       std::min(bytes.size() - offset, kMaxBytesPerCall));
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

[[nodiscard]] auto writeRegularFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
    -> Result<void, FileError> {
    constexpr ::mode_t kNewFileMode = 0666; // read and write for all, less the umask, as files are made
    const int descriptor = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, kNewFileMode);
    if (descriptor < 0) {
        const std::error_code error = lastError();
        return fail(classify(error, FileError::WriteFailed), osFailureText(path, error));
    }
    const ScopedDescriptor file{descriptor};
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const ::ssize_t count = ::write(file.get(), bytes.subspan(offset).data(),
                                        std::min(bytes.size() - offset, kMaxBytesPerCall));
        if (count < 0) {
            if (errno == EINTR) {
                continue; // a signal interrupted the write before any byte left
            }
            return fail(FileError::WriteFailed, osFailureText(path, lastError()));
        }
        if (count == 0) {
            return fail(FileError::WriteFailed, wroteNothingText(path));
        }
        offset += narrow<std::size_t>(count);
    }
    if (::fsync(file.get()) != 0) {
        return fail(FileError::WriteFailed, osFailureText(path, lastError()));
    }
    return {};
}

#endif

/// readFile, readTextFile and readFilePart: checks that `path` is a regular file, then reads `part` of it.
template <FileBuffer Buffer>
[[nodiscard]] auto readCheckedFile(const std::filesystem::path& path, FilePart part = {})
    -> Result<Buffer, FileError> {
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
    return readRegularFile<Buffer>(path, part);
}

} // namespace

auto existingFolders(std::span<const std::filesystem::path> candidates)
    -> std::vector<std::filesystem::path> {
    std::vector<std::filesystem::path> folders;
    for (const std::filesystem::path& candidate : candidates) {
        std::error_code error;
        if (!std::filesystem::is_directory(candidate, error)) {
            continue;
        }
        const bool isKept = std::ranges::any_of(folders, [&candidate](const std::filesystem::path& folder) {
            std::error_code ignored; // folders that cannot be compared are different folders
            return std::filesystem::equivalent(folder, candidate, ignored);
        });
        if (!isKept) {
            folders.push_back(candidate);
        }
    }
    return folders;
}

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
    return readCheckedFile<std::vector<std::byte>>(path);
}

auto readTextFile(const std::filesystem::path& path) -> Result<std::string, FileError> {
    return readCheckedFile<std::string>(path);
}

auto readFilePart(const std::filesystem::path& path, std::uint64_t offset, std::size_t size)
    -> Result<std::vector<std::byte>, FileError> {
    return readCheckedFile<std::vector<std::byte>>(path, {.offset = offset, .size = size});
}

auto writeFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
    -> Result<void, FileError> {
    std::error_code error;
    const std::filesystem::file_status status = std::filesystem::status(path, error);
    if (status.type() != std::filesystem::file_type::not_found) {
        if (error) {
            return fail(classify(error, FileError::WriteFailed), osFailureText(path, error));
        }
        if (!std::filesystem::is_regular_file(status)) {
            return fail(FileError::NotAFile, toUtf8(path));
        }
    }
    std::filesystem::path partial = path;
    partial += kPartialSuffix;
    std::error_code ignored; // a partial file left behind is replaced by the next write
    if (auto written = writeRegularFile(partial, bytes); !written) {
        std::filesystem::remove(partial, ignored);
        return written;
    }
    std::filesystem::rename(partial, path, error);
    if (error) {
        std::filesystem::remove(partial, ignored);
        return fail(classify(error, FileError::WriteFailed), osFailureText(path, error));
    }
    return {};
}

auto createDirectories(const std::filesystem::path& path) -> Result<void, FileError> {
    std::error_code error;
    std::filesystem::create_directories(path, error);
    std::error_code ignored;
    if (std::filesystem::is_directory(path, ignored)) {
        return {};
    }
    if (!error || error == std::errc::file_exists || error == std::errc::not_a_directory) {
        return fail(FileError::NotADirectory, toUtf8(path)); // a file where it, or one above it, goes
    }
    if (error == std::errc::permission_denied || error == std::errc::operation_not_permitted) {
        return fail(FileError::AccessDenied, osFailureText(path, error));
    }
    return fail(FileError::WriteFailed, osFailureText(path, error));
}

#if defined(_WIN32)

auto userDirectory() -> Result<std::filesystem::path, FileError> {
    PWSTR folder = nullptr;
    const HRESULT found = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_DEFAULT, nullptr, &folder);
    const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned{folder, &CoTaskMemFree};
    if (FAILED(found)) {
        return fail(FileError::NotFound, std::format("the user's roaming application data (error {:#x})",
                                                     static_cast<std::uint32_t>(found)));
    }
    return std::filesystem::path{folder} / kUserFolder;
}

auto fontFolders() -> std::vector<std::filesystem::path> {
    const auto known = [](REFKNOWNFOLDERID id) -> std::optional<std::filesystem::path> {
        PWSTR folder = nullptr;
        const HRESULT found = SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &folder);
        const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned{folder, &CoTaskMemFree};
        return FAILED(found) ? std::nullopt : std::optional{std::filesystem::path{folder}};
    };
    std::vector<std::filesystem::path> candidates;
    if (auto system = known(FOLDERID_Fonts)) {
        candidates.push_back(std::move(*system));
    }
    if (auto local = known(FOLDERID_LocalAppData)) {
        candidates.push_back(*local / "Microsoft" / "Windows" / "Fonts"); // the user's own, since Windows 10
    }
    return existingFolders(candidates);
}

auto executablePath() -> Result<std::filesystem::path, FileError> {
    std::wstring path(MAX_PATH, L'\0');
    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, path.data(), narrow<DWORD>(path.size()));
        if (length == 0) {
            const std::error_code error = lastError();
            return fail(FileError::ReadFailed,
                        std::format("the path of the running executable: {} (error {})", error.message(),
                                    error.value()));
        }
        if (length < path.size()) {
            path.resize(length);
            return std::filesystem::path{std::move(path)};
        }
        constexpr std::size_t kGrowth = 2;
        path.resize(path.size() * kGrowth); // GetModuleFileNameW cut it to the buffer
    }
}

#else

auto userDirectory() -> Result<std::filesystem::path, FileError> {
    // XDG Base Directory: a relative $XDG_CONFIG_HOME is invalid and ignored.
    if (const char* configuration = std::getenv("XDG_CONFIG_HOME");
        configuration != nullptr && *configuration == '/') {
        return fromUtf8(configuration) / kUserFolder;
    }
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return fail(FileError::NotFound, "the user's folders: neither XDG_CONFIG_HOME nor HOME is set");
    }
    return fromUtf8(home) / ".config" / kUserFolder;
}

auto fontFolders() -> std::vector<std::filesystem::path> {
    // XDG Base Directory: $XDG_DATA_HOME, then each of $XDG_DATA_DIRS (relative ones are invalid and
    // ignored), each with its fonts folder; and the older ~/.fonts.
    const auto isAbsolute = [](const char* value) {
        return value != nullptr && *value == '/';
    };
    const char* home = std::getenv("HOME");
    std::vector<std::filesystem::path> candidates;
    if (const char* data = std::getenv("XDG_DATA_HOME"); isAbsolute(data)) {
        candidates.push_back(fromUtf8(data) / "fonts");
    } else if (isAbsolute(home)) {
        candidates.push_back(fromUtf8(home) / ".local" / "share" / "fonts");
    }
    if (isAbsolute(home)) {
        candidates.push_back(fromUtf8(home) / ".fonts");
    }
    const char* shared = std::getenv("XDG_DATA_DIRS");
    const std::string_view folders =
        shared != nullptr && *shared != '\0' ? std::string_view{shared} : "/usr/local/share:/usr/share";
    for (const auto folder : folders | std::views::split(':')) {
        const std::string_view text{folder.begin(), folder.end()};
        if (text.starts_with('/')) {
            candidates.push_back(fromUtf8(text) / "fonts");
        }
    }
    return existingFolders(candidates);
}

auto executablePath() -> Result<std::filesystem::path, FileError> {
    const std::filesystem::path link{"/proc/self/exe"};
    std::error_code error;
    std::filesystem::path path = std::filesystem::read_symlink(link, error);
    if (error) {
        return fail(FileError::ReadFailed, osFailureText(link, error));
    }
    return path;
}

#endif

} // namespace nx::platform
