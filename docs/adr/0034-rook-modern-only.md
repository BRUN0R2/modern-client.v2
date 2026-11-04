# ADR-0034 — Rook looks like AMX Pawn and is new inside: one spelling, nothing ported, typed to the maximum

- **Status:** Proposed
- **Date:** 2026-11-04

## Context

The maintainer decided that nothing of AMX Mod X is ported: the language only has to look like it, and
everything inside can be new. It must be modern and typed to the maximum, and ReZombie will be
rewritten from scratch in Rook. Compatibility was the reason behind several decisions, and it no
longer holds:

- [ADR-0021](0021-rook-amxx-names.md) gave Rook a second spelling, the `amxx` layer: `#include`,
  `new`, AMX Mod X's natives and `get_player_var`, with an exception to
  [ADR-0013](0013-camelcase-convention.md), a softer `naming` lint and a `compatibilityName` lint.
- [ADR-0012](0012-typed-get-set-vars-pattern.md) chose `getPlayerVar(player, "health")` over fields
  because fields are "further from what the community knows", and recorded that "the type checking
  would be equivalent".
- [16](../16-amxx-migration.md) and `rook migrate` exist to carry plugins over.
- `setTask` is a port of `set_task`: its owner is optional, nothing stops arming it twice, and an
  error inside it has nowhere to go ([14 §16](../14-rook-language.md#16-tasks-and-coroutines)).
- The API inherited names that are cut (`registerClcmd`, `pluginCfg`), run together
  (`clientPutinserver`) or misspelled (`Supercede`).
- A unit's suffix on a field that already has that type repeats it: `player.health = 100hp`.

Asked directly, the maintainer chose one modern spelling, fields over accessors, no suffix where the
type already says it, parentheses around conditions, and no port of `setTask`.

## Decision

Proposed: Rook has one spelling that looks like AMX Pawn, and owes it nothing else. Looking like it
is a goal; carrying a behavior or an API because AMX Mod X has one is not. Decisions 1 to 3 and 7 are
the maintainer's, as is ruling out `setTask` in 6; the rest follow from them and are proposed for
review.

```rook
import cs.prelude.*;

plugin { name: "Hello", version: "1.0.0", author: "Modern-Client.v2" }

forward pluginInit() -> Result<void, PluginError> {
    registerClientCommand("say /hello", cmdHello)?;
    return ok();
}

fn cmdHello(player: Player) -> PluginResult {
    if (!player.isAlive) {
        return PluginResult.Handled;
    }
    player.health = 100;
    clientPrint(player, PrintTarget.Chat, "Hello, {player.name}!");
    return PluginResult.Handled;
}
```

### 1. One spelling, with AMX Pawn's look

This supersedes ADR-0021. These go away: the `amxx.*` modules, `#include`, `new`, the underscored
accessors, the `compatibilityName` lint, the softer `naming` in some files, `rook fmt --canonical`,
`rook migrate` with `--keep-amxx-names`, and [16](../16-amxx-migration.md). Declarations are `let`
and `var`, and ADR-0013 holds without an exception.

The look stays: a C-like syntax, semicolons, `plugin`, `forward`, and actions as function calls.
Parentheses and braces are mandatory in control flow, in one form only:

```rook
if (!player.isAlive) { ... }
if (let target = findTarget(player)) { ... }
while (count > 0) { ... }
for (player in players) { ... }
match (team) { ... }
```

A condition still accepts only a `bool`. The vocabulary table of
[13 §2](../13-mod-system.md#2-vocabulary-and-equivalents) remains a reading guide for people who know
AMX Mod X, and promises nothing.

### 2. Properties are fields

This supersedes ADR-0012.

```rook
let health = player.health;       // Health
player.health = 100;
player.helth = 100;               // error R0312: no field `helth` on `Player`; did you mean `health`?
player.health = player.armor;     // error R0201: expected `Health`, found `Armor`
player.isAlive = true;            // error R0313: `isAlive` is readonly
```

- A `vars` catalog stays the declaration of a handle's fields: type, default, `readonly` and
  documentation. No accessor is generated, no key is a string, and a field compiles to an index.
- A handle has the fields of those above it ([15 §4](../15-mod-api.md#4-handles-and-var-catalogs)).
  Its own field hides one of the same name: `weapon.owner` is the `Weapon`'s `Player?`, and through
  `let entity: Entity = weapon;`, `entity.owner` is the `Entity`'s `EntityRef?`.
- **A field write only stores.** Whatever applies rules or raises an event is a named action and the
  field is `readonly`, as `team` and `changeTeam` are today: `player.money` becomes `readonly`, changed
  by `setMoney(player, 800, reason)?`. No write hides a flow ([RULES §2](../RULES.md#2-philosophy)),
  and none can end a checked state ([ADR-0033](0033-rook-compiler-and-types.md)).
- `getVarDynamic` and `setVarDynamic` stay, for admin and debugging tools.

### 3. A literal takes the type its place asks for

```rook
player.health = 100;            // ok: `Health`
player.health = 99.5;           // ok
player.maxSpeed = 250;          // ok: `Speed`
setMoney(player, 800, reason)?; // ok: `Money`, the same rule at a parameter
weapon.accuracy = 1;            // ok: `f32`
player.frags = 1.5;             // error R0204: expected `i32`, found a fraction

await wait(3);                  // error R0205: `Duration` has several units; write `3s` or `3ms`
await wait(3s);                 // ok
player.fieldOfView = 90deg;     // ok

let amount = 100;               // i32
player.health = amount;         // error R0201: expected `Health`, found `i32`
let bonus = Health(25);         // nothing asks for a type here, so it is named
let limit: Health = 5000;       // ok
```

- **Only a literal adapts, and only while compiling.** A value that has a type keeps it, so
  conversions stay never implicit ([14 §5](../14-rook-language.md#5-primitive-types-and-conversions)).
  In an operation, the literal takes the type the operation asks of it (`player.health + 50`).
- **A suffix exists only where a number alone cannot say which unit it means:** `Duration` (`ms`, `s`,
  `min`) and `Angle` (`deg`, `rad`). `hp`, `ap`, `dmg`, `ups`, `hu` and `x` go away.
- A literal in a place typed as a unit counts as having one for the `magicNumber` lint, which is what
  `25hp` was: nothing gets looser. A plain number in a place typed `i32` or `f32` stays under the lint.

### 4. Stricter defaults

No carried plugin has to compile any more, so these go from warnings to errors:
`expectInGameplay` and `wildcardHidesVariants`; `naming` is an error everywhere.

A newtype may declare a range. A literal is checked while compiling, a value known only at run time
goes through `tryFrom`, and a cvar takes its limits from its type.

```rook
newtype Volume(f32) range(0..=1);

sound.volume = 1.5;                    // error R0206: `Volume` goes from 0 to 1
let volume = Volume.tryFrom(value)?;   // Result<Volume, ConversionError>
```

### 5. Names

A name stays if it looks like AMX Mod X's, is true today and is written in full. These stay:
`forward`, `plugin`, `pluginPrecache`, `pluginInit`, `pluginEnd`, `pluginPause`, `pluginUnpause`,
`precacheModel`, `precacheSound`, `precacheSprite`, `precacheGeneric`, `clientConnect`,
`clientAuthorized`, `clientDisconnected`, `clientPrint`, `clientPrintColor`, `createCvar`,
`executeForward`, `playerSpawned`, `playerKilled`, `roundStart`, `HookResult.Continue` and
`HookResult.Break`. Keywords and primitive types (`fn`, `pub`, `i32`) are not names in this sense.

| Today | Proposed | Why |
|-------|----------|-----|
| `pluginCfg` | `pluginConfiguration` | Cut |
| `clientPutinserver`, `clientInfochanged` | `clientPutInServer`, `clientInformationChanged` | Run together, cut |
| `registerClcmd`, `registerConcmd` | `registerClientCommand`, `registerConsoleCommand` | Cut |
| `serverCmd` | `serverCommand` | Cut |
| `setTask`, `removeTask`, `TaskHandle` | `startTask`, `cancelTask`, `Task` | A new mechanism (§6) |
| `HookResult.Supercede` | `HookResult.Supersede` | Misspelled |
| `tr` | `translate` | Cut |
| `kvOpen`, `kvGet`, `kvSet` | `openStore`, `readStore`, `writeStore` | Cut |
| `logWarn` | `logWarning` | Cut |
| `hasNvg`, `scoreCt`, `renderFx`, `fov`, `mins`, `maxs` | `hasNightVision`, `scoreCounterTerrorist`, `renderEffect`, `fieldOfView`, `minimumBounds`, `maximumBounds` | Cut |
| `rook fmt`, `rook doc` | `rook format`, `rook document` | Cut |

### 6. Every task has an owner

`setTask` is not ported. A task is an `async fn` that belongs to something and cannot outlive it, the
structured concurrency of Kotlin, Swift and Verse. It still reads as one call:

```rook
fn burn(player: AlivePlayer) -> Result<void, TaskError> {
    startTask(player, burnDamage)?;   // ends by itself if the player dies or leaves
    return ok();
}

async fn burnDamage(player: AlivePlayer) -> Result<void, PluginError> {
    for (_ in 0..BURN_TICKS) {
        await wait(1s);
        takeDamage(player, BURN_DAMAGE);   // `player` is still valid after the `await`
    }
    return ok();
}
```

| Rule | What it closes |
|------|----------------|
| The owner is mandatory, and the function's first parameter has its type: an `AlivePlayer` (dies or leaves), a `Player` (leaves), an `Entity` (removed), a `Round` (ends) or the plugin | A task that outlives what it acts on |
| The VM checks the owner before resuming; if it is gone, the task is cancelled and never resumes | A stale handle after a wait, with no check to write |
| `startTask` returns a `Result`, `TaskError.AlreadyRunning` for the same function and owner; `restartTask` replaces the running one | A timer armed twice by accident |
| Cancellation happens only at an `await`, and `defer` runs when a task ends for any reason | State left half done |
| A task returns a `Result`, and an `err` goes to the log and to `rook_fault_policy` | An error with nowhere to go |
| A quota of tasks per plugin, and the fuel of each step | Endless tasks, and a loop that never waits |
| A plugin's tasks are cancelled on reload and unload, and a `Task` cannot be `persist` | A task running old code |
| No flags: repeating is a `for` or a `loop` with `await wait(1s)`, and a delay is a `wait` on the first line | A second mechanism for each case |

- Time stays game time, and the order stays deterministic by deadline, then by creation
  ([14 §16](../14-rook-language.md#16-tasks-and-coroutines)).
- The owner is the one exception to the rule that an ephemeral handle does not cross an `await`
  ([14 §13.1](../14-rook-language.md#131-handles)), sound because of the check before resuming. Any
  other handle crosses as a `Ref`, and `resolve` gives it back as optional.
- Inside `defer` the owner may be gone, so there it is optional (`Player?`), and `defer` cannot
  `await`.

### 7. ReZombie is written from scratch

ReZombie is the reference mod, written in Rook on this API and designed anew. The items that port it
become items that write it, and the converted examples of 16 go with that document. The proof of
ADR-0033's order is unchanged: the native `mod.zombies` rewritten in Rook and measured first.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| The `amxx` layer as an option | Two spellings of the API to keep, document and check for good, for plugins that are not coming over |
| Keeping `new Type:name` and `#include` | Pawn's most recognizable traits, but `new` is mutable by default, with the type where `let x: T` does not put it. The maintainer chose `let`, `var` and `import` |
| Keeping `get*Var`/`set*Var` | As safe as fields, but strings in the code and a special rule in the compiler. The maintainer chose fields |
| Fields whose writes run rules (`player.money = 800` raising `moneyChanged`) | Shorter, but a hidden flow that looks like storing a value |
| The suffix always required (`100hp`) | One rule for every unit and the unit in plain sight, but it repeats what the field's type says. The maintainer ruled it out |
| The suffix optional | Two ways to write the same value |
| A default unit for `Duration` and `Angle` (`wait(3)` in seconds) | Shorter, but a reader has to know the default, and `3` would read differently from `3ms` |
| Parentheses optional, or none as in Rust and Go | Optional gives two forms of one thing; none loses the look the maintainer wants |
| Keeping `rook migrate` for the community | Most of a converter's work is guessing types from Pawn's untyped cells, its output needs rewriting by hand, and it would tie the language to Pawn's shapes |
| `on`, `event` and `emit` in place of `forward`, and `pluginLoad` and `loadModel` in place of precache | Plain words, proposed first in this ADR; but the old ones are true today and carry the look, so changing them gains little |
| `playerConnecting` and `printToPlayer` in place of the `client*` names | More consistent with `player`, but they lose the look for a small gain |
| `setTask` with a mandatory owner, or renamed `runAfter` | Fewer new names, but a second mechanism beside `async`, with flags or a separate call for each case |
| A timer per scope with no `async` (`player.tasks.add(...)`) | Simple to host, but a sequence of steps goes back to being spread over many functions |

## Consequences

- ✅ One way to write each thing: a smaller grammar, compiler, API surface and documentation.
- ✅ No property is reached through a string, and no special rule for literal keys remains.
- ✅ No hidden flow behind a field write.
- ✅ A task cannot outlive its owner, and stopping when a player dies takes no code.
- ✅ ADR-0013 holds without exceptions, and less of AMX Mod X's interface is borrowed, which narrows
  the pending legal review ([20](../20-legal-and-licensing.md)).
- ⚠️ No existing plugin comes over, so the ecosystem starts empty. Mitigation: the built-in mods
  ([13 §12](../13-mod-system.md#12-built-in-mods-replacements-for-the-default-amxx-plugins)) and
  ReZombie come first, and serve as examples.
- ⚠️ False familiarity: code that looks like AMX Mod X's does not behave like it (`clientPrint` takes a
  `Player`, not an index). Mitigation: 13 §2 maps the concepts, and each diagnostic gives the fix.
- ⚠️ `player.maxSpeed = 250` does not show its unit on the line. Mitigation: the field's type is one
  hover away, and mixing up units stays an error.
- ⚠️ A literal's type depends on its place, which is one more rule for the checker. Mitigation: the
  conformance suite covers each place a literal can stand.
- ⚠️ Properties with rules lose the short form. Mitigation: their action names what happens.
- ⚠️ `async` becomes required from the first task on, and each task keeps a function's state. With
  thousands of zombies that may weigh. Mitigation: ADR-0033's measured proof, before anything is built
  on it.
- ⚠️ A plain field of a task's owner can look safe where it is not, in `defer`. Mitigation: the owner
  is optional there, so the compiler asks for the check.

If accepted, the same change marks ADR-0012 and ADR-0021 as superseded by this ADR; amends
[ADR-0006](0006-own-plugin-language-rook.md), ADR-0013, [ADR-0022](0022-plugin-api-primitives.md),
[ADR-0024](0024-agents.md), [ADR-0028](0028-native-agent-test-mod.md) and ADR-0033; removes
[16](../16-amxx-migration.md); and amends [00](../00-overview.md),
[02 §3.1](../02-coding-standards.md#31-project-rule-camelcase), [13](../13-mod-system.md),
[14](../14-rook-language.md), [15](../15-mod-api.md), [17](../17-build-and-tools.md),
[19](../19-roadmap.md), [20](../20-legal-and-licensing.md), [docs/README](../README.md) and the M6
items of `TODO.md`.
