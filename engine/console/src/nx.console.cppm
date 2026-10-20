// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// nx.console — the console language shared by client and server (docs/01-architecture.md, layer 1;
/// docs/12-ui-hud-and-console.md, section 5).
///
/// Typed cvars with the classic names and syntax, commands, aliases, `+`/`-` buttons and the
/// command buffer that runs configs (.cfg) with `;`, quotes, comments, `wait` and `exec`, as the
/// original does, plus the `log_level_<category>` cvars. The console UI (nx.ui) and the server
/// terminal will be views over a Console (planned: docs/12-ui-hud-and-console.md, section 5.3).
export module nx.console;

export import :text;
export import :parser;
export import :cvar;
export import :button;
export import :console;
export import :logLevels;
