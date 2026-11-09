// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The fonts the project's own menus write in (docs/12-ui-hud-and-console.md, section 4), as their data
/// names them.
export module cs.shared:menuFonts;

import std;
import nx.meta;

export namespace cs {

/// The font families a menu writes in, of the project's own fonts (data/fonts/): for its text, and for
/// what stands out in it.
struct MenuFontFamilies {
    std::string regular{};
    std::string strong{};

    /// Whether both are named.
    [[nodiscard]] inline auto isComplete() const noexcept -> bool {
        return !regular.empty() && !strong.empty();
    }
};

} // namespace cs

template <>
struct nx::meta::Description<cs::MenuFontFamilies> {
    static constexpr std::string_view kName = "cs::MenuFontFamilies";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::MenuFontFamilies::regular>("regular"),
        nx::meta::field<&cs::MenuFontFamilies::strong>("strong"),
    };
};
