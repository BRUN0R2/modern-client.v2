// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

module app.client;

import std;
import nx.core;
import nx.jobs;
import nx.platform;
import nx.assets;
import nx.console;
import cs.shared;
import cs.server;
import mod.zombies;

namespace app {

namespace {

using nx::console::CommandLine;
using nx::console::Console;
using nx::console::ConsoleError;

/// The test zombies' settings of the project, among its data, and a user's, among the game's content, which
/// change only what they name of them.
constexpr std::string_view kProjectSettingsFile = "zombies.toml";
constexpr std::string_view kProjectSettingsFolder = "mods";
constexpr std::string_view kSettingsFile = "entities/zombies.toml";

/// What the test zombies are, on whatever map they are added to.
struct ZombieKind {
    cs::EntityTypeIndex type;
    zombies::Settings settings;
};

/// How many zombies `line` asks for: one unless it says. Errors: InvalidArguments.
[[nodiscard]] auto zombieCountOf(const CommandLine& line) -> nx::Result<std::uint32_t, ConsoleError> {
    if (line.arguments().size() > 1) {
        return nx::fail(ConsoleError::InvalidArguments, "usage: zombie_add [count]");
    }
    std::uint32_t count = 1;
    if (!line.arguments().empty()) {
        const std::string& text = line.arguments().front();
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), count);
        if (error != std::errc{} || end != text.data() + text.size() || count == 0) {
            return nx::fail(ConsoleError::InvalidArguments, "usage: zombie_add [positive count]");
        }
    }
    return count;
}

/// The test zombies' settings: the project's file in `dataFolder`, and kSettingsFile of `content` over it, if
/// a user wrote one. Errors: CommandFailed, with the cause.
[[nodiscard]] auto readZombieSettings(const cs::GameContent& content, const std::filesystem::path& dataFolder)
    -> nx::Result<zombies::Settings, ConsoleError> {
    const std::filesystem::path projectFile = dataFolder / kProjectSettingsFolder / kProjectSettingsFile;
    auto projectText = nx::platform::readTextFile(projectFile);
    if (!projectText) {
        return nx::fail(ConsoleError::CommandFailed, nx::platform::toUtf8(projectFile),
                        std::move(projectText.error()));
    }
    auto project = zombies::readSettings(*projectText, kProjectSettingsFile);
    if (!project) {
        return nx::fail(ConsoleError::CommandFailed, std::move(project.error()));
    }
    auto path = nx::assets::VfsPath::parse(kSettingsFile);
    nx::invariant(path.has_value(), "zombie_add: the settings file is a valid VFS path");
    auto bytes = content.vfs.readAll(*path);
    if (!bytes) {
        if (bytes.error().code == nx::assets::VfsError::NotFound) {
            return std::move(*project); // none changes them
        }
        return nx::fail(ConsoleError::CommandFailed, std::string{kSettingsFile}, std::move(bytes.error()));
    }
    const std::string text = *bytes |
                             std::views::transform([](std::byte value) { return static_cast<char>(value); }) |
                             std::ranges::to<std::string>();
    auto settings = zombies::readSettingsOver(*project, text, kSettingsFile);
    if (!settings) {
        return nx::fail(ConsoleError::CommandFailed, std::move(settings.error()));
    }
    return std::move(*settings);
}

/// What the test zombies are, as readZombieSettings says, their type added to the entities of `content`.
/// Errors: CommandFailed, with the cause.
[[nodiscard]] auto readZombieKind(cs::GameContent& content, const std::filesystem::path& dataFolder)
    -> nx::Result<ZombieKind, ConsoleError> {
    auto settings = readZombieSettings(content, dataFolder);
    if (!settings) {
        return std::unexpected{std::move(settings.error())};
    }
    auto type = content.entities.add(content.vfs, settings->body, cs::ModelTextures::Read);
    if (!type) {
        return nx::fail(ConsoleError::CommandFailed, std::move(type.error()));
    }
    return ZombieKind{.type = *type, .settings = std::move(*settings)};
}

/// Makes the horde of `session`, of `kind`, on the mesh its map built. Errors: why the map has no mesh;
/// CommandFailed, with the cause.
[[nodiscard]] auto makeHorde(Session& session, nx::jobs::JobSystem& jobs, const ZombieKind& kind)
    -> nx::Result<void, ConsoleError> {
    if (!session.agentMesh) {
        return std::unexpected{session.agentMesh.error()};
    }
    auto horde = zombies::Horde::create(*session.game, *session.agentMesh, jobs, kind.type, kind.settings);
    if (!horde) {
        return nx::fail(ConsoleError::CommandFailed, std::move(horde.error()));
    }
    session.horde = std::move(*horde);
    return {};
}

} // namespace

auto registerZombieCommands(Console& console, cs::GameContent& content, std::filesystem::path dataFolder,
                            nx::jobs::JobSystem& jobs, PlayedSession played)
    -> nx::Result<void, ConsoleError> {
    auto added = console.registerCommand(
        {.name = "zombie_add",
         .description = "Spawns entity agents at the terrorist base: zombie_add [count]."},
        [&content, dataFolder = std::move(dataFolder), &jobs, played, kind = std::optional<ZombieKind>{}](
            Console& on, const CommandLine& line) mutable -> nx::Result<void, ConsoleError> {
            const auto count = zombieCountOf(line);
            if (!count) {
                return std::unexpected{count.error()};
            }
            Session* const session = played();
            if (session == nullptr) {
                return nx::fail(ConsoleError::CommandFailed, "zombie_add: load a map first");
            }
            if (!kind) {
                auto read = readZombieKind(content, dataFolder);
                if (!read) {
                    return std::unexpected{std::move(read.error())};
                }
                kind.emplace(std::move(*read));
            }
            if (!session->horde) {
                if (auto made = makeHorde(*session, jobs, *kind); !made) {
                    return made;
                }
            }
            if (auto spawned = session->horde->spawn(session->map->spawnPoints(cs::Team::Terrorists), *count);
                !spawned) {
                return nx::fail(ConsoleError::CommandFailed, std::move(spawned.error()));
            }
            on.print(std::format("{} zombie entities added", *count));
            return {};
        });
    if (!added) {
        return added;
    }
    return console.registerCommand(
        {.name = "zombie_kick", .description = "Removes this session's test zombies and their corpses."},
        [played](Console& on, const CommandLine& line) -> nx::Result<void, ConsoleError> {
            if (!line.arguments().empty()) {
                return nx::fail(ConsoleError::InvalidArguments, "usage: zombie_kick");
            }
            Session* const session = played();
            if (session == nullptr || !session->horde) {
                return nx::fail(ConsoleError::CommandFailed, "zombie_kick: no horde in this map");
            }
            if (auto cleared = session->horde->clear(); !cleared) {
                return nx::fail(ConsoleError::CommandFailed, std::move(cleared.error()));
            }
            on.print("zombie entities removed");
            return {};
        });
}

} // namespace app
