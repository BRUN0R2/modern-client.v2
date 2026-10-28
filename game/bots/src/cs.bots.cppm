// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// cs.bots — Counter-Strike's bot (docs/10-counter-strike-gameplay.md, section 11, ADR-0024): players
/// of the server's game driven by the commands their agents make, on the agents every kind shares
/// (nx.agents) and the navigation meshes (nx.navigation).
///
/// Today: the bots that join, leave and keep their number (Bots, bot_add, bot_add_t, bot_add_ct,
/// bot_kick, bot_quota), see what is in their cone and in line (botSight), hunt, chase and fire; read
/// from data/cstrike/bots.toml (BotRules).
export module cs.bots;

export import :botRules;
export import :botSight;
export import :bots;
export import :botCommands;
