# 14 — Rook language

> **Rook**: the chess piece that comes after the *Pawn*. Working name
> ([ADR-0006](adr/0006-own-plugin-language-rook.md)).
> `.rook` extension · `rookc` compiler · `.rbc` bytecode.

## 1. Design goals

1. **Easy migration from Pawn/AMXX:** C-like syntax, semicolons, `forward`,
   the `get*Var`/`set*Var` pattern and the AMXX lifecycle names, written in camelCase
   like the whole project (`getWeaponVar`, `pluginPrecache`, `pluginInit`).
2. **Stronger typing than TypeScript:** nominal types, no `any`, no `null`, no
   implicit coercion, errors in the return type, exhaustive `match` and units of measure.
3. **Safe by construction:** handles that never dangle, no raw memory access,
   sandbox and budgets in the VM.
4. **Deterministic:** the same state and the same inputs produce the same result.
5. **Fast enough for gameplay:** static types, register bytecode,
   no dynamic type checks.
6. **First-class tooling:** incremental compiler, LSP, formatter, linter,
   debugger and tests.

## 2. Comparison: Pawn × TypeScript × Rook

| Aspect | Pawn (AMXX) | TypeScript | Rook |
|--------|-------------|------------|------|
| Base type | Untyped 32-bit cell, weak *tags* | Structural, with `any` | **Nominal**, no `any`/`unknown` |
| Null | `0` as an invalid handle | `null`/`undefined`, `!` to force | **No null**: `T?` with mandatory handling and no `!` |
| Errors | Return code or `log_error` | Untyped exceptions | `Result<T, E>` with `error enum` and `?`. Ignoring a `Result` is an error |
| Conversions | Implicit between tags, with a warning | Coercion (`"1" + 1`) | **No** implicit ones (except `T → T?`) |
| Integers | 32-bit cell, silent *wrap* | `number` (double) | Fixed widths and **detected overflow** |
| Units | None (`Float:` for everything) | None | `320ups`, `5000hp`, `3s`, `64hu` as distinct types |
| Structs | Do not exist (arrays and enums) | Structural objects | `struct` by value, `class` by reference |
| Enums | Integer constants | Integers or strings | Sum types with data, no conversion to integer |
| `switch` | No exhaustiveness | No mandatory exhaustiveness | Mandatory **exhaustive** `match` |
| Conditions | Any cell | *Truthiness* | Only `bool` |
| Properties by string | No checking | `keyof` (structural) | Literal key checked at compile time, with type and unit |
| Entity handles | Reusable index | — | Generational handle that **cannot** be stored without `Ref` |
| Mutability | Everything mutable | `const` only for the binding | `let` immutable by default |
| Concurrency | Single thread | Event loop | Per-plugin isolates and `pure fn` in jobs |

## 3. First plugin

```rook
import cs.prelude.*;

plugin {
    name:    "Hello",
    version: "1.0.0",
    author:  "Modern-Client.v2",
}

forward pluginInit() -> Result<void, PluginError> {
    registerClcmd("say /hello", cmdHello)?;
    return ok();
}

fn cmdHello(player: Player) -> PluginResult {
    let name = getPlayerVar(player, "name");
    clientPrint(player, PrintTarget.Chat, "Hello, {name}!");
    return PluginResult.Handled;
}
```

## 4. Lexical structure

