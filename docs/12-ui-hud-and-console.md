# 12 — UI, HUD, console and input

## 1. Goals

- **HUD and menus visually identical** to CS 1.6 by default: `hud.txt` sprites, fonts,
  colors, positions, dynamic crosshair, kill feed, radar, scoreboard and menus.
- **100% familiar console and configs:** `~`, `bind`, `alias`, `exec`, `config.cfg`,
  `autoexec.cfg`, `userconfig.cfg`, classic client cvars.
- **Extensible by plugins** with a typed API for the HUD, menus and screens, without the
  limitations of the original HUD messages.

## 2. UI components

| Layer | Technology | Use |
|-------|------------|-----|
| Classic HUD | In-house sprite (SPR) and bitmap font renderer | Health, armor, money, ammo, timer, radar, status icons, crosshair, kill feed |
| Menus and screens | In-house retained UI toolkit on top of `nx.render`, with declarative layout and skins | Main menu, options, server browser, team and character selection, VGUI buy menu, scoreboard, MOTD |
| Text menus | HUD renderer | Classic key menus (radio, old buy menu, plugin `showMenu`) |
| Console | In-house toolkit | Game console and listen server console |
| Dev tools | Dear ImGui | Inspectors, profiler, navigation editor (development builds only) |

> The choice of menu toolkit is recorded as pending validation in the
> [TODO](../TODO.md). The alternative evaluated is RmlUi (HTML/CSS-like). The criteria are
> visual fidelity, performance and ease of use for plugins.

## 3. HUD

### 3.1 Classic elements

| Element | Detail |
|---------|--------|
| Health and armor | Icons and numbers with the original colors and damage "flash" |
| Money | Gain and loss animation |
| Ammo | Clip and reserve per ammo type |
| Round timer | With the planted bomb icon |
| Status icons | Buy zone, bomb zone, defuse kit, C4, rescue zone, VIP |
| Radar | Dots for teammates, the bomb and hostages |
| Crosshair | `cl_crosshair_color`, `cl_crosshair_size`, `cl_crosshair_translucent`, `cl_dynamiccrosshair` |
| Kill feed | Weapon and headshot icon |
| Messages | Center text, hints, `titles.txt`, `game_text`, plugin messages |
| Progress bar | Planting, defusing |
| Damage indicator | Direction of the damage taken |
| Screen effects | Flashbang, fade, NVG, sniper zoom (scope overlay) |

### 3.2 Scale and resolution

- The original layout (per resolution, via `hud.txt`) is reproduced. At high resolutions,
  `hud_scale` applies integer scaling, or fractional scaling in `r_fidelity enhanced`,
  without blurring sprites.
- Wide aspect ratios (16:9, 21:9) keep the original relative position of the elements.

### 3.3 Visual parity of numbers

Values within the original range (health up to 100, armor up to 100, money up to 16000)
are drawn **exactly** as in CS 1.6: the same digit sprites, spacing and position.

### 3.4 No value truncation

In the original, several HUD messages used 1-byte fields: health above 255 showed up
wrong, as did large reserve ammo and clips. In Modern-Client.v2:

- Every value displayed in the HUD uses the **same domain type** as the game, from the
  simulation to the screen: `Health` and `Armor` in floating point (as in the original,
  displayed as an integer by the original rule), `Money` and `AmmoCount` as 32-bit
  integers. There is no "HUD" field smaller than the data.
- On the network, these fields are encoded as **varint**, so small values still take few
  bits without imposing a ceiling.
- The HUD's numeric counters **grow in digits** as needed. To keep the visual composition
  with large numbers, the layout pushes the neighboring elements, and
  `hud_number_style compact` (optional) abbreviates (`12.5k`).
- Mandatory regression test: health 5000, 100000 and the type's maximum, armor above
  100, reserve ammo above 255 and money above 16000 (allowed by plugins). All drawn
  correctly, without *wrap*.

### 3.5 HUD for plugins

Without the historical limits on text channels and message size:

- **HUD texts** (`showHudText`), with typed position, color, effect and duration, and as
  many channels as needed.
- **Persistent elements:** bars, icons and counters that the plugin creates and updates
  by handle.
- **Text menus** (`showMenu`), with typed items and a callback that receives the chosen
  item as an enum or value, never as a raw key number.
