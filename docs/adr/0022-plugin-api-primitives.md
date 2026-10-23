# ADR-0022 — The plugin API as a few generic primitives in place of the old modules

- **Status:** Proposed
- **Date:** 2026-10-23

## Context

A Counter-Strike 1.6 server's mods stood on a pile of modules, each with its own conventions:
AMX Mod X's core, `fun`, `cstrike`, `engine`, `fakemeta`, Ham Sandwich, ReAPI, Orpheu, nVault,
SQLx, sockets, regex, JSON, the cell arrays and tries. Thousands of natives, many doing the same
thing another way: an entity's health was `get_user_health`, `pev(id, pev_health)`,
`get_entvar(id, var_health)` or a private-data offset; a spawn was caught by `Ham_Spawn`,
`RG_CBasePlayer_Spawn` or `FM_Spawn`. They reached the engine by offsets, enum names and
strings, and failed silently or with a generic run-time error.

The maintainer asks to take everything that worked and make one generic thing of it: smaller,
functional, fast, typed and modern, with typed errors and no exceptions, its systems fully
decoupled, clean and lean ([ADR-0021](0021-rook-amxx-names.md) keeps only AMX Mod X's look; none
of the old modules is carried over).

The planned API ([15](../15-mod-api.md)) is already typed, but it still keeps three ways where
one would do:

- **Forwards and post hooks** do the same thing: `playerKilled` is `PlayerKilled`'s `Post`,
  `playerSpawned` is `PlayerSpawn`'s ([15 §3 and §5](../15-mod-api.md#3-forwards)).
