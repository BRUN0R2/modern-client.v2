// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The fonts a scheme can name (ADR-0020): those of the system and those a scheme brings
/// (`CustomFontFiles`), each face described by its own tables, and the face a scheme's font gets
/// found as Windows' GDI finds it. GDI takes the family by its name (name ID 1, in any of the
/// languages the files give it), then a face by italics and weight, and simulates bold when the
/// face is too light; the weights where it changes its mind were measured on it
/// (docs/12-ui-hud-and-console.md, section 2.1).
export module nx.ui:fontCatalog;

import std;
import nx.core;
import :font;

export namespace nx::ui {

/// A face of a font file, as its tables describe it.
struct FontFace {
    std::filesystem::path file{};
    std::uint32_t faceIndex = 0;         ///< Its place in a collection (.ttc); 0 in a single face's file.
    std::vector<std::string> families{}; ///< Its name ID 1 in each language the file gives it, English first.
    std::string style{};                 ///< Its name ID 2, in English when the file has it ("Bold").
    std::uint16_t weight = 400;          ///< From 1 to 1000: 400 regular, 700 bold (OS/2 usWeightClass).
    bool isItalic = false;
};

/// The faces of the TrueType or OpenType font, or collection, `bytes`, which file `file` holds: those
/// whose tables name a family GDI could be asked for (at most 31 characters, as its LOGFONT holds
/// them). None for what is not such a font; at most 64 of a collection.
[[nodiscard]] auto readFontFaces(std::span<const std::byte> bytes, const std::filesystem::path& file = {})
    -> std::vector<FontFace>;

/// What a scheme's font asks of a face: its family (any ASCII case), its weight (0 for 400) and
/// whether it is italic.
struct FontQuery {
    std::string_view family{};
    std::int32_t weight = 0;
    bool isItalic = false;
};

/// The face chosen for a font a scheme asks for, and whether bold is simulated on it. The face is the
/// catalog's: valid until faces are added to it.
struct FontChoice {
    const FontFace* face = nullptr;
    bool isEmboldened = false;
};

/// The fonts to choose from.
class FontCatalog {
public:
    /// Adds the faces of the font file at `path`, reading only its tables. Errors: Unreadable (cause:
    /// the file system's), NotAFont (no face names a family).
    [[nodiscard]] auto addFile(const std::filesystem::path& path) -> Result<void, FontError>;
    /// Adds the faces of each font file (.ttf, .ttc, .otf) in `folder` and its subfolders, leaving out
    /// those that cannot be read; returns how many faces were added. Errors: Unreadable (the folder
    /// cannot be listed; cause: the file system's).
    [[nodiscard]] auto addFolder(const std::filesystem::path& folder) -> Result<std::size_t, FontError>;

    [[nodiscard]] inline auto faces() const noexcept -> std::span<const FontFace> { return m_faces; }

    /// The face for `query`, as GDI chooses it among the family's faces: those as italic as asked if
    /// there are any; of them the heaviest that is at most 88 heavier than asked, else the lightest.
    /// Bold is simulated on a face lighter than 600 that is more than 150 lighter than asked. None when
    /// no face has the family.
    [[nodiscard]] auto choose(const FontQuery& query) const -> std::optional<FontChoice>;

    /// The font of `choice` at what `request` asks for, its file read once and shared by the fonts
    /// opened from it. Errors: Unreadable, and those of Font::open.
    [[nodiscard]] auto open(const FontChoice& choice, FontRequest request) -> Result<Font, FontError>;

private:
    std::vector<FontFace> m_faces;
    /// The files opened, each shared with the fonts opened from it (Font::open).
    std::map<std::filesystem::path, std::shared_ptr<const std::vector<std::byte>>> m_files;
};

} // namespace nx::ui
