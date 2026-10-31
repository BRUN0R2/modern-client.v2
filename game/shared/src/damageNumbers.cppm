// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The damage a player's hits deal, as its client shows it over what they hit, apart from how the hits
/// are dealt: the server's account of each hit, and how the numbers look (data/cstrike/damageNumbers.toml),
/// which cs.client:damageNumbers draws.
export module cs.shared:damageNumbers;

import std;
import nx.core;
import nx.math;
import nx.meta;
import nx.data;

export namespace cs {

/// Why data/cstrike/damageNumbers.toml could not be read.
enum class DamageNumberError : std::uint8_t {
    DataInvalid, ///< The file breaks its schema or its rules (cause: the DataError, when there is one).
};

/// What a hit of a player's dealt a body, which its client shows as a number: where it met the body, the
/// damage it dealt, and whether it met it in the head. Each pellet of a shot that hits is a hit of its own.
struct DamageDealt {
    nx::Vec3 position{};
    nx::Damage damage{};
    bool isHead = false;
};

/// A color of a number, of 255 each.
struct DamageNumberColor {
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
};

/// The system's font a number is written in, as a scheme asks for one: its family, its weight from 1 to
/// 1000, and whether it is italic.
struct DamageNumberFont {
    std::string family{};
    std::int32_t weight = 0;
    bool isItalic = false;
};

/// How the damage of a player's hits shows, the project's own, as action games show it: a number of each
/// hit (DamageDealt) over where it met the body, over everything else, walls and all, in `font`, its line
/// `height` of the screen's at its whole size, in `color`, a head's in `headColor`, outlined in
/// `outlineColor` `outline` of its line's height about its digits. It starts at `startScale` of its size,
/// fading in over `fadeIn`; grows to `peakScale` over `growTime`, easing out; settles back to its whole
/// size over `settleTime`; rises `rise` of the screen's height over its `life`, easing out, from where it
/// was drawn up to `spread` of the screen's height to a side; and over its last `fadeOut`, fades out and
/// shrinks to `endScale`. At most `mostNumbers` show at once, the oldest going first.
struct DamageNumberLook {
    DamageNumberFont font{};
    float height = 0.0F;
    DamageNumberColor color{};
    DamageNumberColor headColor{};
    DamageNumberColor outlineColor{};
    float outline = 0.0F;
    nx::Seconds life{};
    nx::Seconds fadeIn{};
    nx::Seconds growTime{};
    nx::Seconds settleTime{};
    nx::Seconds fadeOut{};
    float startScale = 0.0F;
    float peakScale = 1.0F;
    float endScale = 1.0F;
    float rise = 0.0F;
    float spread = 0.0F;
    std::uint32_t mostNumbers = 0;
};

/// Reads data/cstrike/damageNumbers.toml: a font of a family, a line more than none of the screen's
/// height, a life more than none, holding its fade in and its fade out, and its growing and settling,
/// scales more than none, the start no larger than the peak, and room for one number at least. Errors:
/// DataInvalid.
[[nodiscard]] auto readDamageNumberLook(std::string_view text, std::string_view sourceName)
    -> nx::Result<DamageNumberLook, DamageNumberError>;

} // namespace cs

template <>
struct nx::meta::Description<cs::DamageNumberColor> {
    static constexpr std::string_view kName = "cs::DamageNumberColor";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::DamageNumberColor::red>("red"),
        nx::meta::field<&cs::DamageNumberColor::green>("green"),
        nx::meta::field<&cs::DamageNumberColor::blue>("blue"),
    };
};

template <>
struct nx::meta::Description<cs::DamageNumberFont> {
    static constexpr std::string_view kName = "cs::DamageNumberFont";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::DamageNumberFont::family>("family"),
        nx::meta::field<&cs::DamageNumberFont::weight>("weight",
                                                       nx::data::Range{.minimum = 1.0, .maximum = 1000.0}),
        nx::meta::field<&cs::DamageNumberFont::isItalic>("isItalic"),
    };
};

template <>
struct nx::meta::Description<cs::DamageNumberLook> {
    static constexpr std::string_view kName = "cs::DamageNumberLook";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::DamageNumberLook::font>("font"),
        nx::meta::field<&cs::DamageNumberLook::height>("height",
                                                       nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::DamageNumberLook::color>("color"),
        nx::meta::field<&cs::DamageNumberLook::headColor>("headColor"),
        nx::meta::field<&cs::DamageNumberLook::outlineColor>("outlineColor"),
        nx::meta::field<&cs::DamageNumberLook::outline>("outline",
                                                        nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::DamageNumberLook::life>("life", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DamageNumberLook::fadeIn>("fadeIn", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DamageNumberLook::growTime>("growTime", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DamageNumberLook::settleTime>("settleTime", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DamageNumberLook::fadeOut>("fadeOut", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DamageNumberLook::startScale>("startScale", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DamageNumberLook::peakScale>("peakScale", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DamageNumberLook::endScale>("endScale", nx::data::Range{.minimum = 0.0}),
        nx::meta::field<&cs::DamageNumberLook::rise>("rise", nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::DamageNumberLook::spread>("spread",
                                                       nx::data::Range{.minimum = 0.0, .maximum = 1.0}),
        nx::meta::field<&cs::DamageNumberLook::mostNumbers>("mostNumbers"),
    };
};