- **A weapon's virtuals and the entity hooks** are the same idea with two APIs:
  `bindWeaponVirtual` ([15 §4.4](../15-mod-api.md#44-weaponclass--getweaponclassvar--setweaponclassvar))
  and `EntityThink`, `EntityTouch`, `EntityTakeDamage` ([15 §5](../15-mod-api.md#5-hookable-actions)).
- **Two class systems**: the map's prefabs ([03 §11](../03-ecs.md#11-prefabs-and-map-entities))
  and `createWeaponClass`, with nothing for the classes a mod adds (a rocket, a boss, a pickup);
  and the weapon classes inherit from a parent (`parent: WEAPON_AK47`), which the maintainer rules
  out: a weapon is a new, standalone thing with a weapon's properties.

## Decision

Proposed: the plugin API is **eight primitives**, each one mechanism, owned by one engine module
and generated from its C++ declarations ([15 §1](../15-mod-api.md#1-overview)). Every capability
of the old modules is one of them.

| Primitive | What it is | Replaces |
|-----------|------------|----------|
| **1. Handles and `vars`** | Every piece of state is a typed property of a typed handle, read and written by its accessor, its key checked at compile time ([ADR-0012](0012-typed-get-set-vars-pattern.md)) | `pev`/`set_pev`, `get_entvar`/`set_entvar`, `get_member`/`set_member`, private-data offsets, `cs_get_*`/`cs_set_*`, `get_user_*`/`set_user_*` |
| **2. Types** | One mechanism for every new thing: a type of a kind the engine provides (entity, weapon, item…), standalone, with no parent: its values, components, own `vars`, methods and resources | `create_entity`, `rg_create_entity`, the map's prefabs, `createWeaponClass`, Orpheu's class tricks |
| **3. Methods and hooks** | Every action the game takes is a typed method of a kind; a type overrides it, any plugin hooks it | Ham Sandwich, ReAPI's hook chains, `register_forward`, `register_touch`/`register_think`, Orpheu |
| **4. Actions** | Typed functions for what is not a property: give, change team, create, remove, precache, emit a sound | Most of `amxmodx`, `fun` and `cstrike`, `engfunc`/`dllfunc` |
| **5. Queries** | Finding things with typed filters: players, entities in a radius or of a type, traces, navigation | `get_players` with string flags, `find_ent_*`, `engfunc(EngFunc_TraceLine)`, `fm_*` stocks |
| **6. Messages, effects and the HUD** | Typed structs sent to clients: the engine's effects, messages plugins declare, and HUD elements, retained: made once, their values bound, redrawn by the client when one changes | `message_begin`/`write_*`, `register_message`, `set_msg_arg_*`; `set_hudmessage`/`show_hudmessage`, `set_dhudmessage` and the tasks that resend them every second |
| **7. Commands, cvars and tasks** | Commands with typed arguments, typed cvars, tasks and coroutines on game time | `register_clcmd`/`read_argv`, `register_cvar`/`bind_pcvar_*`, `set_task` |
| **8. Services** | Storage (KV, SQLite), HTTP, logging, localization, behind capabilities | nVault, SQLx, sockets, `log_amx`, dictionaries |

The language's standard library covers the rest: collections (`List`, `Map`, `Set`), JSON, regular
expressions, math and vectors with units (cell arrays, tries, `xs`, `json`, `regex`).

### 2. Types: standalone, no parent

- The engine provides **kinds**, each with its generic behavior, its `vars` catalog and its
  methods: `Entity`, `Weapon`, `Item`, `Projectile`… (a `Player` is an `Entity`,
  [15 §4](../15-mod-api.md#4-handles-and-var-catalogs)). A **type** is a new thing of a kind, made
  once in data (`data/cstrike/entities.toml`, `data/cstrike/weapons/`, the prefabs of
  [03 §11](../03-ecs.md#11-prefabs-and-map-entities)) or by a plugin, and it is **standalone: no
  parent**. Its behavior is its kind's, driven by the values it sets (a weapon's fire interval,
  damage, spread, clip, models and sounds make it fire, reload and sound as it does); what it needs
  beyond is composed: components (an agent part, waves, a pusher), `vars` of its own, and the
  methods it overrides (§3). The resources it names are loaded as it names them, a map played or
  not: no precache list to make before the map, nor its limits.
- **`from:`** starts a type from another's values, copied once as it is made and linked to nothing
  after: changing the AK-47 later does not change a weapon that started from it. Without it, a
  type starts from none, incomplete until its values make one, and nothing gives an incomplete
  type.
- **Its `vars` are the key paths of its data** (`"magazine.clipSize"`, `"firing.cycle"`): one
  schema for its file, the console and plugins, with no second catalog of names to keep in step.

```rook
new Weapon:drakar = createWeapon("DrakarIII", WeaponType.Primary, from: WEAPON_AK47)?;
set_weapon_var(drakar, "name", "Drakar III");
set_weapon_var(drakar, "models.view", "models/rezombie/weapons/drakar/v.mdl");
set_weapon_var(drakar, "magazine.clipSize", 45);
overrideMethod(drakar, Weapon.primaryAttack, onDrakarAttack);
replaceWeapon(WEAPON_AK47, drakar)?;   // the Drakar wherever the game would make an AK-47
```

- The game's own weapons are types made the same way from its data: the AK-47 is one weapon among
  others, and nothing inherits from it. Changing the AK-47's values changes every AK-47; to put
  another weapon in its place, `replaceWeapon` makes the replacement wherever the game makes the
  original (buying, a loadout, a map's entity, `give`), one step, never a chain; a replacement
  only for some (a team, a player) is a `Pre` hook on `Player.giveWeapon` that changes its `inout`
  weapon.
- Each copy of a type in the world or in an inventory is an entity of it, with the `vars` of a
  copy: an item of a weapon (`WeaponItem`: its clip, its owner, its reload), a rocket.
  `spawnEntity(ROCKET, at: origin, angles: view, vars: { speed: 1500ups })?` creates one whole, in
  its final archetype at once, its defaults copied from the type's template, through the tick's
  command buffer, so that no entity is ever seen half made; `giveWeapon(player, drakar)?` gives one.
- Map entities take the same path: a `classname` names a type, its keyvalues become `vars` through
  the type's schema. A plugin may make types for its own maps' entities.
- A type may have an agent part (navigation, behaviors, perception, memory:
  [ADR-0024](0024-agents.md)): a monster, a boss or a hostage is a type with one.

### 3. Methods and hooks: one dispatch

- Each kind declares its methods as typed signatures: `Entity.spawn`, `Entity.think`,
  `Entity.touch`, `Entity.use`, `Entity.takeDamage`, `Entity.killed`, `Player.jump`,
  `Weapon.primaryAttack`, `GameRules.roundEnd`… The engine declares its own with an annotation on
  the C++ function that implements them; the table of [15 §5](../15-mod-api.md#5-hookable-actions)
  becomes the list of the engine's methods.
- **Override**: a type replaces its kind's method, and `callDefault` runs the kind's own, driven
  by the type's values:

```rook
fn onDrakarAttack(item: WeaponItem, owner: Player) -> WeaponReturn {
    return callDefault(item, owner);    // the weapon's attack, with the Drakar's values
}
```

- **Hook**: any plugin wraps a method of a kind, for every entity of it or for one type's, in a
  `Pre` or `Post` phase, with typed `inout` arguments and `HookResult`
  (`@hook(Player.takeDamage, HookPhase.Pre)`, `@hook(Entity.touch, HookPhase.Pre, type: ROCKET)`).
- **Forwards are post hooks**: `forward playerKilled(...)` is a shorter spelling of a `Post` hook
  on `Player.killed`, so that every event has one source. The plugin's own lifecycle
  (`pluginPrecache`, `pluginInit`, `pluginCfg`, `pluginEnd`) and the forwards API plugins declare
  for each other stay forwards.
- Dispatch is resolved when a method is overridden or hooked, never by a string at the call: a
  type's table points at the method's chain of hooks and its body.

### 6. The HUD: retained, not resent

A mod's HUD in AMX Mod X is text on one of four channels (`show_hudmessage`; eight for the
director's `show_dhudmessage`), resent by a task every second to stay on screen: each resend a
message to each player, the text laid out again, one channel's text erasing another's. Here a HUD
element is a handle with `vars`, as anything else (§1): made once for a player or for many, its
values bound by name, drawn by the client until it is removed or its player leaves.

```rook
new Hud:infected = createHud(allPlayers(), HudText {
    text: "Infected: {count}",
    position: HudPosition.TopCenter,
})?;
set_hud_var(infected, "count", 12);   // the clients draw the new value; nothing is sent until it changes
```

- A change sends the value that changed, once and reliably; nothing is sent while nothing
  changes, and the client lays out only the element that changed.
- No channels and no limit of elements: elements stack by their position, each in its own place.
- The element's kinds are typed (text, a bar, an icon, a timer the client counts down itself), each
  with its fields checked when the plugin compiles; a timer needs no task at all.

### Typed errors, no exceptions

Every fallible primitive returns `Result<T, E>`, `E` the owning module's `error enum`
([15 §9](../15-mod-api.md#9-error-catalog)), and `?` carries it; `PluginError` gathers them for a
plugin that does not declare its own. A bug is a `panic` that aborts the current callback and goes
through the server's failure policy ([14 §20](../14-rook-language.md#20-errors-panics-and-failures)).
Nothing throws, in Rook or in the engine ([ADR-0005](0005-typed-errors-without-exceptions.md)).

### Decoupled

- Each primitive is owned by the engine module that does the work (`nx.entity` the types,
  `nx.trace` the traces, `cs.economy` the money) and declared there; `nx.script` only generates the
  bindings and the `.rookd` declarations from those annotations. No module of the API knows
  another's internals, and no plugin sees an engine structure.
- A plugin depends on the API's modules it imports and on the API plugins it imports, nothing
  else ([13 §3.2](../13-mod-system.md#32-automatic-load-order)).

### Lean and fast

- One declaration of each thing: the generator writes the bindings, the declarations, the LSP's
  completion and the reference documentation.
- No string at run time: a `vars` key, a method and a hook are resolved at compile or registration
  time to an index; handles are generational integers.
- A type's template makes creation a copy into its archetype; creating many at once is one call.
- Pure functions run in jobs ([14 §17](../14-rook-language.md#17-concurrency-and-determinism));
  hook chains and queries are benchmarked as hot paths ([RULES §8](../RULES.md#8-performance)).

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Rebuild each old module as its own Rook module (`fun`, `cstrike`, `fakemeta`, `hamsandwich`, `reapi`) | Thousands of overlapping functions, the same property reached five ways, and the offsets and strings they were made of; what ADR-0021 rules out |
| The API of [15](../15-mod-api.md) as it is | Typed, but forwards beside post hooks, weapon virtuals beside entity hooks, and two class systems: three things to learn, document and keep where one does |
| A parent, required or optional, that a type inherits from as the game runs (`parent: WEAPON_AK47`) | A change to the parent changes every child unseen, values are looked up along a chain, and the outcome depends on the order plugins change them; the rules want composition, inheritance only for interfaces ([RULES §5.1](../RULES.md#51-structure)). `from:` gives the same start without the link |
| Hooks only, no overriding (a type's behavior as a hook filtered by type) | A type's own behavior would hang on registration order, and `callDefault` would have no place |
| Types as Rook classes (`class Rocket extends Entity`) | Rook classes live in one plugin's isolate and its collector; a type is shared by every plugin, by the map and by the network, and its copies live in the world |
| A dynamic object model (properties and methods by name at run time) | What Pawn's strings were: no compile-time checking, a lookup at every call |

## Consequences

- ✅ Eight things to learn instead of a dozen modules, each with one way to do it.
- ✅ Everything the old modules did has its place, typed, with typed errors.
- ✅ A mod's weapon, rocket, monster or boss is a type like the game's own, standalone, made in
  one call, its behavior in its values and in the methods it overrides, hooked by other plugins
  like any engine type.
- ✅ The engine stays free to change: plugins see methods, handles and `vars`, never its structures.
- ✅ A mod's HUD is made once and sends only what changes: no channels, no limit, no task resending
  it every second.
- ⚠️ Every hookable action must be a declared method of a kind, its arguments a typed struct.
  Mitigation: the annotation on the C++ function generates both; the CI's API check
  ([15 §10](../15-mod-api.md#10-versioning-and-stability)) keeps them stable.
- ⚠️ Hook chains on hot methods (`Player.takeDamage`, `Entity.think`) cost a call each. Mitigation:
  a method without hooks calls its body directly, and the chains are benchmarked.
- ⚠️ Forwards become a spelling of post hooks, which changes [15 §3](../15-mod-api.md#3-forwards).
- ⚠️ Names change in [15 §4](../15-mod-api.md#4-handles-and-var-catalogs): a weapon's type is a
  `Weapon`, made by `createWeapon` (today's `WeaponClass`), and a copy of it a `WeaponItem` (today's
  `Weapon`).

If accepted, the same change amends [03 §11](../03-ecs.md#11-prefabs-and-map-entities),
[13 §2](../13-mod-system.md#2-vocabulary-and-equivalents), [14 §15](../14-rook-language.md#15-forwards-hooks-commands-and-cvars),
[15 §2 to §6.4 and §9](../15-mod-api.md) and the M6 items of `TODO.md`.
