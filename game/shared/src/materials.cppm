// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the textures of the maps are made of (docs/11-audio.md, section 3): the installation's
/// sound/materials.txt, read as ReGameDLL_CS's pm_shared reads it (PM_InitTextureTypes,
/// PM_SortTextures, PM_FindTextureType and PM_CatagorizeTextureType; GPL, consulted: docs/20
/// section 3), which the sounds of the player's steps follow.
export module cs.shared:materials;

import std;
import nx.core;

export namespace cs {

/// What a texture is made of, by the letter materials.txt tags it with.
enum class TextureMaterial : std::uint8_t {
    Concrete, ///< `C`, and every texture the file does not tag, or tags with another letter.
    Metal,    ///< `M`.
    Dirt,     ///< `D`.
    Vent,     ///< `V`.
    Grate,    ///< `G`.
    Tile,     ///< `T`.
    Slosh,    ///< `S`: a liquid.
    Wood,     ///< `W`.
    Computer, ///< `P`.
    Grass,    ///< `X`.
    Glass,    ///< `Y`.
    Flesh,    ///< `F`.
    Snow,     ///< `N`.
    Count,
};

/// The most textures the original reads from the file (MAX_TEXTURES): those tagged after are
/// concrete.
inline constexpr std::size_t kMostTaggedTextures = 1024;
/// The characters of a texture's name the file and the lookup compare (MAX_TEXTURENAME_LENGHT, less
/// its terminator).
inline constexpr std::size_t kTaggedNameLength = 16;
/// The most bytes of a line of the file the original reads at once (memfgets into its 512 bytes,
/// told 511): the rest of a longer line is read as the next.
inline constexpr std::size_t kMostLineBytes = 510;

/// The textures materials.txt tags, as the original holds them: their first kTaggedNameLength
/// characters, sorted without case by its own sort, each with what it is made of.
class TextureMaterials {
public:
    /// What `text`, a materials.txt, tags, as PM_InitTextureTypes reads it: on each line, after any
    /// spaces, a letter, spaces, then a texture's name, which must be followed by a space or the
    /// line's end of line; a line that starts otherwise (a comment) is left out; kMostTaggedTextures
    /// at most. A line is kMostLineBytes at most, the rest of a longer one the next, and ends at a NUL,
    /// as the original reads it as a string.
    [[nodiscard]] static auto read(std::string_view text) -> TextureMaterials;

    /// What the texture named `name` is made of, as PM_FindTextureType finds it: its first
    /// kTaggedNameLength characters looked up without case; concrete when the file does not tag it.
    [[nodiscard]] auto materialOf(std::string_view name) const noexcept -> TextureMaterial;

    /// How many textures it tags.
    [[nodiscard]] inline auto size() const noexcept -> std::size_t { return m_tagged.size(); }

private:
    struct Tagged {
        std::string name; ///< Its first kTaggedNameLength characters.
        TextureMaterial material = TextureMaterial::Concrete;
    };

    std::vector<Tagged> m_tagged; ///< In the order the original's sort leaves them.
};

/// The name of a face's texture `name` as the player's movement looks it up (PM_CatagorizeTextureType):
/// without the `-` or `+` and the digit that start a tiled or toggled texture's, then without one
/// `{`, `!`, `~` or space, and kTaggedNameLength characters at most.
[[nodiscard]] auto lookedUpTextureName(std::string_view name) noexcept -> std::string_view;

} // namespace cs
