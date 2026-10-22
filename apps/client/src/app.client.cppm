// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// app.client — the client, `next` (docs/01-architecture.md, sections 1.1 and 3): its command line
/// and its main loop, over everything below it.
///
/// Today: the original's command line (`-basedir`, the `+` commands), the player's settings, and the
/// map the `map` command loads, played by the client alone in a window until the server comes.
export module app.client;

export import :commandLine;
export import :client;
