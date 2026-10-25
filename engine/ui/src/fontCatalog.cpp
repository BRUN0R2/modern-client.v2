// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module nx.ui;

import std;
import nx.core;
import nx.platform;

namespace nx::ui {

namespace {

/// How much heavier than asked a face may be and still be chosen: GDI takes Tahoma Bold, Verdana Bold,
/// Arial Bold and Trebuchet MS Bold (700) from a weight of 612.
constexpr std::int32_t kHeavierReach = 88;
/// Bold is simulated on a face lighter than this...
constexpr std::int32_t kSimulatedBoldBelow = 600;
/// ...more than this lighter than asked: GDI emboldens Tahoma (400) from 551, Marlett (500) from 651.
constexpr std::int32_t kSimulatedBoldGap = 150;
/// The weight asked for by 0 (FW_DONTCARE).
constexpr std::int32_t kRegularWeight = 400;

/// The most faces of a collection, and tables of a face, read: far more than any font has.
constexpr std::uint32_t kMaxFaces = 64;
constexpr std::uint16_t kMaxTables = 256;
/// The bytes of a face's header and its table records, of a collection's header and its offsets.
constexpr std::size_t kFaceHeaderSize = 12;
constexpr std::size_t kTableRecordSize = 16;
constexpr std::size_t kCollectionHeaderSize = 12;
/// Where the fields read are in the OpenType structures (the OpenType specification): in a face's
/// header (sfntVersion, numTables), in a table record (tableTag, checksum, offset, length), in a
/// collection's header (ttcTag, version, numFonts, then an offset to each face).
constexpr std::size_t kTableCountAt = 4;
constexpr std::size_t kTableOffsetAt = 8;
constexpr std::size_t kTableLengthAt = 12;
constexpr std::size_t kFaceCountAt = 8;
/// A tag's bytes, and a collection's offset to a face's.
constexpr std::size_t kTagBytes = sizeof(std::uint32_t);
constexpr std::size_t kFaceOffsetBytes = sizeof(std::uint32_t);
/// The most bytes of a name table read: names are short.
constexpr std::size_t kMaxNameTableSize = std::size_t{1} << 20;

/// The longest family or style name GDI takes, in UTF-16 units: its LOGFONT holds 32 with the
/// terminator (LF_FACESIZE). A longer one names no face a scheme can ask for.
constexpr std::size_t kMaxNameUnits = 31;
/// The most family names a face keeps, one a language: far more than any font gives.
constexpr std::size_t kMaxFamilyNames = 32;

/// The tag of a table, as the four letters a directory lists it by.
[[nodiscard]] constexpr auto tagOf(std::string_view letters) noexcept -> std::uint32_t {
    std::uint32_t tag = 0;
    for (const char letter : letters) {
        tag = (tag << kBitsPerByte) | static_cast<std::uint8_t>(letter);
    }
    return tag;
}

/// What a font's bytes are read through: `size` bytes from `offset`, fewer where the file ends.
template <class Read>
concept FontBytes = requires(const Read& read, std::uint64_t offset, std::size_t size) {
    { read(offset, size) } -> std::same_as<std::vector<std::byte>>;
};

/// The text of a name record of the Windows or Unicode platforms: UTF-16 big-endian, in UTF-8.
[[nodiscard]] auto utf16Name(std::span<const std::byte> bytes) -> std::string {
    std::string text;
    for (std::size_t at = 0; at + 1 < bytes.size(); at += 2) {
        auto unit = static_cast<char32_t>(loadBigEndian<std::uint16_t>(bytes.subspan(at)));
        if (isHighSurrogate(unit) && at + 3 < bytes.size()) {
            const auto low = static_cast<char32_t>(loadBigEndian<std::uint16_t>(bytes.subspan(at + 2)));
            if (isLowSurrogate(low)) {
                unit = characterOfSurrogates(unit, low);
                at += 2;
            }
        }
        appendUtf8(text, isSurrogate(unit) ? kReplacementCharacter : unit);
    }
    return text;
}

/// The families (name ID 1, English first) and the style (name ID 2, English when there is one) of a
/// name table, from the records of the Windows and Unicode platforms.
void readNames(std::span<const std::byte> table, FontFace& face) {
    // The header (version, count, storageOffset), then the records (platformID, encodingID,
    // languageID, nameID, length, stringOffset), of 16-bit fields; the names in UTF-16.
    constexpr std::size_t kHeaderSize = 6;
    constexpr std::size_t kRecordSize = 12;
    constexpr std::size_t kCountAt = 2;
    constexpr std::size_t kStorageAt = 4;
    constexpr std::size_t kLanguageAt = 4;
    constexpr std::size_t kNameIdAt = 6;
    constexpr std::size_t kLengthAt = 8;
    constexpr std::size_t kStringAt = 10;
    constexpr std::size_t kUnitBytes = sizeof(std::uint16_t);
    constexpr std::uint16_t kUnicodePlatform = 0;
    constexpr std::uint16_t kWindowsPlatform = 3;
    constexpr std::uint16_t kEnglish = 0x0409;
    constexpr std::uint16_t kFamilyName = 1;
    constexpr std::uint16_t kStyleName = 2;
    if (table.size() < kHeaderSize) {
        return;
    }
    const std::size_t count = loadBigEndian<std::uint16_t>(table.subspan(kCountAt));
    const std::size_t strings = loadBigEndian<std::uint16_t>(table.subspan(kStorageAt));
    bool hasEnglishStyle = false;
    for (std::size_t index = 0; index < count && kHeaderSize + ((index + 1) * kRecordSize) <= table.size();
         ++index) {
        const std::span<const std::byte> record =
            table.subspan(kHeaderSize + (index * kRecordSize), kRecordSize);
        const auto platform = loadBigEndian<std::uint16_t>(record);
        const auto language = loadBigEndian<std::uint16_t>(record.subspan(kLanguageAt));
        const auto id = loadBigEndian<std::uint16_t>(record.subspan(kNameIdAt));
        const std::size_t length = loadBigEndian<std::uint16_t>(record.subspan(kLengthAt));
        const std::size_t offset = strings + loadBigEndian<std::uint16_t>(record.subspan(kStringAt));
        if ((platform != kWindowsPlatform && platform != kUnicodePlatform) ||
            (id != kFamilyName && id != kStyleName) || offset > table.size() ||
            length > table.size() - offset || length > kUnitBytes * kMaxNameUnits) {
            continue;
        }
        std::string text = utf16Name(table.subspan(offset, length));
        const bool isEnglish = platform == kWindowsPlatform && language == kEnglish;
        if (id == kStyleName) {
            if (face.style.empty() || (isEnglish && !hasEnglishStyle)) {
                face.style = std::move(text);
                hasEnglishStyle = isEnglish;
            }
        } else if (!text.empty() && std::ranges::find(face.families, text) == face.families.end()) {
            // English first, the others while there is room.
            if (isEnglish) {
                face.families.insert(face.families.begin(), std::move(text));
                if (face.families.size() > kMaxFamilyNames) {
                    face.families.pop_back();
                }
            } else if (face.families.size() < kMaxFamilyNames) {
                face.families.push_back(std::move(text));
            }
        }
    }
}

/// The face whose table records start at `offset`, read through `read`; none when it names no family.
template <FontBytes Read>
[[nodiscard]] auto readFace(const Read& read, std::uint64_t offset, const std::filesystem::path& file,
                            std::uint32_t faceIndex) -> std::optional<FontFace> {
    const std::vector<std::byte> header = read(offset, kFaceHeaderSize);
    if (header.size() < kFaceHeaderSize) {
        return std::nullopt;
    }
    const auto tableCount =
        std::min(loadBigEndian<std::uint16_t>(std::span{header}.subspan(kTableCountAt)), kMaxTables);
    const std::vector<std::byte> records =
        read(offset + kFaceHeaderSize, std::size_t{tableCount} * kTableRecordSize);
    const auto tableOf = [&](std::string_view letters, std::size_t limit) -> std::vector<std::byte> {
        for (std::size_t at = 0; at + kTableRecordSize <= records.size(); at += kTableRecordSize) {
            const std::span<const std::byte> record = std::span{records}.subspan(at, kTableRecordSize);
            if (loadBigEndian<std::uint32_t>(record) == tagOf(letters)) {
                return read(loadBigEndian<std::uint32_t>(record.subspan(kTableOffsetAt)),
                            std::min<std::size_t>(
                                loadBigEndian<std::uint32_t>(record.subspan(kTableLengthAt)), limit));
            }
        }
        return {};
    };

    FontFace face{.file = file, .faceIndex = faceIndex};
    readNames(tableOf("name", kMaxNameTableSize), face);
    if (face.families.empty()) {
        return std::nullopt;
    }
    // The weight and italics of the OS/2 table (usWeightClass, fsSelection), else of head's macStyle.
    constexpr std::size_t kWeightOffset = 4;
    constexpr std::size_t kSelectionOffset = 62;
    constexpr std::uint16_t kItalicSelection = 0x0001;
    constexpr std::size_t kMacStyleOffset = 44;
    constexpr std::uint16_t kBoldStyle = 0x0001;
    constexpr std::uint16_t kItalicStyle = 0x0002;
    constexpr std::uint16_t kBoldWeight = 700;
    constexpr std::uint16_t kHeaviest = 1000;
    constexpr std::size_t kWindowsBytes = kSelectionOffset + sizeof(std::uint16_t);
    constexpr std::size_t kHeadBytes = kMacStyleOffset + sizeof(std::uint16_t);
    if (const std::vector<std::byte> windows = tableOf("OS/2", kWindowsBytes);
        windows.size() == kWindowsBytes) {
        face.weight = std::clamp(loadBigEndian<std::uint16_t>(std::span{windows}.subspan(kWeightOffset)),
                                 std::uint16_t{1}, kHeaviest);
        face.isItalic = (loadBigEndian<std::uint16_t>(std::span{windows}.subspan(kSelectionOffset)) &
                         kItalicSelection) != 0;
    } else if (const std::vector<std::byte> head = tableOf("head", kHeadBytes); head.size() == kHeadBytes) {
        const auto style = loadBigEndian<std::uint16_t>(std::span{head}.subspan(kMacStyleOffset));
        face.weight = (style & kBoldStyle) != 0 ? kBoldWeight : std::uint16_t{kRegularWeight};
        face.isItalic = (style & kItalicStyle) != 0;
    }
    return face;
}

/// The faces of a font file read through `read`: each of a collection's, or the one of a single
/// face's file.
template <FontBytes Read>
[[nodiscard]] auto readFaces(const Read& read, const std::filesystem::path& file) -> std::vector<FontFace> {
    std::vector<FontFace> faces;
    const std::vector<std::byte> header = read(0, kCollectionHeaderSize);
    if (header.size() >= kTagBytes && loadBigEndian<std::uint32_t>(header) == tagOf("ttcf")) {
        if (header.size() < kCollectionHeaderSize) {
            return faces;
        }
        const auto count =
            std::min(loadBigEndian<std::uint32_t>(std::span{header}.subspan(kFaceCountAt)), kMaxFaces);
        const std::vector<std::byte> offsets =
            read(kCollectionHeaderSize, std::size_t{count} * kFaceOffsetBytes);
        for (std::uint32_t index = 0; (index + 1) * kFaceOffsetBytes <= offsets.size(); ++index) {
            if (auto face = readFace(
                    read, loadBigEndian<std::uint32_t>(std::span{offsets}.subspan(index * kFaceOffsetBytes)),
                    file, index)) {
                faces.push_back(std::move(*face));
            }
        }
        return faces;
    }
    if (auto face = readFace(read, 0, file, 0)) {
        faces.push_back(std::move(*face));
    }
    return faces;
}

/// Whether `path` names a font file the catalog reads, by its extension.
[[nodiscard]] auto isFontFile(const std::filesystem::path& path) -> bool {
    const std::string extension = platform::toUtf8(path.extension());
    return equalsIgnoringCase(extension, ".ttf") || equalsIgnoringCase(extension, ".ttc") ||
           equalsIgnoringCase(extension, ".otf");
}

} // namespace

auto readFontFaces(std::span<const std::byte> bytes, const std::filesystem::path& file)
    -> std::vector<FontFace> {
    return readFaces(
        [bytes](std::uint64_t offset, std::size_t size) {
            const std::size_t start = narrow<std::size_t>(std::min<std::uint64_t>(offset, bytes.size()));
            const std::span<const std::byte> part =
                bytes.subspan(start, std::min(size, bytes.size() - start));
            return std::vector<std::byte>(part.begin(), part.end());
        },
        file);
}

auto FontCatalog::addFile(const std::filesystem::path& path) -> Result<void, FontError> {
    std::optional<Failure<platform::FileError>> failure;
    std::vector<FontFace> faces = readFaces(
        [&](std::uint64_t offset, std::size_t size) {
            auto part = platform::readFilePart(path, offset, size);
            if (!part) {
                failure = std::move(part.error());
                return std::vector<std::byte>{};
            }
            return std::move(*part);
        },
        path);
    if (failure) {
        return fail(FontError::Unreadable, platform::toUtf8(path), std::move(*failure));
    }
    if (faces.empty()) {
        return fail(FontError::NotAFont, std::format("{}: no face names a family", platform::toUtf8(path)));
    }
    std::ranges::move(faces, std::back_inserter(m_faces));
    return {};
}

auto FontCatalog::addFolder(const std::filesystem::path& folder) -> std::size_t {
    const std::size_t before = m_faces.size();
    const auto files = platform::listFiles(folder);
    for (const std::filesystem::path& file : files.value_or(std::vector<std::filesystem::path>{})) {
        if (isFontFile(file)) {
            std::ignore = addFile(folder / file); // a file that cannot be read is left out
        }
    }
    return m_faces.size() - before;
}

auto FontCatalog::choose(std::string_view family, std::int32_t weight, bool isItalic) const
    -> std::optional<FontChoice> {
    const std::int32_t asked = weight > 0 ? weight : kRegularWeight;
    std::vector<const FontFace*> candidates;
    for (const FontFace& face : m_faces) {
        if (std::ranges::any_of(face.families, [family](const std::string& name) {
                return equalsIgnoringCase(name, family);
            })) {
            candidates.push_back(&face);
        }
    }
    if (candidates.empty()) {
        return std::nullopt;
    }
    if (std::ranges::any_of(candidates,
                            [isItalic](const FontFace* face) { return face->isItalic == isItalic; })) {
        std::erase_if(candidates, [isItalic](const FontFace* face) { return face->isItalic != isItalic; });
    }
    const FontFace* chosen = nullptr;
    for (const FontFace* face : candidates) {
        const bool isWithinReach = face->weight <= asked + kHeavierReach;
        if (isWithinReach && (chosen == nullptr || face->weight > chosen->weight)) {
            chosen = face;
        }
    }
    if (chosen == nullptr) {
        chosen = *std::ranges::min_element(candidates, {}, [](const FontFace* face) { return face->weight; });
    }
    return FontChoice{.face = chosen,
                      .isEmboldened =
                          chosen->weight < kSimulatedBoldBelow && asked > chosen->weight + kSimulatedBoldGap};
}

auto FontCatalog::open(const FontChoice& choice, FontRequest request) -> Result<Font, FontError> {
    expects(choice.face != nullptr, "FontCatalog::open: a chosen face");
    auto& file = m_files[choice.face->file];
    if (file == nullptr) {
        auto bytes = platform::readFile(choice.face->file);
        if (!bytes) {
            return fail(FontError::Unreadable, platform::toUtf8(choice.face->file), std::move(bytes.error()));
        }
        file = std::make_shared<const std::vector<std::byte>>(std::move(*bytes));
    }
    request.faceIndex = choice.face->faceIndex;
    request.isEmboldened = choice.isEmboldened;
    return Font::open(file, request);
}

} // namespace nx::ui