- **Full screens** (declarative layouts) for client plugins
  ([13 §7](13-mod-system.md#7-plugins-on-the-client)).

## 4. Game menus and screens

| Screen | Requirements |
|--------|--------------|
| First-start wizard | Detects the CS 1.6 installation and asks once whether the player wants to use the original assets (`classic` profile). It also lets the player pick the folder manually or go on without them ([06 §2.5](06-assets-and-resources.md#25-local-configuration)) |
| Main menu | New game (listen server), find servers, options (including **Content**: profile and sources), quit. Background music from the active content (`media/gamestartup.mp3` in the `classic` profile) |
| Server browser | Internet (master servers), favorites, history, LAN. Filters by map, players, ping, password and plugins |
| Options | Keyboard (binds), mouse, audio, video, voice, multiplayer (name, spray, crosshair) |
| Team and character selection | Original keys and layout |
| Buying (VGUI and by keys) | [10 §8](10-counter-strike-gameplay.md#8-buying) |
| Scoreboard | Name, frags, deaths, ping, status (dead, bomb, VIP), *mute* |
| MOTD | **Restricted and safe** text or HTML (a subset rendered by the UI, no scripts). External links open in the system browser after confirmation |

## 5. Console

- Opens with `~` (`toggleconsole`), with history, command and cvar autocompletion (with
  description and range), colors per log level and search.
- The same command language as the original: the `;` separator, quotes, `alias`,
  `+command`/`-command`, `wait`, `exec`, `echo`, `toggle`, `incrementvar`.
- **Explicit errors:** an unknown command or cvar, or an invalid value, yields a message
  with a suggestion ("did you mean...").
- **Content:** `content_list`, `content_scan`, `content_add <path>` and
  `content_profile <profile>` ([06 §2.5](06-assets-and-resources.md#25-local-configuration)).

### 5.1 Preserved client cvars (selection)

| Group | Cvars |
|-------|-------|
| Network | `rate`, `cl_updaterate`, `cl_cmdrate`, `cl_cmdbackup`, `ex_interp`/`cl_interp`, `cl_lw`, `cl_lc` |
| Video | `fps_max`, `gamma`, `brightness`, `gl_vsync` (alias of `vid_vsync`), `fov` (with the original rules), `hud_draw`, `r_drawviewmodel` |
| HUD | `hud_fastswitch`, `hud_centerid`, `hud_saytext_time`, `cl_righthand`, `cl_crosshair_*`, `cl_dynamiccrosshair`, `net_graph` |
| Mouse | `sensitivity`, `m_rawinput`, `m_filter`, `m_pitch`, `m_yaw`, `zoom_sensitivity_ratio`, `m_customaccel` (off) |
| Audio | `volume`, `bgmvolume`, `voice_enable`, `voice_scale` |
| Player | `name`, `_cl_autowepswitch`, `cl_autobuy`, `cl_rebuy`, `cl_minmodels` (respecting the server) |

### 5.2 Client configuration files

| File | Role |
|------|------|
| `config.cfg` | Written automatically (binds and archived cvars) |
| `autoexec.cfg` | Run at startup |
| `userconfig.cfg` | The user's personal configuration |
| `video.toml`, `audio.toml` | Video and audio device and mode (new, structured) |

### 5.3 The `nx.console` module

The console language is the same on the client and the server and lives in `nx.console`
(layer 1, depends only on `nx.core`). The UI console (`nx.ui`) and the server terminal are
views over an `nx::console::Console`, which writes replies and failures to a
`ConsoleOutput` and reads the `exec` files through a `ConfigFiles` (the VFS, from M2 on).

- **A single namespace**, case-insensitive, for cvars, commands and aliases
  (`SV_GRAVITY 600` changes `sv_gravity`). Registering a name already in use fails with
  `NameTaken`.
- **`.cfg` syntax:** a command ends at a line ending or at a `;` outside a quoted word and
  outside a comment. A `"` at the start of a word opens a quoted word, without escape
  sequences, that runs to the next `"` or to the end of the line; inside a word, `"` is an
  ordinary character. `//` at the start of a word comments out the rest of the line, so an
  unquoted `http://...` stays a single word and a `;` after it still ends the command. `\r` and tab are whitespace, and a UTF-8 BOM
  at the start of a file is ignored.
- **Command buffer:** `addText` queues, `insertText` jumps ahead (aliases and `exec` run at
  the point where they are called) and `execute()` runs until the buffer is empty or until
  a `wait`, which leaves the rest for the next frame. More than 65,536 alias and `exec`
  expansions in a single execution are a loop: the buffer is discarded and the
  `ExpansionLimit` failure is reported.
- **Aliases:** the body is the rest of the words, joined by spaces. Arguments after the
  name of an alias are ignored, as in the original. `alias name` alone defines an empty
  alias, and `alias` lists them all.
- **`+`/`-` buttons:** `registerButton` creates `+name` and `-name`. A key (the number the
  bind passes, `+jump 32`) holds the button until it is released. Without a key, the
  console holds it, and `-name` without a key releases everything. The edges (pressed and
  released since the last read) are not lost when `+` and `-` arrive in the same frame, as
  with a bind on the mouse wheel.
- **Typed cvars:** `bool` (`0` or `1`), 32-bit integer (accepts `10.0` and `1e3`, rejects
  `10.5`), finite `float`, text (no double quotes or line endings, so that `config.cfg`
  always reads the value back) and `enum` with `Count` (accepts the number or the
  enumerator name and writes the number). Numbers may have a range. Rejected text never
  becomes 0: `mp_freezetime: expected a whole number in [0, 60], got "abc"`.

| Flag | Effect |
|------|--------|
| `Archive` | Goes into `config.cfg`: `Console::archivedCvars()` produces the `name "value"` lines |
| `Notify` | A change is announced to the players and the cvar is listed in `A2S_RULES`. The console only marks it; whoever announces compares `revision()` |
| `Cheat` | The console only changes it with cheats enabled (`Console::setCheats`, which follows the server's `sv_cheats`). Otherwise, `CheatProtected` |
| `ServerProtected` | The value comes from the server: the console never changes it (`ServerProtected`), only the code does. On the client, this is the case of the movement cvars replicated for prediction |

The code reads and writes through the typed handle (`Cvar<float>`), which validates the
type and range, but not the flags. The `Console` belongs to one thread (the main one,
between frames), and the simulation copies the values it needs at the start of the tick
(`MovementSettings`).

Built-in commands: `alias`, `wait`, `exec`, `echo`, `toggle` (switches between 0 and 1 or
cycles through the given values), `incrementvar` (adds the step and wraps around the
range), `cvarlist [prefix]` (cvars with their values and flags), `cmdlist [prefix]`
(commands with their descriptions) and `find <text>` (cvars and commands whose name or
description contains the text). Prefixes and searches ignore case, and every listing ends
with its count (`2 cvars starting with "sv_"`).

Log levels are cvars too: `LogLevelCvars` registers one `log_level_<category>` per log
category the application passes in (`log_level_net debug`, or the level's number), and its
`apply()`, called once a frame after `Console::execute`, copies the changed levels to the
`Logger` ([02 §10](02-coding-standards.md#10-logging)).

## 6. Input

- **SDL3** for keyboard, mouse and gamepad, behind `nx::platform::Window`: keys arrive by
  physical position (`Key`, the USB HID usage code), the mouse as relative motion in its own
  counts (`MouseMoved`), and the window gives a GPU backend its native handles.
- **Raw mouse** by default (`m_rawinput 1`), without OS acceleration. Sensitivity is
  calibrated so that `sensitivity` has **the same effect in degrees per count** as in the
  original (validated by a parity test).
- Input is sampled as late as possible in the frame ([05 §12](05-rendering.md#12-latency-and-frame-pacing)).
- **Binds:** the same key names as the original (`MOUSE1`, `MWHEELUP`, `KP_END`, `SPACE`…),
  including *bunny hop* with `MWHEELDOWN` on `+jump` (with the original's instant command
  behavior).
- Non-QWERTY keyboard layouts: optional binds by **physical position**
  (`in_keymap scancode`).

## 7. Localization

- UI strings in per-language files (`locale/pt-BR.toml`, `locale/en.toml`), with typed keys
  generated for C++ and Rook. A nonexistent key is a build error, not empty text.
- `titles.txt` and the original texts are still read and can be overridden by a
  translation.
- Plugins have their own locale files ([14](14-rook-language.md#12-strings-formatting-and-translation)).

## 8. Accessibility

- Independent UI scale and console font scale.
- Crosshair and highlight palettes for color blindness (optional, off by default so as not
  to change parity).
- Optional subtitles for radio speech.

## 9. Typed errors

| Enum | Examples |
|------|----------|
| `ConsoleError` | `UnknownCommand` (with a suggestion), `UnknownCvar`, `InvalidValue`, `CheatProtected`, `ServerProtected`, `InvalidArguments`, `InvalidName`, `NameTaken`, `ExecFailed`, `ExpansionLimit`, `CommandFailed` |
| `CvarError` | `InvalidName`, `NameTaken`, `InvalidDefinition`, `InvalidValue`, `OutOfRange` |
| `ConfigFileError` | `NotFound`, `InvalidPath`, `Unreadable` |
| `UiError` | `LayoutInvalid`, `MissingSprite`, `UnknownLocaleKey` |
| `InputError` | `UnknownKeyName`, `DeviceUnavailable` |
