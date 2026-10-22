// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;

namespace cs {

auto soundProblem(const GameSound& sound) -> std::optional<std::string_view> {
    if (sound.files.empty()) {
        return "makes a sound without a file";
    }
    if (sound.pitch && sound.pitch->least > sound.pitch->most) {
        return "makes a sound whose least pitch is above its most";
    }
    return std::nullopt;
}

} // namespace cs
