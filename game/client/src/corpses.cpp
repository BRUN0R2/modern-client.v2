// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.client;

import std;
import nx.core;
import nx.console;

namespace cs {

auto CorpseCvars::registerIn(nx::console::Console& console)
    -> nx::Result<CorpseCvars, nx::console::CvarError> {
    auto stay = console.registerCvar(nx::console::CvarDesc<float>{
        .name = "cl_corpsestay",
        .defaultValue = kLongestCorpseStay.count(),
        .description = "The seconds a dead body stays, from 0 to 600, before it fades out.",
        .flags = {nx::console::CvarFlag::Archive}});
    if (!stay) {
        return std::unexpected{std::move(stay.error())};
    }
    return CorpseCvars{.stay = std::move(*stay)};
}

auto CorpseCvars::stayTime() const noexcept -> nx::Seconds {
    const float seconds = stay.value();
    if (!std::isfinite(seconds)) {
        return {};
    }
    return nx::Seconds{std::clamp(seconds, 0.0F, kLongestCorpseStay.count())};
}

auto corpseAmountOf(std::optional<nx::Seconds> dead, nx::Seconds stay) noexcept -> std::uint8_t {
    if (!dead || *dead <= stay) {
        return kWholeBody;
    }
    const float faded = (*dead - stay) / kCorpseFade;
    if (faded >= 1.0F) {
        return 0;
    }
    return static_cast<std::uint8_t>(std::lround(static_cast<float>(kWholeBody) * (1.0F - faded)));
}

} // namespace cs