- Source in **UTF-8**. Semicolons are **mandatory** (like `#pragma semicolon 1`).
- Comments: `//`, `/* ... */` and `///` documentation (feeds `rook doc` and shows up in the LSP).
- Naming (checked by the linter), following the project's camelCase rule
  ([02 §3.1](02-coding-standards.md#31-project-rule-camelcase)):

| Element | Style | Example |
|---------|-------|---------|
| Functions, variables, parameters, modules, `vars` keys, fields | `camelCase` | `setWeaponVar`, `maxClip`, `pluginPrecache` |
| Types, enums, variants, interfaces | `PascalCase` | `WeaponClass`, `Team.Zombie` |
| Constants | `SCREAMING_SNAKE_CASE` | `MAX_INFECTION_DELAY` |
| Cvar and console command names (strings) | Classic console name | `"rz_infection_delay"`, `"say /menu"` |

- Literals:

| Literal | Examples |
|---------|----------|
| Integer | `42`, `1_000_000`, `0xFF`, `0b1010` (default type `i32`) |
| Floating point | `0.75`, `1.5e3` (default type `f32`) |
| With a unit | `5000hp`, `100ap`, `320ups`, `64hu`, `1.5s`, `250ms`, `90deg`, `0.75x` |
| Boolean | `true`, `false` |
| Character | `'a'`, `'\n'` |
| String | `"text {interpolation}"`, `r"no escapes"` |

- Reserved words: `plugin`, `import`, `pub`, `fn`, `pure`, `async`, `await`, `let`,
  `var`, `const`, `persist`, `if`, `else`, `match`, `while`, `for`, `in`, `loop`, `break`,
  `continue`, `return`, `struct`, `enum`, `error`, `class`, `open`, `abstract`, `override`,
  `super`, `self`, `interface`, `impl`, `newtype`, `suffix`, `handle`, `vars`, `readonly`,
  `component`, `forward`, `type`, `inout`, `is`, `true`, `false`, `none`, `some`, `ok`,
  `err`, `void`.

## 5. Primitive types and conversions

| Type | Description |
|------|-------------|
| `bool` | `true`/`false` |
| `i8`, `i16`, `i32`, `i64` | Signed integers |
| `u8`, `u16`, `u32`, `u64` | Unsigned integers |
| `f32`, `f64` | IEEE 754 |
| `char` | Unicode code point |
| `string` | Immutable UTF-8 |
| `void` | Absence of a value (`Result<void, E>`) |

**Arithmetic:** overflow and division by zero raise a `panic` (the callback is aborted with a
typed failure). For other behaviors, the choice is explicit:

```rook
let a = x.wrappingAdd(y);     // wraps around
let b = x.saturatingAdd(y);   // clamps at the type bounds
let c = x.checkedAdd(y);      // i32? (none on overflow)
```

**Conversions** are never implicit:

| From → to | How |
|-----------|-----|
| Lossless (`i32 → i64`, `i32 → f64`) | `i64(x)`, `f64(x)` |
| Possibly lossy (`i64 → i32`) | `i32.tryFrom(x)` → `Result<i32, ConversionError>` |
| Float → integer | `x.roundToI32()`, `x.floorToI32()`, `x.truncToI32()` → `Result<i32, ConversionError>` |
| Any → string | Interpolation `"{x}"` or `x.toString()` |
| String → number | `i32.parse(s)` → `Result<i32, ParseError>` |

## 6. Units and newtypes

A **newtype** creates a new type, incompatible with its base, which can have a literal suffix
and permitted operations:

```rook
newtype Health(f32) suffix hp derive(Ord, Add, Sub, Scale);
newtype Speed(f32) suffix ups derive(Ord, Add, Sub, Scale);
```

```rook
let a: Health = 100hp;
let b: Armor  = 100ap;
let c = a + b;          // error R0201: cannot add `Health` and `Armor`
let d = a + 50;         // error R0204: expected `Health`, found integer literal; did you mean `50hp`?
let e = a.value();      // f32, explicit escape when really needed
```

Standard library units:

| Type | Suffixes | Base | Meaning |
|------|----------|------|---------|
| `Health` | `hp` | `f32` | Health points (fractional as in the original. The HUD shows the integer by the original rule, with no cap) |
| `Armor` | `ap` | `f32` | Armor points |
| `Damage` | `dmg` | `f32` | Raw damage, before armor and multipliers |
| `Money` | — (use `Money(800)`) | `i32` | Money |
| `AmmoCount` | — (use `AmmoCount(30)`) | `i32` | Ammunition |
| `Distance` | `hu` | `f32` | Map units (*hammer units*) |
| `Speed` | `ups` | `f32` | Units per second |
| `Duration` | `ms`, `s`, `min` | `i64` (µs) | Game time |
| `Angle` | `deg`, `rad` | `f32` | Angles |
| `Scale` | `x` | `f32` | Multipliers (gravity, damage) |

Standard library dimensional operations: `Distance / Duration → Speed`,
`Speed * Duration → Distance`, `Health − Damage → Health`, `Damage * Scale → Damage`,
`Health * Scale → Health`, `Duration * f32 → Duration`.

## 7. Compound types

### 7.1 Structs (value)

```rook
pub struct HudText {
    message:  string,
    position: HudPosition = HudPosition.CENTER,
    color:    Color = Color.WHITE,
    hold:     Duration = 3s,
}

let text = HudText { message: "Infection!", hold: 5s };   // omitted fields use defaults
let bad  = HudText { mesage: "x" };                        // error R0310: unknown field `mesage`
```

Fields without a default value are mandatory in the literal. Structs are copied on
assignment or when passed as an argument.

### 7.2 Enums (sum types)

```rook
pub enum Team { Terrorist, CounterTerrorist, Spectator }

pub enum DamageSource {
    Weapon(weapon: Weapon),
    Fall(speed: Speed),
    Explosion(radius: Distance),
    World,
}
```

Enums do **not** convert to integers. `team.ordinal()` exists for explicit serialization.
When the expected type is known, the variant can be abbreviated: `.Terrorist`.

### 7.3 Tuples, arrays and collections

| Type | Semantics |
|------|-----------|
| `(A, B)` | Tuple by value |
| `[T; N]` | Fixed-size array by value, with checked indexing |
| `List<T>` | Dynamic list (reference) |
| `Map<K, V>` | Map with **insertion order** (deterministic iteration) |
| `Set<T>` | Set with insertion order |
| `PlayerMap<T>` | Per-player data, **cleared automatically** when the player disconnects |
| `EntityMap<T>` | Per-entity data, cleared automatically when the entity is destroyed |

`PlayerMap` replaces the AMXX `new g_data[MAX_PLAYERS + 1]` pattern and eliminates the classic bug
of forgetting to reset data in `clientDisconnected`.

## 8. Option and Result

### 8.1 No null

```rook
let target: Player? = findPlayerByName("Arctic");

if let player = target {
    giveItem(player, ItemId.Ak47);
}

let name: string = match target {
    some(player) => getPlayerVar(player, "name"),
    none         => "nobody",
};

let respawnDelay = customDelay ?? DEFAULT_RESPAWN_DELAY;   // `??` supplies a default
```

- `T` → `T?` is the **only** implicit conversion in the language.
- There is no operator to "force" an optional. To assert that something exists, use
  `match`, `if let` or `.expect("reason")`, which raises a `panic` with the message and is
  flagged by the linter on gameplay paths.

### 8.2 Typed errors

```rook
pub error enum ClassError {
    @message("class \"{id}\" is already registered")
    AlreadyRegistered(id: ClassId),

    @message("classes can only be registered during pluginPrecache")
    RegistrationClosed,
}

pub error enum PrecacheError {
    from Class(ClassError),          // `?` converts ClassError automatically
    from Resource(ResourceError),
}

pub fn createClass(id: ClassId, team: Team) -> Result<Class, ClassError> {
    if !isPrecachePhase() {
        return err(ClassError.RegistrationClosed);
    }
    if classes.containsKey(id) {
        return err(ClassError.AlreadyRegistered(id: id));
    }
    ...
}
```

`ClassId` is a string newtype created by `classId("assassin")`, a `const fn` that
validates the ID format **at compile time**. The owning plugin exports the ID as a constant
(`pub const ASSASSIN: ClassId = classId("assassin");`), and whoever imports it gets
name checking and a load dependency ([§19](#19-modules-imports-and-visibility)).

- Ignoring a `Result` is a **compile error** (`R0410`). To discard one on purpose:
  `let _ = ...;` with a comment.
- `?` only works when the source error has a `from` variant in the target error.
  There is no "generic" conversion.

## 9. Variables, constants and scope

```rook
const MAX_ZOMBIE_SPEED: Speed = 320ups;    // compile-time constant
let delay = 10s;                            // immutable local
var infectedCount = 0;                     // mutable local

var roundCounter: i32 = 0;                 // plugin global (lives in the isolate)
persist var totalInfections: i64 = 0;      // survives hot reload (matched by name and type)
```

- Every variable is initialized at its declaration (no "garbage" and no implicit zero).
- *Shadowing* in a nested scope is forbidden (`R0120`).
- Globals exist (as in Pawn), but they belong to the plugin's isolate and are not visible
  to other plugins, except through `pub` functions.

## 10. Functions, closures and generics

```rook
pub fn giveHealth(player: Player, amount: Health, limit: Health = 100hp) -> Health {
    let current = getPlayerVar(player, "health");
    let updated = min(current + amount, limit);
    setPlayerVar(player, "health", updated);
    return updated;
}

giveHealth(player, 25hp);
giveHealth(player, amount: 50hp, limit: 200hp);   // named arguments (like Pawn's .name = value)
```

- No function overloading: distinct names or generics.
- `inout` parameters allow modifying the argument, with an explicit `&` at the call site:

```rook
fn clampSpeed(inout velocity: Vector3, limit: Speed) { ... }
clampSpeed(&velocity, MAX_ZOMBIE_SPEED);
```

- Closures: `(player) => isAlive(player)` or `(player) => { ...; return x; }`.
- Generics with interface constraints:

```rook
pub fn strongest<T: HasHealth>(items: List<T>) -> T? { ... }
```

- `const fn`: evaluated at compile time. Used in IDs (`classId("human")`) and derived
  constants, with the argument validated at build time.
- `pure fn`: forbidden from accessing the world, mutable globals and non-`pure` functions. It
  can run in jobs ([§17](#17-concurrency-and-determinism)).

## 11. Control flow and match

```rook
let reward = match reason {
    RoundEndReason.TargetBombed        => economy.bombWin,
    RoundEndReason.BombDefused         => economy.defuseWin,
    RoundEndReason.CTsWin
    | RoundEndReason.TerroristsWin     => economy.eliminationWin,
    _                                  => economy.defaultWin,
};

match source {
    .Weapon(weapon) if isKnife(weapon) => applyKnifeBonus(weapon),
    .Weapon(weapon)                     => applyWeaponDamage(weapon),
    .Fall(speed)                        => applyFall(speed),
    .Explosion(radius)                  => applyExplosion(radius),
    .World                              => {},
}
```

- `if`, `while`, `for x in collection`, `for i in 0..count`, `loop`, `break`, `continue`
  and labels (`'outer: for ...`).
- `match` is **exhaustive**. `_` is only allowed when there are remaining variants (a lint
  warns when `_` hides new variants of an enum from the plugin itself).
- `if` and `match` are expressions.
- Conditions only accept `bool`: `if (count)` is an error and must be `if count > 0`.

## 12. Strings, formatting and translation

```rook
let money = getPlayerVar(player, "money");
clientPrint(player, .Chat, "You have {money}.");
let line = "{name:<16} {frags:>4} {deaths:>4}";      // alignment and width
```

- Interpolation checked at compile time. Format specifiers are validated against
  the type.
- **Translation** with typed keys:

```rook
clientPrint(player, .Center, tr("RZ_INFECTED_BY", infected: victimName, by: attackerName));
```

```toml
# locale/pt-BR.toml
RZ_INFECTED_BY = "{infected} foi infectado por {by}!"
```

`rookc` reads the mod's locale files. A missing key, or a missing or extra parameter, is
a compile error. The language is chosen per player (`cl_language`).

## 13. Handles, vars and the get/set pattern

### 13.1 Handles

A `handle` is an opaque, typed identifier for an object that lives in the engine or in
another plugin (entity, player, weapon, ReZombie class…):

```rook
pub handle Class;
pub handle Props;
```

- Handles are **generational**: a handle to something destroyed never comes to point to
  something else.
- Handles to short-lived objects (`Player`, `Entity`, `Weapon`) are **ephemeral**:
  valid until the end of the current callback. The compiler **forbids** storing them in globals,
  fields, components or scheduled closures, and keeping them across an `await`. To store one,
  use its `Ref` counterpart:

```rook
var lastAttacker: PlayerRef? = none;

fn onDamage(victim: Player, attacker: Player) {
    lastAttacker = refOf(attacker);          // ok: Ref can be stored
}

fn later() {
    if let attacker = resolve(lastAttacker) { // Player? (none if disconnected)
        ...
    }
}
```

This eliminates, at compile time, the AMXX class of bugs in which a stored `id` comes to
point to another player.

### 13.2 Variable catalogs (`vars`)

The pattern the community knows (`getEntityVar`, `setWeaponVar`,
`setClassVar`…) is the main way to read and write properties. Each handle has a declared
**`vars` catalog**:

```rook
/// Gameplay properties shared by classes and subclasses.
pub handle Props;

pub vars Props {
    health:              Health = 100hp,
    armor:               Armor = 0ap,
    speed:               Speed = 250ups,
    gravity:             Scale = 1.0x,
    weaponsInteraction: bool = true,
    readonly id:         string,
}
```

The compiler generates `getPropsVar` and `setPropsVar`, in which **the literal key defines the
type**:

```rook
setPropsVar(props, "health", 5000hp);       // ok
setPropsVar(props, "health", 5000);         // error R0204: expected `Health`; did you mean `5000hp`?
setPropsVar(props, "helth", 5000hp);        // error R0312: unknown var "helth"; did you mean "health"?
setPropsVar(props, "id", "x");              // error R0313: var "id" is readonly
let speed = getPropsVar(props, "speed");    // speed: Speed
```

- The key must be a **literal**. Dynamic access by string exists only in
  `getVarDynamic`/`setVarDynamic`, which return `Result<VarValue, VarError>` and are
  meant for admin and debugging tools.
- The LSP autocompletes the keys and shows the type, default and documentation of each one.
- The engine provides catalogs for `Entity`, `Player`, `Weapon`, `WeaponClass`,
  `WeaponDef` and others ([15 §4](15-mod-api.md#4-handles-and-var-catalogs)).

## 14. Classes and interfaces

```rook
pub interface Damageable {
    fn takeDamage(self, amount: Health, source: DamageSource) -> Health;
}

pub abstract class Ability {
    var cooldown: Duration;

    pub fn new(cooldown: Duration) -> Self { return Self { cooldown: cooldown }; }
    pub abstract fn activate(self, player: Player) -> Result<void, AbilityError>;
}

pub class Leap extends Ability {
    override fn activate(self, player: Player) -> Result<void, AbilityError> {
        let velocity = forwardVector(player) * LEAP_FORCE;
        setEntityVar(player, "velocity", velocity);
        return ok();
    }
}
```

- Classes are `final` by default. `open`/`abstract` allow inheritance, and `override` is
  mandatory.
- Single inheritance, multiple interfaces (`impl Damageable for Leap`).
- Classes are reference types, managed by the isolate's GC.

## 15. Forwards, hooks, commands and cvars

### 15.1 Forwards

```rook
forward pluginPrecache() -> Result<void, PrecacheError> { ... }
forward clientPutinserver(player: Player) { ... }
forward clientDisconnected(player: Player, reason: DisconnectReason) { ... }
```

- `forward` implements a **known** forward: from the engine, from the game or exported by another
  plugin. An unknown name or a different signature is an error (`R0501: unknown forward
  "pluginInti"; did you mean "pluginInit"?`). In Pawn, this error was silent.
- API plugins declare and fire their own forwards:

```rook
pub forward roundStart(mode: Mode, duration: Duration);

executeForward(roundStart, mode, duration);
```

### 15.2 Hooks (ReAPI style)

```rook
@hook(Hook.PlayerTakeDamage, HookPhase.Pre)
fn onTakeDamage(inout args: PlayerTakeDamageArgs) -> HookResult {
    if getPlayerVar(args.victim, "team") == Team.Zombie && args.source is .Fall {
        return HookResult.Supercede;            // block the original action
    }
    args.damage = args.damage * ZOMBIE_DAMAGE_SCALE;
    return HookResult.Continue;
}
```

- Each hook has a typed argument struct. `readonly` fields cannot be changed, not even in
  the pre hook.
- Returns: `Continue`, `Supercede` (blocks the original action) and `Break` (blocks it and
  stops the following hooks). The names are the same as in ReAPI.
- Post hooks receive `args` and the result of the action (`postResult`).
- Dynamic registration: `let hook = registerHook(Hook.PlayerSpawn, HookPhase.Post, onSpawn);`
  with `disableHook(hook)` and `enableHook(hook)`.

### 15.3 Commands with typed arguments

```rook
forward pluginInit() -> Result<void, PluginError> {
    registerConcmd("rz_give_health", cmdGiveHealth, access: Permission.Cheats,
                    usage: "<player> <amount>")?;
    return ok();
}

fn cmdGiveHealth(caller: Caller, target: Player, amount: Health) -> PluginResult {
    giveHealth(target, amount);
    return PluginResult.Handled;
}
```

The runtime parses the arguments according to the signature (`Player` accepts `#userid`,
a partial name or a `PlayerId`, and `Health` accepts `50` or `50hp`). An invalid argument
automatically gets the correct usage as the reply.

### 15.4 Typed cvars

```rook
let infectionDelay = createCvar("rz_infection_delay", 10s,
                                  description: "Delay before the first infection",
                                  min: 1s, max: 60s)?;

let delay = getCvar(infectionDelay);          // Duration
hookCvarChange(infectionDelay, onDelayChanged);
```

## 16. Tasks and coroutines

```rook
let task = setTask(2s, onTimer);              // TaskHandle
removeTask(task);

async fn infectionCountdown() {
    for remaining in (1..=COUNTDOWN_STEPS).rev() {
        showHudText(allPlayers(), HudText { message: tr("RZ_INFECTION_IN", seconds: remaining) });
        await wait(1s);
    }
}
```

- Time is **game time**. Tasks and coroutines resume at the start of the `Script`
  phase of the tick in which their deadline expired, ordered by (deadline, creation order). This
  is deterministic and independent of the FPS.
- `await wait(duration)`, `await nextTick()` and `await until(condition)`.
- Tasks can have an **owner** (`setTask(2s, cb, owner: refOf(player))`) and are cancelled
  automatically when the owner ceases to exist.

## 17. Concurrency and determinism

- Each plugin is a **single-threaded** isolate: Rook code never has a data race.
- Parallelism comes from two places:
  1. **ECS systems** ([§18](#18-ecs-components-and-systems)) from different plugins without
     conflicts run in parallel.
  2. **`pure fn` in jobs:** `let path = await runJob(findPath, start, goal);`. The function
     receives data by copy and does not touch the world.
- Determinism: no wall clock (except with a capability), `random_*` comes from a
  per-plugin stream seeded by the server, and `Map`/`Set` iterate in insertion order.

## 18. ECS: components and systems

```rook
@replicated
pub component Frozen {
    until: GameTime,
}

@system(phase: Phase.Simulation)
fn thawFrozen(query: Query<(Entity, Frozen)>, time: Time, commands: Commands) {
    for (entity, frozen) in query {
        if time.now >= frozen.until {
            commands.removeComponent<Frozen>(entity);
        }
    }
}

addComponent(player, Frozen { until: gameTime() + FREEZE_DURATION });
```

- Plugin components take part in queries and replication like the engine's
  ([03 §13](03-ecs.md#13-rook-plugin-integration)).
- `@replicated` sends the component to the clients, enabling effects and prediction in
  `client`/`shared` plugins.

## 19. Modules, imports and visibility

```rook
import cs.weapons.{createWeaponClass, setWeaponClassVar, WeaponType};
import rezombie.api.classes.{createClass, Class};    // another plugin: creates a dependency
import rezombie.prelude.*;                           // glob only for modules marked `prelude`
```

- Each file is a module, and the module path comes from the file path.
- `pub` exports. Without `pub`, the item is private to the module.
- Importing from another plugin creates the load dependency ([13 §3.2](13-mod-system.md#32-automatic-load-order)).
- Glob imports (`*`) are only allowed for `prelude` modules, which gather the most common usage (the
  modern equivalent of `#include <rezombie_main>`).

## 20. Errors, panics and failures

| Situation | Mechanism |
|-----------|-----------|
| Expected failure (missing resource, duplicate name) | `Result<T, E>` |
| Violated invariant (bug) | `panic("reason")`, `assert(cond, "reason")`, `unreachable()` |
| Limit exceeded (CPU, heap, stack) | VM failure (`PluginFault`) |

Panics and failures abort **only the current callback**, with a Rook *stack trace* and the
server's failure policy ([13 §6.4](13-mod-system.md#64-budgets-and-failures)). `todo()` is
forbidden in release builds.

## 21. Tests

```rook
@test
fn assassinHasHighHealth() -> Result<void, TestError> {
    let server = TestServer.start(map: "de_dust2")?;
    let player = server.addBot(team: Team.Terrorist)?;
    changePlayerClass(player, requireClass(ASSASSIN)?)?;
    expectEq(getPlayerVar(player, "health"), 5000hp)?;
    return ok();
}
```

`rook test` starts a simulated *headless* server, with controlled time
(`server.advance(3s)`), test bots and typed assertions.

## 22. Lints and magic numbers

| Lint | Default | Rule |
|------|---------|------|
| `magicNumber` | error | Numeric literal without a unit and without a name (outside `const`, `vars`, a named argument or a var key) |
| `stringLookupInPrecache` | warning | `require*("name")` in `pluginPrecache`. Import the exported ID |
| `expectInGameplay` | warning | `.expect()` outside precache, init and tests |
| `wildcardHidesVariants` | warning | `_` in `match` hides the plugin's own variants |
| `unused` | warning | Unused variable, import, function or result |
| `naming` | error | Naming outside the standard of [§4](#4-lexical-structure) |

## 23. The `rookc` compiler

```mermaid
flowchart LR
    A[.rook source] --> B[Lexer + parser<br/>error-tolerant CST]
    B --> C[Name resolution]
    C --> D[Type checking<br/>local bidirectional inference]
    D --> E[Analyses<br/>exhaustiveness, ephemerals, pure, phases]
    E --> F[RIR · typed SSA IR]
    F --> G[Optimizations<br/>const fold, inline, bounds-check elimination]
    G --> H[.rbc bytecode<br/>+ metadata + debug info]
```

- Written in **C++26**, sharing the bytecode format definitions with the VM.
- **Incremental and query-based:** the same base serves `rook build`, `rook check`
  and `rookLsp`. Editing a function recompiles only what depends on it.
- The lossless CST feeds the formatter and *refactoring*.
- Diagnostics with a code, a snippet, an explanation and a fix suggestion, in English and
  Portuguese (`--lang pt-BR`), with `rookc --explain R0204`:

```text
error[R0204]: expected `Speed`, found integer literal
  --> plugins/class/assassin.rook:19:35
   |
19 |     setPropsVar(props, "speed", 320);
   |                                   ^^^ var "speed" has type `Speed`
   |
   = help: add the unit suffix: `320ups`
```

## 24. VM and bytecode

- **Typed register bytecode.** Instructions already know the operand types, with no
  dynamic type checking.
- **Load-time verifier** (like the JVM/Wasm): the `.rbc` is checked for types,
  register bounds and jump targets before it runs. That is why bytecode coming
  from the network is safe on the client.
- Interpreter with *computed goto* *dispatch*. An optional JIT remains future
  work, without changing the format.
- **Per-isolate GC**, precise and incremental, run in the tick's idle budget. There is
  no global pause across plugins.
- **Instruction fuel** for the CPU budget, and a *stack trace* with a source map.
- Details in [ADR-0007](adr/0007-own-vm-typed-bytecode.md).

## 25. Grammar (summary)

```ebnf
file          = { import } [ plugin_decl ] { item } ;
import        = "import" path [ "." ( "{" ident { "," ident } "}" | "*" ) ] ";" ;
plugin_decl   = "plugin" struct_body ;
item          = [ attributes ] [ "pub" ] ( fn_decl | forward_decl | struct_decl | enum_decl
              | class_decl | interface_decl | newtype_decl | handle_decl | vars_decl
              | component_decl | const_decl | var_decl | type_alias ) ;
fn_decl       = [ "pure" ] [ "async" ] "fn" ident [ generics ] "(" [ params ] ")" [ "->" type ] block ;
forward_decl  = "forward" ident "(" [ params ] ")" [ "->" type ] ( block | ";" ) ;
params        = param { "," param } ;
param         = [ "inout" ] ident ":" type [ "=" expr ] ;
newtype_decl  = "newtype" ident "(" type ")" [ "suffix" ident ] [ "derive" "(" ident { "," ident } ")" ] ";" ;
handle_decl   = "handle" ident ";" ;
vars_decl     = "vars" ident "{" var_entry { "," var_entry } [ "," ] "}" ;
var_entry     = [ "readonly" ] ident ":" type [ "=" expr ] ;
type          = path [ "<" type { "," type } ">" ] [ "?" ] | "(" type { "," type } ")" | "[" type ";" expr "]" ;
statement     = let_stmt | var_stmt | expr ";" | if_stmt | match_expr | while_stmt
              | for_stmt | loop_stmt | "return" [ expr ] ";" | "break" ";" | "continue" ";" ;
attributes    = { "@" ident [ "(" args ")" ] } ;
```

The full grammar, with operator precedence, lives in `rook/spec/grammar.ebnf`, and the
compiler's conformance test suite is the executable reference.
