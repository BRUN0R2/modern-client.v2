// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;
import std;
import nx.core;
import nx.data;

namespace cs {

auto readDamageNumberLook(std::string_view text, std::string_view sourceName)
    -> nx::Result<DamageNumberLook, DamageNumberError> {
    auto look = nx::data::parseToml<DamageNumberLook>(text, sourceName);
    if (!look) {
        return nx::fail(DamageNumberError::DataInvalid, std::string{sourceName}, std::move(look.error()));
    }
    const auto positive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };
    const bool isValidLook = !look->font.family.empty() && positive(look->height) &&
                             positive(look->life.count()) && look->fadeIn + look->fadeOut <= look->life &&
                             look->growTime + look->settleTime <= look->life && positive(look->startScale) &&
                             positive(look->peakScale) && positive(look->endScale) &&
                             look->startScale <= look->peakScale && look->mostNumbers > 0;
    if (!isValidLook) {
        return nx::fail(DamageNumberError::DataInvalid,
                        std::format("{}: a font of a family, a line more than none, a life more than none "
                                    "holding its fades and its growing and settling, scales more than none, "
                                    "the start no larger than the peak, and room for a number are required",
                                    sourceName));
    }
    return std::move(*look);
}

} // namespace cs
