// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module;

#if defined(_WIN32)
#include <windows.h> // WIN32_LEAN_AND_MEAN and NOMINMAX come from engine/platform/CMakeLists.txt
#endif

module nx.platform;

import std;
import nx.core;

namespace nx::platform {

namespace {

#if defined(_WIN32)

constexpr const wchar_t* kSteamKey = L"Software\\Valve\\Steam";
constexpr const wchar_t* kSteamPathValue = L"SteamPath";

[[nodiscard]] auto candidateFolders() -> std::vector<std::filesystem::path> {
    DWORD bytes = 0;
    if (RegGetValueW(HKEY_CURRENT_USER, kSteamKey, kSteamPathValue, RRF_RT_REG_SZ, nullptr, nullptr,
                     &bytes) != ERROR_SUCCESS) {
        return {};
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    if (RegGetValueW(HKEY_CURRENT_USER, kSteamKey, kSteamPathValue, RRF_RT_REG_SZ, nullptr, value.data(),
                     &bytes) != ERROR_SUCCESS) {
        return {};
    }
    value.resize(bytes / sizeof(wchar_t) - 1); // without the terminator RRF_RT_REG_SZ guarantees
    return {std::filesystem::path{value}};
}

#else

[[nodiscard]] auto candidateFolders() -> std::vector<std::filesystem::path> {
    const char* home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return {};
    }
    const std::filesystem::path base = fromUtf8(home);
    return {base / ".steam/steam", base / ".local/share/Steam",
            base / ".var/app/com.valvesoftware.Steam/.local/share/Steam"};
}

#endif

} // namespace

auto steamFolders() -> std::vector<std::filesystem::path> {
    return existingFolders(candidateFolders());
}

} // namespace nx::platform
