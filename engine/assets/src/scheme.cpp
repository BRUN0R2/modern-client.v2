// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.assets;

import std;
import nx.core;

namespace nx::assets {

namespace {

/// The words of `text`, split at whitespace.
[[nodiscard]] auto wordsOf(std::string_view text) -> std::vector<std::string_view> {
    std::vector<std::string_view> words;
    for (const auto part : std::views::split(text, ' ')) {
        const std::string_view word{part};
        if (!word.empty()) {
            words.push_back(word);
        }
    }
    return words;
}

/// `word` as a whole number of `Number`, in `base`; none for anything else.
template <class Number>
[[nodiscard]] auto numberOf(std::string_view word, int base = 10) noexcept -> std::optional<Number> {
    Number value{};
    const auto [end, error] = std::from_chars(word.data(), word.data() + word.size(), value, base);
    return error == std::errc{} && end == word.data() + word.size() && !word.empty() ? std::optional{value}
                                                                                     : std::nullopt;
}

/// The `count` whole numbers `text` writes; none when it writes another count, or other words.
template <class Number>
[[nodiscard]] auto numbersOf(std::string_view text, std::size_t count) -> std::optional<std::vector<Number>> {
    const std::vector<std::string_view> words = wordsOf(text);
    if (words.size() != count) {
        return std::nullopt;
    }
    std::vector<Number> numbers;
    for (const std::string_view word : words) {
        const std::optional<Number> number = numberOf<Number>(word);
        if (!number) {
            return std::nullopt;
        }
        numbers.push_back(*number);
    }
    return numbers;
}

/// `word` as a hexadecimal number written "0x0000".
[[nodiscard]] auto hexadecimalOf(std::string_view word) noexcept -> std::optional<std::uint32_t> {
    if (!word.starts_with("0x") && !word.starts_with("0X")) {
        return std::nullopt;
    }
    constexpr int kHexadecimal = 16;
    return numberOf<std::uint32_t>(word.substr(2), kHexadecimal);
}

/// The names VGUI2 gives a font's flags.
constexpr EnumArray<FontFlag, std::string_view> kFlagNames{EnumArray<FontFlag, std::string_view>::Storage{
    "antialias", "underline", "italic", "dropshadow", "outline", "symbol", "additive", "rotary"}};

/// The names of a border's sides.
constexpr EnumArray<BorderSide, std::string_view> kSideNames{
    EnumArray<BorderSide, std::string_view>::Storage{"Left", "Right", "Top", "Bottom"}};

/// The entries of `settings` and of the blocks within it, named from `prefix`.
void flattenSettings(const KeyValuesEntry& settings, std::string_view prefix,
                     std::vector<SchemeSetting>& found) {
    for (const KeyValuesEntry& entry : settings.entries) {
        std::string name = prefix.empty() ? entry.key : std::format("{}.{}", prefix, entry.key);
        if (entry.isBlock) {
            flattenSettings(entry, name, found);
        } else {
            found.push_back({.name = std::move(name), .value = entry.word});
        }
    }
}

[[nodiscard]] auto readCandidate(std::string_view font, const KeyValuesEntry& fields)
    -> Result<FontCandidate, SchemeError> {
    FontCandidate candidate;
    const auto bad = [font, &fields](std::string_view what) {
        return fail(SchemeError::BadFont,
                    std::format("font {:?}, candidate {:?}: {}", font, fields.key, what));
    };
    if (!fields.isBlock) {
        return bad("not a block");
    }
    for (const KeyValuesEntry& field : fields.entries) {
        if (field.isBlock) {
            continue; // nothing VGUI2 reads
        }
        const std::string_view value = field.word;
        if (equalsIgnoringCase(field.key, "name")) {
            candidate.face = value;
        } else if (equalsIgnoringCase(field.key, "yres")) {
            const auto heights = numbersOf<std::uint32_t>(value, 2);
            if (!heights) {
                return bad(std::format("yres {:?}", value));
            }
            candidate.heights = SchemeRange<std::uint32_t>{.first = (*heights)[0], .last = (*heights)[1]};
        } else if (equalsIgnoringCase(field.key, "range")) {
            const std::vector<std::string_view> words = wordsOf(value);
            const std::optional<std::uint32_t> first =
                words.size() == 2 ? hexadecimalOf(words[0]) : std::nullopt;
            const std::optional<std::uint32_t> last =
                words.size() == 2 ? hexadecimalOf(words[1]) : std::nullopt;
            if (!first || !last) {
                return bad(std::format("range {:?}", value));
            }
            candidate.characters = SchemeRange<char32_t>{.first = static_cast<char32_t>(*first),
                                                         .last = static_cast<char32_t>(*last)};
        } else {
            std::int32_t* number = equalsIgnoringCase(field.key, "tall")        ? &candidate.tall
                                   : equalsIgnoringCase(field.key, "weight")    ? &candidate.weight
                                   : equalsIgnoringCase(field.key, "blur")      ? &candidate.blur
                                   : equalsIgnoringCase(field.key, "scanlines") ? &candidate.scanLines
                                                                                : nullptr;
            std::optional<FontFlag> flag;
            for (const FontFlag which : EnumArray<FontFlag, std::string_view>::keys()) {
                if (equalsIgnoringCase(field.key, kFlagNames[which])) {
                    flag = which;
                }
            }
            if (number == nullptr && !flag) {
                continue; // a key VGUI2 does not know
            }
            const std::optional<std::int32_t> parsed = numberOf<std::int32_t>(value);
            if (!parsed) {
                return bad(std::format("{} {:?}", field.key, value));
            }
            if (number != nullptr) {
                *number = *parsed;
            } else if (*parsed != 0) {
                candidate.flags.set(*flag);
            }
        }
    }
    return candidate;
}

[[nodiscard]] auto readBorder(const KeyValuesEntry& entry) -> Result<SchemeBorder, SchemeError> {
    SchemeBorder border{.name = entry.key};
    if (!entry.isBlock) {
        border.sameAs = entry.word;
        return border;
    }
    const auto bad = [&entry](std::string_view what) {
        return fail(SchemeError::BadBorder, std::format("border {:?}: {}", entry.key, what));
    };
    if (const std::optional<std::string_view> inset = entry.findWord("inset")) {
        const auto numbers = numbersOf<std::int32_t>(*inset, border.inset.size());
        if (!numbers) {
            return bad(std::format("inset {:?}", *inset));
        }
        std::ranges::copy(*numbers, border.inset.begin());
    }
    for (const BorderSide side : EnumArray<BorderSide, std::string_view>::keys()) {
        const KeyValuesEntry* lines = entry.find(kSideNames[side]);
        if (lines == nullptr || !lines->isBlock) {
            continue;
        }
        for (const KeyValuesEntry& line : lines->entries) {
            const std::optional<std::string_view> color = line.findWord("color");
            if (!line.isBlock || !color) {
                return bad(std::format("{} line {:?} without a color", kSideNames[side], line.key));
            }
            BorderLine made{.color = std::string{*color}};
            if (const std::optional<std::string_view> offset = line.findWord("offset")) {
                const auto numbers = numbersOf<std::int32_t>(*offset, 2);
                if (!numbers) {
                    return bad(std::format("{} line {:?}: offset {:?}", kSideNames[side], line.key, *offset));
                }
                made.startOffset = (*numbers)[0];
                made.endOffset = (*numbers)[1];
            }
            border.sides[side].push_back(std::move(made));
        }
    }
    return border;
}

} // namespace

auto schemeColorOf(std::string_view text) noexcept -> std::optional<SchemeColor> {
    std::array<std::uint8_t, 4> channels{0, 0, 0, std::numeric_limits<std::uint8_t>::max()};
    std::size_t count = 0;
    for (const auto part : std::views::split(text, ' ')) {
        const std::string_view word{part};
        if (word.empty()) {
            continue;
        }
        const std::optional<std::uint8_t> channel =
            count < channels.size() ? numberOf<std::uint8_t>(word) : std::nullopt;
        if (!channel) {
            return std::nullopt;
        }
        channels[count++] = *channel;
    }
    if (count != 3 && count != 4) {
        return std::nullopt;
    }
    return SchemeColor{.red = channels[0], .green = channels[1], .blue = channels[2], .alpha = channels[3]};
}

auto Scheme::setting(std::string_view name) const noexcept -> std::optional<std::string_view> {
    const auto found = std::ranges::find_if(
        settings, [name](const SchemeSetting& each) { return equalsIgnoringCase(each.name, name); });
    return found == settings.end() ? std::nullopt : std::optional<std::string_view>{found->value};
}

auto Scheme::color(std::string_view name) const -> std::optional<SchemeColor> {
    const std::string_view text = setting(name).value_or(name);
    if (const std::optional<SchemeColor> literal = schemeColorOf(text)) {
        return literal;
    }
    const auto found = std::ranges::find_if(
        colors, [text](const NamedColor& named) { return equalsIgnoringCase(named.name, text); });
    return found == colors.end() ? std::nullopt : std::optional{found->color};
}

auto Scheme::font(std::string_view name) const noexcept -> const SchemeFont* {
    const auto found = std::ranges::find_if(
        fonts, [name](const SchemeFont& each) { return equalsIgnoringCase(each.name, name); });
    return found == fonts.end() ? nullptr : &*found;
}

auto Scheme::border(std::string_view name) const noexcept -> const SchemeBorder* {
    const auto named = [this](std::string_view wanted) -> const SchemeBorder* {
        const auto found = std::ranges::find_if(
            borders, [wanted](const SchemeBorder& each) { return equalsIgnoringCase(each.name, wanted); });
        return found == borders.end() ? nullptr : &*found;
    };
    const SchemeBorder* reached = named(name);
    // readScheme refused names of borders that do not exist and loops: borders.size() steps reach one
    // with lines of its own.
    for (std::size_t step = 0; reached != nullptr && !reached->sameAs.empty() && step < borders.size();
         ++step) {
        reached = named(reached->sameAs);
    }
    return reached;
}

auto readScheme(std::span<const KeyValuesEntry> entries) -> Result<Scheme, SchemeError> {
    const auto root = std::ranges::find_if(entries, [](const KeyValuesEntry& entry) {
        return entry.isBlock && equalsIgnoringCase(entry.key, "Scheme");
    });
    if (root == entries.end()) {
        return fail(SchemeError::NotAScheme, "no Scheme block");
    }
    Scheme scheme;
    if (const KeyValuesEntry* colors = root->find("Colors"); colors != nullptr) {
        for (const KeyValuesEntry& entry : colors->entries) {
            const std::optional<SchemeColor> color = entry.isBlock ? std::nullopt : schemeColorOf(entry.word);
            if (!color) {
                return fail(SchemeError::BadColor, std::format("{:?}: {:?}", entry.key, entry.word));
            }
            scheme.colors.push_back({.name = entry.key, .color = *color});
        }
    }
    if (const KeyValuesEntry* settings = root->find("BaseSettings"); settings != nullptr) {
        flattenSettings(*settings, {}, scheme.settings);
    }
    if (const KeyValuesEntry* fonts = root->find("Fonts"); fonts != nullptr) {
        for (const KeyValuesEntry& entry : fonts->entries) {
            SchemeFont font{.name = entry.key};
            for (const KeyValuesEntry& fields : entry.entries) {
                auto candidate = readCandidate(entry.key, fields);
                if (!candidate) {
                    return std::unexpected{std::move(candidate.error())};
                }
                font.candidates.push_back(std::move(*candidate));
            }
            scheme.fonts.push_back(std::move(font));
        }
    }
    if (const KeyValuesEntry* borders = root->find("Borders"); borders != nullptr) {
        for (const KeyValuesEntry& entry : borders->entries) {
            auto border = readBorder(entry);
            if (!border) {
                return std::unexpected{std::move(border.error())};
            }
            scheme.borders.push_back(std::move(*border));
        }
    }
    // A border named as another must lead, from name to name, to one with lines of its own.
    for (const SchemeBorder& border : scheme.borders) {
        const SchemeBorder* reached = scheme.border(border.name);
        if (reached == nullptr || !reached->sameAs.empty()) {
            return fail(SchemeError::BadBorder,
                        std::format("border {:?}: {:?} names no border with lines of its own", border.name,
                                    border.sameAs));
        }
    }
    if (const KeyValuesEntry* files = root->find("CustomFontFiles"); files != nullptr) {
        for (const KeyValuesEntry& entry : files->entries) {
            if (!entry.isBlock) {
                scheme.customFontFiles.push_back(entry.word);
            }
        }
    }
    return scheme;
}

} // namespace nx::assets
