// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

#pragma once

// Test helper: a fresh directory for the files a test reads, removed with them at the end of its
// scope. Include after <gtest/gtest.h> and `import std;`.

namespace nx::test {

class TemporaryDirectory {
public:
    TemporaryDirectory()
        : m_path{std::filesystem::temp_directory_path() /
                 std::format("nxTest-{:08x}{:08x}", std::random_device{}(), std::random_device{}())} {
        std::error_code error;
        std::filesystem::create_directories(m_path, error);
        EXPECT_FALSE(error) << "cannot create " << m_path.string() << ": " << error.message();
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(m_path, error);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory(TemporaryDirectory&&) = delete;
    auto operator=(const TemporaryDirectory&) -> TemporaryDirectory& = delete;
    auto operator=(TemporaryDirectory&&) -> TemporaryDirectory& = delete;

    [[nodiscard]] auto path() const noexcept -> const std::filesystem::path& { return m_path; }

    /// Creates the directory at `relative` ("maps") and its parents.
    void makeDirectory(const std::filesystem::path& relative) const {
        std::error_code error;
        std::filesystem::create_directories(m_path / relative, error);
        EXPECT_FALSE(error) << "cannot create " << (m_path / relative).string() << ": " << error.message();
    }

    /// Writes `content` to the file at `relative` ("maps/x.bsp"), creating its directories.
    void write(const std::filesystem::path& relative, std::string_view content) const {
        makeDirectory(relative.parent_path());
        const std::filesystem::path file = m_path / relative;
        std::ofstream stream{file, std::ios::binary};
        stream.write(content.data(), static_cast<std::streamsize>(content.size()));
        EXPECT_TRUE(stream.good()) << "cannot write " << file.string();
    }

private:
    std::filesystem::path m_path;
};

} // namespace nx::test
