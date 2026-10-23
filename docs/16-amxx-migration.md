# 16 — AMX Mod X (Pawn) → Rook migration

## 1. Philosophy

The mental model stays the same: plugins with `pluginPrecache`/`pluginInit`, natives,
forwards, hooks, `setTask`, cvars and commands. Migrating means **replacing indices, tags
and loose strings with types**. The compiler points out everything that was wrong and went
unnoticed in Pawn.

The conversion can be done by hand with this guide or assisted by
`rook migrate` ([§6](#6-rook-migrate)). It may keep AMX Mod X's spelling: `#include <amxmodx>`,
`new`, the natives of the `amxx` layer and the underscored accessors (`get_player_var`) compile as
they read in Pawn, the types aside ([§3.1](#31-the-amxx-layer),
[ADR-0021](adr/0021-rook-amxx-names.md)).

## 2. Syntax

| Pawn | Rook |
|------|------|
| `#include <amxmodx>` | `import cs.prelude.*;`, or kept: it imports `amxx.amxmodx.*` ([§3.1](#31-the-amxx-layer)) |
| `#include <rezombie_main>` | `import rezombie.prelude.*;` |
| `#pragma semicolon 1` | Always mandatory |
| `#pragma compress 1` | Unnecessary (packages are already compressed) |
| `#define MAX_ZOMBIES 10` | `const MAX_ZOMBIES: i32 = 10;` |
| `register_plugin("Name", VERSION, AUTHOR)` | `plugin { name: ..., version: ..., author: ... }` block |
| `new x = 10;` | `let x = 10;` (immutable) or `var x = 10;`; `new x = 10;` is kept as `var` |
| `new Float:speed = 250.0;` | `let speed: Speed = 250ups;`, or kept with its type before the name: `new Speed:speed = 250ups;` |
| `new Class:class = ...` (tag) | `let class: Class = ...` (nominal handle) |
| `any:` | Generics (`fn f<T>(x: T)`) |
| `_:value` (remove the tag) | `.value()` or an explicit conversion |
| `stock foo()` | `fn foo()` (or `pub fn` to export it) |
| `public callback()` referenced by a string | `fn callback()` passed by reference |
| `@OnPrimaryAttack(...)` | `fn onPrimaryAttack(...)` |
| `forward @round_start(...)` (declaration) | `pub forward roundStart(...);` |
| `native create_class(...)` + `register_native` | `pub fn createClass(...)` in the API plugin |
| `#pragma unused weapon` | Parameter with a `_` prefix: `_weapon: Weapon` |
| `new players[MAX_PLAYERS]; new count;` | `let players: List<Player> = ...;` |
| `new g_data[MAX_PLAYERS + 1];` | `var data: PlayerMap<T> = PlayerMap.new();` (cleared on disconnect) |
| `new szName[32]; get_user_name(id, szName, charsmax(szName));` | `let name = getPlayerVar(player, "name");` |
| `formatex(buffer, charsmax(buffer), "%s has %d", a, b)` | `"{a} has {b}"` |
| `equal(a, b)` / `equali(a, b)` | `a == b` / `a.eqIgnoreCase(b)` |
| `str_to_num(s)` | `i32.parse(s)?` |
| `switch (x) { case 1: ... }` | `match x { ... }` (exhaustive) |
| `ArrayCreate` / `TrieCreate` | `List<T>` / `Map<K, V>` |
| `null` (`any:0`) | `none` (with `T?`) |

## 3. API

| AMXX / ReAPI / Fakemeta / Ham | Rook |
|-------------------------------|------|
| `pev(id, pev_origin, origin)` / `get_entvar(id, var_origin, origin)` | `let origin = getEntityVar(entity, "origin");` |
| `set_pev(id, pev_health, 100.0)` / `set_entvar(...)` | `setEntityVar(entity, "health", 100hp);` |
| `get_member(id, m_iAccount)` / `cs_get_user_money(id)` | `getPlayerVar(player, "money")` |
| `set_member(id, m_iAccount, 800)` / `cs_set_user_money` | `setPlayerVar(player, "money", Money(800));` |
| `get_user_health(id)` / `set_user_health(id, 100)` | `getEntityVar(player, "health")` / `setEntityVar(player, "health", 100hp)` |
| `is_user_alive(id)` | `getPlayerVar(player, "isAlive")` |
| `get_user_team(id)` / `cs_get_user_team` | `getPlayerVar(player, "team")` |
| `rg_set_user_team(id, TEAM_CT)` | `changeTeam(player, Team.CounterTerrorist)?` |
| `get_players(players, num, "ae", "TERRORIST")` | `getPlayers(PlayerFilter { alive: some(true), team: some(Team.Terrorist) })` |
| `give_item(id, "weapon_ak47")` / `rg_give_item` | `giveWeapon(player, WEAPON_AK47)?` |
| `precache_model("models/x.mdl")` | `precacheModel("models/x.mdl")?` → typed `Model`, no limit |
| `RegisterHam(Ham_TakeDamage, "player", "fw_TakeDamage")` | `@hook(Hook.PlayerTakeDamage, HookPhase.Pre)` |
| `RegisterHookChain(RG_CBasePlayer_Spawn, "OnSpawn", .post = true)` | `@hook(Hook.PlayerSpawn, HookPhase.Post)` or `registerHook(...)` |
| `SetHookChainArg(4, ATYPE_FLOAT, dmg)` | `args.damage = dmg;` (`inout args`) |
| `HC_CONTINUE` / `HC_SUPERCEDE` / `HC_BREAK` | `HookResult.Continue` / `.Supercede` / `.Break` |
| `register_event("DeathMsg", "on_death", "a")` | `forward playerKilled(victim, killer, weapon, headshot) { ... }` |
| `register_logevent("on_round_start", 2, "1=Round_Start")` | `forward roundStart() { ... }` |
| `register_clcmd("say /menu", "cmd_menu")` | `registerClcmd("say /menu", cmdMenu)?` |
| `read_argv(1, arg, charsmax(arg))` | Typed parameters in the command signature |
| `create_cvar("rz_delay", "10.0")` + `bind_pcvar_float` | `createCvar("rz_delay", 10s, min: 1s, max: 60s)?` + `getCvar(cvar)` |
| `set_task(1.0, "task", id)` / `remove_task(id)` | `setTask(1s, task, owner: refOf(player))` / `removeTask(handle)` |
| `set_hudmessage(...)` + `show_hudmessage(id, ...)` | `showHudText(player, HudText { ... })` |
| `client_print(id, print_chat, "...")` | `clientPrint(player, PrintTarget.Chat, "...")` |
| `client_print_color(id, print_team_default, "^4[RZ]^1 ...")` | `clientPrintColor(player, "{green}[RZ]{default} ...")` |
| `menu_create` / `menu_additem` / `menu_display` | `showMenu(player, Menu<T> { ... })` |
| `message_begin(MSG_BROADCAST, SVC_TEMPENTITY)` + `write_byte`… | `createBeam(...)`, `createSpriteEffect(...)`, `screenFade(...)` |
| `engfunc(EngFunc_TraceLine, ...)` | `traceLine(TraceQuery { ... })` |
| `client_cmd(id, "...")` | Does not exist (protection against *slowhack*). Use the specific API |
| `pdata` offsets / Orpheu | Unnecessary: the `vars` catalog and hooks |
| `nvault_open` / `nvault_get` | `kvOpen` / `kvGet` (capability `storage`) |
| `SQL_MakeDbTuple` / `SQL_ThreadQuery` | `sqlOpen` + typed prepared queries (`await`) |
| `set_fail_state("reason")` | `return err(MyError.Variant);` in the lifecycle forward |
| `log_amx("...")` | `logInfo("...")` |

### 3.1 The `amxx` layer

A plugin may keep AMX Mod X's names: the `amxx.*` modules provide, by their literal names, the
natives of the table above that do what no catalog does, each the canonical function with Rook's
types ([ADR-0021](adr/0021-rook-amxx-names.md)):

| Kept | Canonical |
|------|-----------|
| `register_clcmd("say /menu", cmd_menu)?` | `registerClcmd("say /menu", cmdMenu)?` |
| `client_print(player, PrintTarget.Chat, "...")` | `clientPrint(player, PrintTarget.Chat, "...")` |
| `client_print_color(player, "...")` | `clientPrintColor(player, "...")` |
| `set_task(1s, task, owner: refOf(player))` / `remove_task(handle)` | `setTask(...)` / `removeTask(handle)` |
| `precache_model("models/x.mdl")?` | `precacheModel("models/x.mdl")?` |
| `log_amx("...")` | `logInfo("...")` |

The types are Rook's: the handler by reference and not by its name, a `Player` and not an index,
units, `Result`. A native that only reads or writes a property a catalog has is not in the layer,
so that each property has one way: it is reached through its handle's accessor, in either
spelling. `get_user_health(id)` is `get_player_var(player, "health")`, `is_user_alive(id)` is
`get_player_var(player, "isAlive")`, `cs_get_user_money(id)` is `get_player_var(player, "money")`;
calling one of them is an error whose fix writes the accessor.

The natives that reach the engine's internals are not kept either: `engfunc` and `dllfunc`, `pev`
and `set_pev`, ReAPI's `get_entvar`/`set_entvar` and `get_member`/`set_member`,
`rg_create_entity`, `RegisterHam` and `RegisterHookChain`, `message_begin` and `write_byte`.
`#include <fakemeta>` and the other headers of the kind (`engine`, `hamsandwich`, `reapi`,
`orpheu`, `fun`, `cstrike`) are errors; their calls become the typed API of the table above:
`engfunc(EngFunc_SetModel, entity, "models/x.mdl")` and `set_entvar(entity, var_model, "...")`
become `setEntityVar(entity, "model", precacheModel("models/x.mdl")?)`, and an entity is created
whole through `nx.entity` ([15 §2](15-mod-api.md#2-module-organization)).

## 4. Converted examples (ReZombie)

### 4.1 Assassin class

**Pawn**

```pawn
#include <rezombie_main>

#pragma semicolon 1
#pragma compress 1

public plugin_precache()
{
	register_plugin("Class: Assassin", REZOMBIE_VERSION, REZOMBIE_AUTHOR);

	new Class:class = create_class("assassin", TEAM_ZOMBIE);
	set_class_var(class, "name", "Assassin");

	new ModelsPack:models = get_class_var(class, "models");
	models_pack_add_model(models, create_model("models/player/rz_banshee/rz_banshee.mdl"));

	new Props:props = get_class_var(class, "props");
	set_props_var(props, "health", 5000);
	set_props_var(props, "armor", 0);
	set_props_var(props, "speed", 320);
	set_props_var(props, "gravity", 0.75);
	set_props_var(props, "weapons_interaction", false);

	new WeaponClass:melee = get_class_var(class, "melee");
	set_weapon_class_var(melee, "name", "Assassin Claws");
	set_weapon_class_var(melee, "view_model", create_model("models/player/rz_banshee/hand.mdl"));
}
```

**Rook**

```rook
import rezombie.prelude.*;

plugin {
    name:    "Class: Assassin",
    version: REZOMBIE_VERSION,
    author:  REZOMBIE_AUTHOR,
}

pub const ASSASSIN: ClassId = classId("assassin");

forward pluginPrecache() -> Result<void, PrecacheError> {
    let class = createClass(ASSASSIN, Team.Zombie)?;
    setClassVar(class, "name", "Assassin");

    let models = getClassVar(class, "models");
    modelsPackAddModel(models, createModel("models/player/rz_banshee/rz_banshee.mdl")?);

    let props = getClassVar(class, "props");
    setPropsVar(props, "health", 5000hp);
    setPropsVar(props, "armor", 0ap);
    setPropsVar(props, "speed", 320ups);
    setPropsVar(props, "gravity", 0.75x);
    setPropsVar(props, "weaponsInteraction", false);

    let melee = getClassVar(class, "melee");
    setWeaponClassVar(melee, "name", "Assassin Claws");
    setWeaponClassVar(melee, "viewModel", createModel("models/player/rz_banshee/hand.mdl")?);

    return ok();
}
```

What changed:

- Same structure. The names only change spelling to the project's camelCase
  (`set_props_var` → `setPropsVar`, `"weapons_interaction"` → `"weaponsInteraction"`).
- The keys (`"health"`, `"speed"`) are still strings, now **checked at compile time**
  with the right type.
- `5000hp`, `320ups`, `0.75x`: an explicit unit, no magic number and no mixing health
  with speed.
- `createModel(...)?`: a nonexistent model becomes a typed error at load time, not an
  invisible model at runtime.
- The 5000 health shows correctly on the HUD ([12 §3.4](12-ui-hud-and-console.md#34-no-value-truncation)).

### 4.2 Human class (dependency without manual ordering)

**Pawn**: it depended on the order in `plugins.manifest` ("coldsteel must register before
Class: Human").

```pawn
	// Default knife: custom Cold Steel (registered before this class plugin).
	set_class_var(class, "melee", RequireWeaponClass("coldsteel"));
```

**Rook**

```rook
import rezombie.prelude.*;
import rezombie.weapons.coldsteel.{COLDSTEEL};    // the import itself orders the load

plugin {
    name:    "Class: Human",
    version: REZOMBIE_VERSION,
    author:  REZOMBIE_AUTHOR,
}

pub const HUMAN: ClassId = classId("human");

forward pluginPrecache() -> Result<void, PrecacheError> {
    let class = createClass(HUMAN, Team.Human)?;
    setClassVar(class, "name", "Human");

    let props = getClassVar(class, "props");
    setPropsVar(props, "health", 100hp);
    setPropsVar(props, "armor", 100ap);
    setPropsVar(props, "speed", 250ups);
    setPropsVar(props, "gravity", 1.0x);
    setPropsVar(props, "weaponsInteraction", true);

    // Default knife: custom Cold Steel. The import above guarantees it is registered first.
    setClassVar(class, "melee", requireWeaponClass(COLDSTEEL)?);

    return ok();
}
```

### 4.3 Cold Steel weapon

```rook
import rezombie.prelude.*;

plugin {
    name:    "Weapon: Cold Steel",
    version: REZOMBIE_VERSION,
    author:  REZOMBIE_AUTHOR,
}

pub const COLDSTEEL: WeaponClassId = weaponClassId("coldsteel");

const DRAW_SOUND: string = "rezombie/weapons/coldsteel/draw.wav";

forward pluginPrecache() -> Result<void, PrecacheError> {
    precacheSound(DRAW_SOUND)?;

    let weaponClass = createWeaponClass(COLDSTEEL, WeaponType.Melee)?;
    setWeaponClassVar(weaponClass, "name", "Cold Steel");
    setWeaponClassVar(weaponClass, "viewModel", createModel("models/rezombie/weapons/coldsteel/model_v.mdl")?);
    setWeaponClassVar(weaponClass, "playerModel", createModel("models/rezombie/weapons/coldsteel/model_p.mdl")?);
    setWeaponClassVar(weaponClass, "worldModel", createModel("models/rezombie/weapons/coldsteel/model_w.mdl")?);
    setWeaponClassVar(weaponClass, "weight", 10);
    setWeaponClassVar(weaponClass, "canDrop", true);
    bindWeaponVirtual(weaponClass, WeaponMethod.PrimaryAttack, onPrimaryAttack);

    return ok();
}

fn onPrimaryAttack(_weapon: Weapon, _player: Player) -> WeaponReturn {
    return WeaponReturn.Supercede;
}
```

- `bindWeaponVirtual` receives the **function**, not the string `"@OnPrimaryAttack"`. A
  wrong signature is a compile error.
- With `target = "shared"` in `mod.toml`, the weapon is **predicted on the client**.

### 4.4 Nemesis mode

**Pawn** (excerpt)

```pawn
@LaunchNemesis(target)
{
	new players[MAX_PLAYERS];
	new playersCount;
	CollectAliveHumans(players, playersCount);

	if (playersCount <= 0)
		return false;
	...
	if (change_player_class(.id = player, .class = nemesisClass, ...) > RZ_CONTINUE)
		return false;

	return true;
}

EndRoundEvent:@CheckNemesisWin()
{
	...
	if (humans <= 0 && zombies <= 0)
		return EndRoundEventEndDraw;
	...
}
```

**Rook**

```rook
import rezombie.prelude.*;
import rezombie.classes.nemesis.{NEMESIS};

plugin {
    name:    "Mode: Nemesis",
    version: REZOMBIE_VERSION,
    author:  REZOMBIE_AUTHOR,
}

pub error enum NemesisLaunchError {
    @message("no alive human can become the Nemesis")
    NoEligiblePlayers,

    @message("the chosen target is not an alive human")
    TargetNotEligible,

    from ClassChange(ClassChangeError),
}

forward pluginPrecache() -> Result<void, PrecacheError> {
    let mode = createMode(modeId("nemesis"), launchNemesis)?;
    setModeVar(mode, "name", "Nemesis");
    setModeVar(mode, "noticeMessage", "Nemesis");
    setModeVar(mode, "minPlayers", 2);
    setModeVar(mode, "roundTime", 300s);
    // Single boss: no auto-respawn (no cloned nemesis from horde respawn).
    setModeVar(mode, "respawn", RespawnType.Off);
    setModeVar(mode, "supportTarget", true);
    setModeVar(mode, "targetClass", NEMESIS);
    setModeVar(mode, "winCheck", checkNemesisWin);
    return ok();
}

/// One super-zombie (Nemesis) vs the remaining humans.
/// No infection; no class override (one living boss only).
fn launchNemesis(target: Player?) -> Result<void, NemesisLaunchError> {
    let humans = collectAliveHumans();

    if let chosen = target {
        if !isAliveHuman(chosen) {
            return err(NemesisLaunchError.TargetNotEligible);
        }
    }

    let player = pickPlayer(humans, target).okOr(NemesisLaunchError.NoEligiblePlayers)?;
    changePlayerClass(player, NEMESIS, reason: ClassChangeReason.Mode)?;
    return ok();
}

fn checkNemesisWin() -> EndRoundEvent {
    let humans  = countAliveHumans();
    let zombies = countAliveZombies();

    return match (humans, zombies) {
        (0, 0) => EndRoundEvent.Draw,
        (0, _) => EndRoundEvent.ZombiesWin,
        (_, 0) => EndRoundEvent.HumansWin,
        _      => EndRoundEvent.None,
    };
}
```

What changed:

- `launchNemesis` returns **why** it failed (`NemesisLaunchError`), not just `false`. The
  owner of the flow (`GameRules`) reacts accordingly and the reason shows up in the log.
- `target` is `Player?`: no more `RZ_MODE_NO_TARGET = 0`.
- The empty `prepare`/`end` callbacks of the original are no longer mandatory: they are
  optional keys of the `vars Mode` catalog.
- A `match` on a tuple replaces the `if` chain, and the compiler guarantees that every case
  is covered.

## 5. Important behavior differences

| Topic | AMXX | Rook |
|-------|------|------|
| Entity index | Reusable integer `id`, `0` is the world | Generational handle. The world is `world()`. Absence is `none` |
| Keeping a player for later | `g_target = id` (may become another player) | `PlayerRef` + `resolve()` (enforced by the compiler) |
| Per-player data | Reset by hand in `client_disconnected` | `PlayerMap<T>` cleans itself up |
| `setTask` timing | Server clock, affected by FPS | Deterministic game time per tick |
| Initialization failure | `set_fail_state` | `err(...)` in the forward, and dependents stop with the reason |
| Strings | Fixed buffers and multibyte problems | UTF-8 `string` without a fixed size |
| Arrays | Out of bounds raises a generic runtime error | Checked index with a typed failure and a *stack trace* |
| Overflow | Silent *wrap* | `panic` (or explicit `wrapping_*`) |
| Plugin order | Manual, in `plugins.ini` | By the import graph |
| Reloading | Map change | `plugins reload` with `persist var` migration |
| Precache | Limited and only at map start | No limit and at any time |

## 6. `rook migrate`

```bash
rook migrate src/class/Assassin.sma --out plugins/class/assassin.rook --report migrate-report.md
```

What the tool does:

1. Parses the Pawn source, including includes and `#define`s.
2. Converts the syntax (declarations, tags to types, `switch` to `match`, `format` to
   interpolation) and renames identifiers and `vars` keys to camelCase
   (`set_props_var(props, "max_clip", ...)` → `setPropsVar(props, "maxClip", ...)`).
3. Maps known natives (AMXX, fun, cstrike, fakemeta, hamsandwich, ReAPI) through the
   table in [§3](#3-api).
4. Infers units from the catalog key (`"health"` → `hp`) and from variable names.
5. Marks every point that requires a human decision with `// MIGRATE(Rxxxx): explanation`,
   for example handles stored in globals, `client_cmd` or pdata offsets.
6. Generates a report with what was converted automatically, what needs review and what
   has no equivalent.

With `--keep-amxx-names`, it keeps AMX Mod X's spelling (`#include`, `new`, the `amxx` layer's
natives, the underscored accessors) and changes only what typing requires, for the smallest diff;
the natives of a catalog's property still become its accessor.

The tool **does not promise a 100% automatic conversion**. The goal is to eliminate the
mechanical work and make explicit what needs to be thought through.

## 7. Per-plugin checklist

- [ ] Imports and a `plugin` block in place of the includes and `register_plugin`.
- [ ] Content IDs exported as `pub const` and imported by whoever depends on them.
- [ ] Names converted to camelCase (`set_props_var` → `setPropsVar`), or kept with `--keep-amxx-names`; the keys in the catalog's spelling (`"max_clip"` → `"maxClip"`).
- [ ] Every `get*Var`/`set*Var` key compiles, with units.
- [ ] No ephemeral handle stored: use `Ref` or `PlayerMap`/`EntityMap`.
- [ ] Callbacks by reference, without strings.
- [ ] Lifecycle forwards returning `Result` with their own `error enum`.
- [ ] Flow-control `setTask` calls reviewed (prefer `@system` or a coroutine).
- [ ] No `logInfo` on a hot path.
- [ ] `@test` tests for the main behavior.
