// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module cs.shared;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.platform;

namespace cs {

namespace {

/// Where the languages' files are in the game's data, and what they end in.
constexpr std::string_view kGameFolder = "cstrike";
constexpr std::string_view kLanguagesFolder = "locale";
constexpr std::string_view kLanguageExtension = ".toml";
/// What stands between the parts of a language tag.
constexpr char kTagSeparator = '-';

[[nodiscard]] constexpr auto isTagLetter(char character) noexcept -> bool {
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9');
}

[[nodiscard]] auto languagesFolderOf(const std::filesystem::path& data) -> std::filesystem::path {
    return data / kGameFolder / kLanguagesFolder;
}

/// The name of the first text of `part` that is empty; none when each is written.
template <class Part>
[[nodiscard]] auto firstEmptyTextOf(const Part& part) -> std::optional<std::string_view> {
    std::optional<std::string_view> empty;
    nx::meta::forEachField<Part>([&]<class Field>(Field) {
        if (!empty && Field::get(part).empty()) {
            empty = Field::kName;
        }
    });
    return empty;
}

} // namespace

auto isLanguageTag(std::string_view language) noexcept -> bool {
    const auto isPart = [](auto part) {
        return !part.empty() && std::ranges::all_of(part, isTagLetter);
    };
    return !language.empty() && std::ranges::all_of(language | std::views::split(kTagSeparator), isPart);
}

auto readTexts(std::string_view text, std::string_view sourceName) -> nx::Result<Texts, TextsError> {
    auto texts = nx::data::parseToml<Texts>(text, sourceName);
    if (!texts) {
        return nx::fail(TextsError::DataInvalid, std::string{sourceName}, std::move(texts.error()));
    }
    // Each part by the name of its table.
    const std::array emptyTexts{std::pair{std::string_view{"mainMenu"}, firstEmptyTextOf(texts->mainMenu)},
                                std::pair{std::string_view{"options"}, firstEmptyTextOf(texts->options)}};
    for (const auto& [part, empty] : emptyTexts) {
        if (empty) {
            return nx::fail(TextsError::DataInvalid,
                            std::format("{}: {}.{} is empty", sourceName, part, *empty));
        }
    }
    return std::move(*texts);
}

auto languagesOf(const std::filesystem::path& data) -> nx::Result<std::vector<std::string>, TextsError> {
    auto files = nx::platform::listFiles(languagesFolderOf(data));
    if (!files) {
        return nx::fail(TextsError::UnknownLanguage, std::move(files.error()));
    }
    std::vector<std::string> languages;
    for (const std::filesystem::path& file : *files) {
        const std::string language = nx::platform::toUtf8(file.stem());
        if (!file.has_parent_path() && file.extension() == kLanguageExtension && isLanguageTag(language)) {
            languages.push_back(language);
        }
    }
    std::ranges::sort(languages);
    return languages;
}

auto openTexts(const std::filesystem::path& data, std::string_view language)
    -> nx::Result<Texts, TextsError> {
    auto languages = languagesOf(data);
    if (!languages) {
        return std::unexpected{std::move(languages.error())};
    }
    if (!isLanguageTag(language) || !std::ranges::contains(*languages, language)) {
        return nx::fail(TextsError::UnknownLanguage,
                        std::format("{:?} is none of the game's languages: {}", language, *languages));
    }
    const std::filesystem::path file =
        languagesFolderOf(data) / std::format("{}{}", language, kLanguageExtension);
    auto text = nx::platform::readTextFile(file);
    if (!text) {
        return nx::fail(TextsError::UnknownLanguage, std::string{language}, std::move(text.error()));
    }
    return readTexts(*text, nx::platform::toUtf8(file));
}

} // namespace cs
