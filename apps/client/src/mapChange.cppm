// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// How the map the client plays changes (docs/12-ui-hud-and-console.md, section 4, the loading screen):
/// `map` and `nav_generate` ask for a map, a frame tells that it is loading, the next loads it and plays it,
/// and `disconnect` leaves the map played.
export module app.client:mapChange;

import std;
import nx.core;
import nx.console;
import :client;
import :agentMeshes;

export namespace app {

/// Where a map asked for is on its way to be played.
enum class MapAskedStage : std::uint8_t {
    Asked,       ///< The frame drawn next tells that it is loading.
    ToldLoading, ///< A frame told so: the next one loads it, holding the frames for as long as it takes.
};

/// What the client plays from the frame's switch on (MapChange::switchMap).
enum class MapSwitch : std::uint8_t {
    Stay,  ///< What it played: a map, or none.
    Play,  ///< The map just loaded.
    Leave, ///< No map: `disconnect` left the map played, or the one just loaded.
};

/// What loads a map asked for: the map `name`, its agents' mesh read from the cache or not as `kept` says.
/// Errors: those of the command that asked for it.
template <typename Played>
using MapLoader = std::move_only_function<nx::Result<std::unique_ptr<Played>, nx::console::ConsoleError>(
    std::string_view name, KeptMesh kept)>;

/// The map the client plays, `Played` (a Session), and how it changes as the console's `map`, `nav_generate`
/// and `disconnect` ask. A frame carries a change out in this order, as the client's run does:
///  1. loadAsked(): the map a frame told loading is loaded, to be played from this frame;
///  2. the console's commands run, unless holdsCommands(): those that follow a map asked for wait for it to
///  be
///     loaded, so that they act on it (actedOn());
///  3. switchMap(), when switchDue() says that one is due: the client plays what was asked for from now on;
///  4. frameShown(): the frame drawn told that the map asked for is loading.
/// A template so that what a map played is does not matter to how it changes: the tests change stand-ins of
/// no game. It stays where it is made: the console's commands point into it.
template <typename Played>
class MapChange {
public:
    /// Loads the maps asked for with `load`.
    explicit MapChange(MapLoader<Played> load) noexcept : load{std::move(load)} {}

    MapChange(const MapChange&) = delete;
    auto operator=(const MapChange&) -> MapChange& = delete;
    MapChange(MapChange&&) = delete;
    auto operator=(MapChange&&) -> MapChange& = delete;
    ~MapChange() = default;

    /// Registers in `console` `map`, which asks for a map, `nav_generate`, which asks for the map played
    /// again, its agents' mesh built again, and `disconnect`, which leaves the map played. Errors:
    /// SettingsUnavailable, with the cause.
    [[nodiscard]] auto registerCommands(nx::console::Console& console) -> nx::Result<void, ClientError> {
        using nx::console::CommandLine;
        using nx::console::Console;
        using nx::console::ConsoleError;
        for (auto&& registered : {
                 console.registerCommand(
                     {.name = "map", .description = "Plays a map: map <name>."},
                     [this](Console& asking, const CommandLine& line) -> nx::Result<void, ConsoleError> {
                         if (line.arguments().size() != 1) {
                             return nx::fail(ConsoleError::InvalidArguments, "usage: map <name>");
                         }
                         ask(line.arguments().front(), KeptMesh::Read, asking);
                         return {};
                     }),
                 console.registerCommand(
                     {
                         .name = "nav_generate",
                         .description =
                             "Builds the agents' navigation mesh of the map again, the cache's left "
                             "aside, and plays the map again on it.",
                     },
                     [this](Console& asking, const CommandLine& line) -> nx::Result<void, ConsoleError> {
                         if (!line.arguments().empty()) {
                             return nx::fail(ConsoleError::InvalidArguments, "usage: nav_generate");
                         }
                         if (!playing) {
                             return nx::fail(ConsoleError::CommandFailed, "nav_generate: load a map first");
                         }
                         // As Condition Zero restarts the game once it analyzed a map.
                         ask(playing->name, KeptMesh::Ignored, asking);
                         return {};
                     }),
                 console.registerCommand(
                     {.name = "disconnect", .description = "Leaves the map played."},
                     [this](Console&, const CommandLine&) -> nx::Result<void, ConsoleError> {
                         leave();
                         return {};
                     }),
             }) {
            if (!registered) {
                return nx::fail(ClientError::SettingsUnavailable, registered.error());
            }
        }
        return {};
    }

