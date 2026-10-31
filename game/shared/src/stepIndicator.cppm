// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How the steps of the entities' bodies show around the middle of a player's screen
/// (data/cstrike/stepIndicator.toml), which cs.client:stepIndicator draws, apart from how the bodies step
/// (EntityStep).
export module cs.shared:stepIndicator;

import std;
import nx.core;
import nx.meta;
import nx.data;

export namespace cs {

/// Why data/cstrike/stepIndicator.toml could not be read.
enum class StepIndicatorError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// A color of the steps' marks, of 255 each.
struct StepMarkColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

/// How the steps of the entities' bodies show, the project's own, as games show the sounds a player hears:
/// a mark of footprints for each body heard stepping, on a ring `ringRadius` of the screen's height about
/// its middle, the way the body is from where the player looks (ahead at the top, behind at the bottom), its
/// toes toward it, `size` of the screen's height across, in `color`, `opacity` opaque at most. A step
/// lights its body's mark over `attack`, easing in from what it showed; it holds `hold`, then fades out
/// over `release`; each step pops the mark `pulseScale` larger, back over `pulseTime`. A body within
/// `nearDistance` shows wholly, fading out to none at `farDistance`, as far as a step is heard. At most
/// `mostShown` marks show, the loudest first, none closer to another than `separation` of their size; at
/// most `mostHeard` bodies are kept, the faintest giving way.
struct StepIndicatorLook {
    StepMarkColor color{};
    float opacity = 0.0F;
    float ringRadius = 0.0F;
    float size = 0.0F;
    nx::HammerUnits nearDistance{};
    nx::HammerUnits farDistance{};
    nx::Seconds attack{};
    nx::Seconds hold{};
    nx::Seconds release{};
    float pulseScale = 0.0F;
    nx::Seconds pulseTime{};
    float separation = 0.0F;
    std::uint32_t mostShown = 0;
    std::uint32_t mostHeard = 0;
};

/// Reads data/cstrike/stepIndicator.toml: an opacity, a ring and a size more than none, the near distance
/// less than the far one, an attack and a release more than none, a hold and a pulse none or more, and room
/// for a mark, no fewer bodies kept than marks shown. Errors: DataInvalid.
[[nodiscard]] auto readStepIndicatorLook(std::string_view text, std::string_view sourceName)
    -> nx::Result<StepIndicatorLook, StepIndicatorError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::StepMarkColor> {
    static constexpr std::string_view kName = "cs::StepMarkColor";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::StepMarkColor::red>("red"),
        nx::meta::field<&cs::StepMarkColor::green>("green"),
        nx::meta::field<&cs::StepMarkColor::blue>("blue"),
    };
};

template <>
struct nx::meta::Description<cs::StepIndicatorLook> {
    static constexpr std::string_view kName = "cs::StepIndicatorLook";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::StepIndicatorLook::color>("color"),
        nx::meta::field<&cs::StepIndicatorLook::opacity>("opacity",
                                                         nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::StepIndicatorLook::ringRadius>("ringRadius",
                                                            nx::data::Range{.minimum = 0.0, .maximum = 0.5}),
        nx::meta::field<&cs::StepIndicatorLook::size>("size",
                                                      nx::data::Range{.minimum = 0.0, .maximum = 0.5}),
        nx::meta::field<&cs::StepIndicatorLook::nearDistance>("nearDistance",
                                                              nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::StepIndicatorLook::farDistance>("farDistance", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::StepIndicatorLook::attack>("attack", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::StepIndicatorLook::hold>("hold", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::StepIndicatorLook::release>("release", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::StepIndicatorLook::pulseScale>("pulseScale", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::StepIndicatorLook::pulseTime>("pulseTime", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::StepIndicatorLook::separation>("separation", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::StepIndicatorLook::mostShown>("mostShown"),
        nx::meta::field<&cs::StepIndicatorLook::mostHeard>("mostHeard"),
    };
};
