// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;
import std;
import nx.core;
import nx.data;

namespace cs {

auto readStepIndicatorLook(std::string_view text, std::string_view sourceName)
    -> nx::Result<StepIndicatorLook, StepIndicatorError> {
    auto look = nx::data::parseToml<StepIndicatorLook>(text, sourceName);
    if (!look) {
        return nx::fail(StepIndicatorError::DataInvalid, std::string{sourceName}, std::move(look.error()));
    }
    const auto positive = [](float value) {
        return std::isfinite(value) && value > 0.0F;
    };
    const bool isValidLook = positive(look->opacity) && positive(look->ringRadius) && positive(look->size) &&
                             look->nearDistance < look->farDistance && positive(look->attack.count()) &&
                             positive(look->release.count()) && look->mostShown > 0 &&
                             look->mostHeard >= look->mostShown;
    if (!isValidLook) {
        return nx::fail(
            StepIndicatorError::DataInvalid,
            std::format("{}: an opacity, a ring and a size more than none, the near distance less "
                        "than the far one, an attack and a release more than none, and room for a "
                        "mark, no fewer bodies kept than marks shown, are required",
                        sourceName));
    }
    return std::move(*look);
}

} // namespace cs
