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

**Observed** (Counter-Strike 1.6, build 8684 through NextClient, de_dust2 at 640x480, 1280x720 and
1920x1080, `gamma` 3: snapshots with `hud_draw` 1 and 0, what the HUD added fitted to the sprites'
texels): the HUD does not scale. The 640 entries of `hud.txt` are drawn at every resolution from 640
wide up, at their size in pixels, and the elements keep to the screen's edges and middle. The
pictures are added (`SPR_DrawAdditive`) in (255, 160, 0) at the alpha the HUD rests at, 100, their
palettes through the texture gamma (the texels match to within one level). A rectangle of w by h
texels of a sprite is drawn from its x, y over w + 1 by h + 1 pixels, its texels filtered linearly,
but for its first row of pixels, which the original leaves out: that row is not reproduced yet, its
cause unknown. At W by H pixels, the pictures' top left corners, a number's digits each the width of
`number_0` (20) after the one before:

| Element | Where |
|---------|-------|
| Health | `cross` at (10, H − 40); the digits at the right of room for three from x 36, y H − 40 |
| Armor | `suit_empty` at (W / 5, H − 37), `suit_full` over it; the digits right after the suit, at the right of room for three, y H − 37 |
| Money | The digits at the right of room for five that ends 16 from the right edge, y H − 75; `dollar` right before that room (W − 134) |
| Ammo (a Glock, 20 and 40) | The weapon list's `ammo` icon at (W − 42, H − 40); the reserve's digits ending at the icon, y H − 37; a 2 by 25 rectangle of the HUD's color added (`FillRGBA`) at (W − 114, H − 37); the clip's digits ending 10 before it |
| Round timer | `stopwatch` at (W / 2 − 64, H − 37); the minutes' digit at W / 2 − 20, the seconds' at W / 2 + 6 and W / 2 + 26, y H − 37; the colon two 2 by 2 rectangles, dimmer, at (W / 2 + 2, H − 28) and (W / 2 + 2, H − 22) |
| Status icons | Added green; at 1280x720 `buyzone` at (12, 286) and `c4` at (12, 323) |
| Radar | `radar` in the top left corner |
| Crosshair | Four lines a pixel thick added about the middle (`FillRGBA`, `cl_crosshair_color` 50 250 50 at full alpha). At 1280x720 10 long, 14 from the middle with the knife, 16 with the Glock and 24 running with it; at 1920x1080 15 long and 24 away standing, 36 running; at 640x480 3 long and 5 away |

