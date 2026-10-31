// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What the game shows of every map's world over what the map sets (docs/05-rendering.md, section 8):
/// data/cstrike/environment.toml, as a mod sets the original's `sv_skyname` over each map's sky. The
/// game's own data sets nothing, each map its own; a mod's data names its sky. The world's light is the
/// light styles' (cs.shared:lightStyles): a mod's darker style 0 there.
export module cs.shared:environment;

import std;
import nx.core;
import nx.meta;
import nx.data;
import nx.assets;

export namespace cs {

/// data/cstrike/environment.toml: a sky in place of each map's worldspawn's `skyname`, none for each
/// map's own.
struct WorldEnvironment {
    std::optional<std::string> sky{};
};

/// Why the environment could not be read.
enum class EnvironmentError : std::uint8_t {
    DataInvalid, ///< The text is not of the file's schema (cause: the DataError), or names a sky of no
                 ///< name, or of one with a path in it.
};

/// Reads `text`, the file `sourceName`: a sky, if one, of a name with no path in it. Errors: DataInvalid.
[[nodiscard]] auto readWorldEnvironment(std::string_view text, std::string_view sourceName)
    -> nx::Result<WorldEnvironment, EnvironmentError>;

/// The name of the sky `map` is drawn with by `environment`: the environment's, else its worldspawn's
/// `skyname`; none for a map of neither, which the client draws with its default sky. It points into
/// the environment or the map.
[[nodiscard]] auto skyNameOf(const nx::assets::BspView& map, const WorldEnvironment& environment)
    -> std::optional<std::string_view>;

} // namespace cs

template <>
struct nx::meta::Description<cs::WorldEnvironment> {
    static constexpr std::string_view kName = "cs::WorldEnvironment";
    static constexpr std::tuple kFields{
        nx::meta::field<&cs::WorldEnvironment::sky>("sky"),
    };
};
