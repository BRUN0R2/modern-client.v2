// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception

/// What a player does with its command for a tick: what it thinks before it moves, its move and what it
/// touches, and what it thinks after (SV_RunCmd, CBasePlayer::PreThink, PostThink).
export module cs.server:playerCommands;

import std;
import nx.core;
import nx.math;
import cs.shared;
import :gameState;

namespace cs {

/// What a move of `mover` meets as boxes: the world's (solidBoxesOf), the other players alive among
/// them, each standing or ducked, as the engine lists them for movement (SV_AddLinksToPM's
/// SOLID_SLIDEBOX).
[[nodiscard]] auto boxesMetBy(GameState& game, const Player& mover) -> std::vector<SolidBox>;

/// `player` uses what is in front of it, looking along `command`'s view from where the move left its
/// eye (CBasePlayer::PlayerUse), heard finding something to use or nothing (UseEmpty).
void use(GameState& game, Player& player, const UserCommand& command);

/// The sounds `sounds`, made by `player`'s movement at the tick of `command` among `entities`, as it
/// hears them: each step by what it steps on (GameMap::materialAlong, in the space of the entity it
/// stands on), while mp_footsteps lets them sound; each splash.
void hearMovement(GameState& game, Player& player, std::span<const MovementSound> sounds,
                  std::span<const BrushEntity> entities, const UserCommand& command);

/// What the ground a player stood on at `ground` is made of: the texture kStepTextureDepth under it,
/// on the world or on the entity among `entities` it stood on (PM_CatagorizeTextureType).
[[nodiscard]] auto groundMaterial(const GameState& game, const StepGround& ground,
                                  std::span<const BrushEntity> entities) -> TextureMaterial;

/// What the texture a trace from `start` to `end` met, `hit`, is made of, as UTIL_TextureHit finds
/// it: flesh for a body; of the world's or a brush entity's model, as materials.txt tags it; none for
/// another entity.
[[nodiscard]] auto materialOfHit(const GameState& game, const WeaponHit& hit, nx::Vec3 start, nx::Vec3 end)
    -> std::optional<TextureMaterial>;

/// What `player` does before `command` moves it (PreThink): alive, it breathes in the water the last
/// command left it in (Breath::breathe), hurt as it drowns and by harmful water; on the ground, slowed
/// down by a hit, it is a hundredth of its speed less slow each command, its velocity that share of it
/// (m_flVelocityModifier); then the harms it keeps over time are looked at, and what drowning took
/// given back to it alive (TimedHarms::check).
void preThink(GameState& game, Player& player, const UserCommand& command);

/// What a living `player` does once `command` moved it from `before` (PostThink): unless it is held
/// back (Inventory::isHeldBack), the use key going down uses what is in front (use), then the weapon
/// in hand fires, is reloaded or idles, its attacks dealt to what they meet (ItemPostFrame); then its
/// landing hurts it (landingDamageOf).
void postThink(GameState& game, Player& player, const UserCommand& command, const MovementState& before);

/// What `player` does with `command` in the tick: it selects, thinks before it moves (preThink),
/// moves and touches what it meets, then thinks after (postThink), alive.
void runCommand(GameState& game, Player& player, const UserCommand& command);

} // namespace cs