    /// The map the frames play; none on the menu.
    [[nodiscard]] auto played() const noexcept -> Played* { return playing ? playing->map.get() : nullptr; }

    /// The map the console's commands act on: the one just loaded, played from this frame's switch, as the
    /// commands that follow `map` act on it; none once `disconnect` asked; else the map played, or none.
    [[nodiscard]] auto actedOn() const noexcept -> Played* {
        switch (due) {
        case MapSwitch::Play:
            return loaded->map.get();
        case MapSwitch::Leave:
            return nullptr;
        case MapSwitch::Stay:
            break;
        }
        return played();
    }

    /// The name of the map asked for, while it is on its way; none else.
    [[nodiscard]] auto askedMap() const noexcept -> std::optional<std::string_view> {
        if (!asked) {
            return std::nullopt;
        }
        return asked->name;
    }

    /// Whether the console's commands wait: a map asked for is not loaded yet.
    [[nodiscard]] auto holdsCommands() const noexcept -> bool { return asked.has_value(); }

    /// Loads the map asked for, once a frame told that it is loading, to be played from this frame's switch;
    /// logged. One that cannot be loaded leaves the client where it was, and its commands no longer wait.
    /// Errors: those of the loader.
    [[nodiscard]] auto loadAsked() -> nx::Result<void, nx::console::ConsoleError> {
        if (!asked || asked->stage != MapAskedStage::ToldLoading) {
            return {};
        }
        const AskedMap map = *std::exchange(asked, std::nullopt);
        nx::log::info(kLog, "loading the map {}", map.name);
        auto made = load(map.name, map.kept);
        if (!made) {
            return std::unexpected{std::move(made.error())};
        }
        loaded = PlayedMap{.name = map.name, .map = std::move(*made)};
        due = MapSwitch::Play;
        return {};
    }

    /// What the frame's switch plays from now on.
    [[nodiscard]] auto switchDue() const noexcept -> MapSwitch { return due; }

    /// Plays from now on what is due (switchDue), which is logged: the map loaded, or none. The map played
    /// before goes. Expects a switch due.
    void switchMap() {
        nx::expects(due != MapSwitch::Stay, "MapChange::switchMap: a switch is due");
        if (due == MapSwitch::Play) {
            nx::log::info(kLog, "playing the map {}", loaded->name);
        } else if (playing) {
            nx::log::info(kLog, "left the map {}", playing->name);
        }
        due = MapSwitch::Stay;
        playing = std::exchange(loaded, std::nullopt);
    }

    /// The frame drawn told that the map asked for is loading, or none did while the window shows none: the
    /// next frame loads it.
    void frameShown() noexcept {
        if (asked) {
            asked->stage = MapAskedStage::ToldLoading;
        }
    }

private:
    /// A map played, or loaded to be: its name, as it was asked for.
    struct PlayedMap {
        std::string name;
        std::unique_ptr<Played> map;
    };

    /// A map asked for: its name, and whether its agents' mesh is read from the cache.
    struct AskedMap {
        std::string name;
        KeptMesh kept = KeptMesh::Read;
        MapAskedStage stage = MapAskedStage::Asked;
    };

    static constexpr nx::log::Category kLog{"client"};

    /// Asks for the map `name`, its agents' mesh read from the cache or not as `kept` says, in place of one
    /// asked for before: the next frame tells that it is loading, the one after loads it, and `console`'s
    /// commands that follow run once it is loaded.
    void ask(std::string name, KeptMesh kept, nx::console::Console& console) {
        asked = AskedMap{.name = std::move(name), .kept = kept};
        console.wait();
    }

    /// Leaves the map the commands act on from this frame's switch, as the original leaves a game; nothing
    /// without one.
    void leave() noexcept {
        if (actedOn() == nullptr) {
            return;
        }
        loaded.reset();
        due = MapSwitch::Leave;
    }

    MapLoader<Played> load;
    std::optional<PlayedMap> playing; ///< None on the menu.
    std::optional<PlayedMap> loaded;  ///< While `due` is Play.
    MapSwitch due = MapSwitch::Stay;  ///< At this frame's switch.
    std::optional<AskedMap> asked;    ///< While it is on its way.
};

} // namespace app
