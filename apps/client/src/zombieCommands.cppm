// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// The console's test zombies (docs/offlineAgents.md): `zombie_add` and `zombie_kick`.
export module app.client:zombieCommands;

import std;
import nx.core;
import nx.jobs;
import nx.console;
import cs.shared;
import :session;

namespace app {

/// The session a command acts on; none without one.
using PlayedSession = std::function<Session*()>;

/// Registers in `console` the commands that add test zombies to the session `played` says, and remove them:
/// what they are is read, as the first is asked for, from the project's file in `dataFolder` and a user's in
/// `content` over it, their type added to its entities, and each session's horde decides in jobs of `jobs`.
/// `content` and `jobs` must outlive the commands. Errors: those of nx::console::Console::registerCommand.
[[nodiscard]] auto registerZombieCommands(nx::console::Console& console, cs::GameContent& content,
                                          std::filesystem::path dataFolder, nx::jobs::JobSystem& jobs,
                                          PlayedSession played)
    -> nx::Result<void, nx::console::ConsoleError>;

} // namespace app