The drawing itself is `nx.render:screen` ([05 §7.1](05-rendering.md#71-frame-passes-legacy-mode)).

**Today** (`cs.client:hud`): `next`, and `nxview --hud`, draw the health, armor and money of the
player as it spawns (100, 0 and 800: `cs.shared:player`) where the table puts them, each picture
drawn by `nx::render::spriteQuad`, while `hud_draw` is 1. A number with more digits than its room
widens the room, health and armor to the right and money to the left, and a longer health pushes the
suit right, keeping the 32 pixels it has at 640 wide (§3.4). Armor between none and 100 shows as
much of `suit_full` from the bottom up, which the references (0 and 100) do not check yet. The
first row of each picture, the 320 entries for screens under 640 wide, the helmet, the flash of a
number that changes, and the ammo, timer, status icons, radar and crosshair come next.

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

**Today** (`cs.client:hud`): `healthDigits` makes a health a whole number as the game sends it to
its client (its whole part, none below 0, and 1 above 0 up to 1), without the 255 ceiling, every
digit of it up to the type's maximum (39 digits); armor and money likewise. The tests draw health
5000, 100000 and the type's maximum, armor 300 and 70000, and money above 16000 up to the type's
maximum; reserve ammo comes with the ammo.

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

**Today** (`nx.settings`): the files live in the user's folder of Modern-Client.v2
(`nx::platform::userDirectory`: `%APPDATA%\Modern-Client.v2` on Windows,
`$XDG_CONFIG_HOME/modern-client.v2` or `~/.config/modern-client.v2` elsewhere), next to
`content.toml`, never in the game's installation. At startup `runStartupFiles` runs `config.cfg`,
then `userconfig.cfg`, then `autoexec.cfg`, those that exist; a missing one is no failure, an
unreadable one is printed. `exec` reads `.cfg` files of that folder by relative paths
(`UserConfigFiles`; an absolute path, `..` or another extension is `InvalidPath`). At exit
`saveConfig` writes `config.cfg`: a line that says what it is, then the archived cvars; the
file is written beside its place and then moved there, so a crash leaves the old settings or
the new ones, never a part. The original ends its `config.cfg` with `exec userconfig.cfg`;
here the startup runs that file itself. `registerClientCvars` registers the client's cvars of
§5.1 that have a consumer today, with Counter-Strike 1.6's names and starting values:
`sensitivity` (3), `m_yaw` and `m_pitch` (0.022), `fps_max` (100, 0 for no limit), `gl_vsync`
(1), `gamma` (2.5, from 1.8 to 3), `brightness` (1, from 0 to 3), `hud_draw` (1), all archived,
and `texgamma` (2) and `lightgamma` (2.5), from 1.8 to 3, which the original does not archive.
`next` reads them as it starts and writes `config.cfg` as it ends; a change takes effect when it
starts again until the console comes, but `hud_draw`'s, which each frame reads. `config.cfg` keeps the binds too (§6), before the cvars, in the
original's order. `vid_vsync` as the name of `gl_vsync`, and the other cvars, come with their
consumers.

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

**Today** (`nx.settings`): a key is the original's key number, which `console::KeyId` holds: a
key that types an ASCII character has that character's number (`a` 97, `SPACE` 32), the others
the original's own (`UPARROW` 128, `MWHEELUP` 240, `MOUSE1` 241). `keyOf` gives the key at a
position of a US keyboard: both Ctrl keys are `CTRL`, both Shift `SHIFT`, both Alt `ALT`, and
the keypad's keys are its `KP_` keys whatever Num Lock says; the keys the original has no number
for (Print Screen, Scroll Lock, Num Lock, the system keys) run nothing. The platform reports
positions only, so binds follow them until `in_keymap` brings the layout's characters. `bind
<key> [command]`, `unbind`, `unbindall` and `bindlist` set and list them: a key is one ASCII
character (a letter in either case) or one of the original's names in any case (`keyNamed`), and
a command holds no `"`, so that `config.cfg` reads it back. `defaultBindings` are Counter-Strike
1.6's, those its options restore, and `config.cfg` keeps the binds: `unbindall`, then a `bind
"<key>" "<command>"` line per bound key. `BoundInput` runs them as the original does: a key
going down queues its command, followed by the key's number when it starts with `+` (`+jump
32`); coming up, the same with its `+` turned to `-` (`-jump 32`), and nothing for other
commands. The number follows the whole command, as in the original: a key bound to `+jump;
wait; -jump` queues `+jump; wait; -jump 32`. A key the system repeats queues nothing more, a key
that comes up without having gone down here queues nothing, and every key down comes up when the
window loses the keyboard. Each notch of the wheel presses its key and releases it at once.

`cs::PlayerInput` (`cs.client`) registers the player's buttons, `+forward`, `+back`,
`+moveleft`, `+moveright`, `+moveup`, `+movedown`, `+left`, `+right`, `+lookup`, `+lookdown`,
`+strafe`, `+speed`, `+jump`, `+duck` and `+use`, and their cvars with Counter-Strike 1.6's
values: `cl_forwardspeed`, `cl_backspeed` and `cl_sidespeed` (400, archived), `cl_upspeed`
(320), `cl_movespeedkey` (0.52), `cl_yawspeed` (210), `cl_pitchspeed` (225) and
`cl_anglespeedkey` (0.67). `nextCommand` makes a user command as the original's `CL_CreateMove`
makes it: each movement button asks its speed for the share of the command it was held (all of
it, but half when it went down during the command, as `CL_KeyState` counts), `+speed` asks
`cl_movespeedkey` of each, and together they are cut to the player's speed limit. `+jump`,
`+duck` and `+use` count also when pressed and released within one command, so that `+jump` on
`MWHEELDOWN` jumps once a notch. `turn` turns the view each frame as `CL_AdjustAngles` does:
`+left` and `+right` (which move instead while `+strafe` is held), `+lookup` and `+lookdown`, up
to 89 degrees. `+attack`, `+reload` and the other buttons come with what reads them; the mouse
always looks, so `+mlook` and `+klook` are not there. `next` plays with them: Counter-Strike's
binds, and `noclip` on `v`.

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
