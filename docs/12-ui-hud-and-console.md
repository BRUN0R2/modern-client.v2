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
| Menus and screens | In-house retained UI toolkit on top of `nx.render`, with declarative layout and skins | Main menu, options, server browser, team and character selection, buy menu, scoreboard, MOTD |
| Text menus | HUD renderer | Classic key menus (radio, plugin `showMenu`) |
| Console | In-house toolkit | Game console and listen server console |
| Dev tools | Dear ImGui | Inspectors, profiler, navigation editor (development builds only) |

> **Decided** in [ADR-0020](adr/0020-in-house-ui-toolkit.md): `nx.ui`, in-house and retained, in
> VGUI2's model of widgets, layouts and schemes, so that the original's own schemes, `.res`
> layouts and strings are read as they are; RmlUi was weighed and not chosen.

**Today** (`nx.ui:panel`): retained panels, each in its parent, its children over it in their order,
placed by a `PixelRect` from its parent's top left (`nx.ui:geometry`: points, sizes and rectangles in
`nx::Pixels`, [02 §4.1](02-coding-standards.md#41-strong-types-and-units)). `applySettings` reads a panel's block of a layout (`.res`) by VGUI2's names in any case: `xpos` and
`ypos`, from the parent's right or bottom with `r` and from its middle with `c` (`"r80"`,
`"c-175"`, as the installation's layouts write them), `wide`, `tall`, `visible`, `enabled`,
`tabPosition`, `pinCorner` and `autoResize`; `applyLayout` applies a layout's blocks to the children
they name. As a panel changes size, each edge of its children keeps its distance to the edge it
follows: the left and top edges those of the pinned corner, the right and bottom ones the parent's
right and bottom when the child resizes that way, else its own left and top, keeping its size. A
setting that cannot be read is refused, and changes nothing.

Panels are drawn (`nx.ui:painter`) as rectangles of the screen: a `PanelPainter` places what a
panel draws from its top left and keeps it within the panel and every panel it is in, with the
screen's clips ([05 §7.1](05-rendering.md#71-frame-passes-legacy-mode)), as VGUI2 keeps a panel's
drawing within it. `paintTree` draws a panel's background, then over it each of its visible
children in their order; a hidden panel draws nothing, nor do its children. A control is a kind of
panel that draws more: the first is the `Label` (`nx.ui:label`), a line of text in a face and a
colour, placed against an edge of the label or halfway between two by its alignment, named as the
layouts' `textAlignment` names them (`north-west` to `south-east`). The `TextEntry`
(`nx.ui:textEntry`) is a line the player types in, the console's input line first: typed text
([§6](#6-input)) goes in at its cursor, control characters left out; Left and Right move the
cursor by a character, Home and End to the ends, Backspace and Delete remove the character before
or after it, and Enter submits the line to its owner; it draws its text and a cursor a pixel wide,
scrolled left when the cursor would pass its right edge. Selection, the clipboard and the cursor's
blinking come later. `RichText` (`nx.ui:richText`) shows lines of text, each in its colour, the
console's output: the newest at the bottom, each wrapped at its last space that fits its width
(anywhere in a word wider than it), the oldest dropped past its capacity (1024 lines unless told
otherwise), scrolled up from the newest by whole rows; scrolled up, it stays on the rows it shows
as lines come. Which of a scheme's colours,
borders and fonts each control takes, and where VGUI2 puts a label's text to the pixel, wait for
reference shots of the original's windows.

### 2.1 Fonts

VGUI2's text is TrueType: the schemes name a face (Tahoma, Verdana, Arial, Trebuchet MS, Marlett),
a cell height (`tall`), a weight and flags such as `antialias`, and Windows' GDI sized and drew it.
`nx.ui:font` reads the faces with FreeType (`Font::open`) and reproduces both.

**Size.** A cell height is the height of a line. GDI turns it into a whole number of pixels per em
by the face's tables, which `fontMetricsFor` reproduces (the rule below was found by asking GDI
for every height from 4 to 120 in Tahoma, Verdana, Arial, Trebuchet MS, Courier New, Marlett,
Times New Roman, Segoe UI, Georgia and Lucida Console, regular and bold, and matches it in every
case):

- The device heights of the face's `VDMX` table (the highest and lowest pixel of its glyphs at each
  size, recorded by the font maker), when Windows takes them: a version 1 table, or a version 0
  table made for the ANSI characters (the OpenType specification says Windows ignores the others;
  Tahoma's is one of those). When the table reaches past the cell, the em of the first device
  height whose line is exactly the cell, else the largest em whose line fits. Its line is the
  device height's highest and lowest pixel. A device height whose line is less than half its em is
  left out (`readDeviceHeights`): a face's line is at least half its em (`Font::open` refuses a face
  whose isn't), so it is no height of the face, but a broken table, or one made to size the face to
  tens of thousands of pixels per em in a small line and so exhaust whoever draws its glyphs (found
  by fuzzing: a record of 64768 pixels per em in a line of 21 sized a face so, and drawing its
  glyphs took 93 seconds in a debug build). The em is then never more than twice the cell.
- Otherwise every em counts, each by its device height where it has one, else by its line,
  `usWinAscent + usWinDescent` scaled and rounded: again the first em whose line is exactly the
  cell, else the largest whose line fits. A scaled line is split as the cell is: the ascent and
  descent at the scale of the cell, a whole number of 1/65536 steps, each rounded. So Tahoma at 16
  is 13 pixels per em with a line of 13 + 3, at 20 it is 16 with 17 + 3, and at 38 its line is 37,
  one pixel short of the cell, as GDI makes it.

Past about 250 pixels per em, beyond the schemes' sizes, GDI's results stray from the rule by a
pixel or two.

**Glyphs.** FreeType hints them by the faces' own instructions with its version 35 interpreter,
the one that hints both axes as Windows does (never with its automatic hinter), and draws them
without antialiasing (each pixel in or out by its centre) or with it (`FontSmoothing`). Asked to
smooth them, Windows does as the face's `gasp` table says at its size, and so does `Font`:
smoothed or not, fitted to the pixels or not. Tahoma and Verdana are not smoothed from 9 to 16
pixels per em, so a scheme's `antialias` changes nothing at the console's size. Without smoothing,
Windows fits the glyphs at every size, whatever the table says. A glyph wider or taller than eight
ems (`kMaxGlyphEms`) is refused: no letter is, and a font made to exhaust whoever draws it could
ask for bitmaps of thousands of pixels a side. Measured
against GDI's glyphs (`GetGlyphOutline`) in Tahoma, Tahoma Bold, Verdana, Verdana Bold, Arial and
Trebuchet MS at cells of 12 to 28 pixels, the 94 printable ASCII characters each:

| | Result |
|-|--------|
| Line metrics | Identical everywhere |
| Advances | Identical, but for one glyph (Arial at 13) |
| With antialiasing | Each pixel within 2/16 of the coverage GDI gives (4/16 in two cases), compared at every size: `GetGlyphOutline` ignores the `gasp` table |
| Without antialiasing | 0 to 11 glyphs out of 94 differ, by a pixel or two (Tahoma at 16, the console's size: `/` and `z`); 29 in Arial at 28 and 35 in Trebuchet MS at 12 (7 pixels per em) |

The glyphs without antialiasing that differ are thin diagonals, where GDI turns on a pixel more
than FreeType; the cause is still open (the two rasterizers' dropout control is the suspect).

**Text on the screen** (`nx.ui:text`). A `GlyphAtlas` packs a font's glyphs for a set of
characters into one picture of white texels, each as opaque as its glyph covers the pixel, in rows
a texel apart; `ScreenRenderer::loadImage` puts it on the GPU as it is. `layoutText` places the
glyphs of a UTF-8 line: the baseline the font's ascent below the line's top, each glyph's bitmap
where the pen puts it, the pen moved by its advance alone (Windows' `TextOut` does not kern);
`appendText` makes them rectangles of the screen in the text's colour, blended by their coverage.
Lines of 80 characters composed so and drawn by GDI's `TextOutW` in Tahoma, Verdana and Trebuchet
MS at cells of 13 to 28 pixels end on the same pixel; without smoothing, and at the sizes `gasp`
does not smooth, 0 to 7 of their 900 to 3400 inked pixels differ. Where GDI smooths, drawing white
on black, its 16 levels lie on a curve (the first is 104 of 255) where FreeType's coverage is
linear, and about a quarter of the edge pixels differ by more than 40 of 255; which of the two the
original shows needs reference shots of its smoothed text (the console's size is not smoothed).
Below 9 pixels per em the hinting differs more (20 to 100 pixels of a line); no scheme asks for
such sizes.

**Which face** (`nx.ui:fontCatalog`). A `FontCatalog` holds the faces of the system's fonts
(`nx::platform::fontFolders`: Windows' Fonts folder and the user's own; elsewhere the XDG fonts
folders and `~/.fonts`) and of the files a scheme brings (`CustomFontFiles`), each described by
reading only its tables: its family (name ID 1, in each language the file gives it), style, weight
and italics; a name longer than the 31 characters GDI's LOGFONT holds is no family a scheme can ask
for, and a collection gives at most 64 faces, so that no file makes the scan run long. A font file
of a folder that cannot be added is left out, and why is told to the caller (`AddedFonts::leftOut`),
which logs it. GDI was asked for each weight from 0 to 1000 in Tahoma, Verdana, Arial, Trebuchet MS,
Segoe UI and Marlett, and `choose` reproduces what it gave:

- the family by its name ID 1 only, in any case: "Verdana Bold", which `cstrike`'s
  `ClientScheme.res` names, is no family, and GDI gives another font for it;
- the faces as italic as asked if there are any; of them the heaviest at most 88 heavier than asked
  (0 asks for 400), else the lightest: Tahoma Bold from 612;
- bold simulated on a face lighter than 600 more than 150 lighter than asked: Tahoma from 551,
  Marlett (500) from 651, never on a bold face. Simulated, each glyph is struck twice, the second
  a pixel to the right, and advances a pixel more, as GDI draws it (the same in all 94 glyphs
  compared); the line keeps its metrics.

Scanning this computer's 172 faces takes about 0.2 s.

**A scheme's font** (`nx.ui:schemeFont`). A font of a scheme's `Fonts` lists candidates; the scheme
files say how they combine: in their order, a later one only for a range not already filled, and
one that fails to load replaced by those after it. A `SchemeFontSet` is a font made for a screen:
the candidates whose `yres` holds the screen's height (and those without one), each opened with the
face the catalog chooses for its name, weight and italics, at its `tall`, smoothed when it says
`antialias`; a character is drawn by the first whose `range` holds it, and the line is the first's.
Every font of the installation's `TrackerScheme.res` and `ClientScheme.res` opens so on this
computer, but `GameConsole_Mono`, whose Courier is a bitmap font.

**Drop shadow.** A candidate that says `dropshadow` casts VGUI2's shadow under its characters
(`SchemeFontSet::hasDropShadow`, `AtlasGlyph::hasDropShadow`): `appendText` draws each such glyph's own
shape in black, as opaque as the text, a pixel right and down (`kDropShadowOffset`), every shadow of
the line before its glyphs, so that none falls over a glyph. Measured on Counter-Strike 1.6's
`cl_showfps` (§3.2) at five resolutions: GDI's text with its copy so drawn under it matches the
original's to the pixel, text and shadow alike, the shadow opaque black and the advances GDI's (the
shadow widens no glyph).

Still to come: Windows' bitmap fonts
(`.fon`), such as the Courier that `GameConsole_Mono` asks for; italics simulated on a family
without an italic face (Tahoma); and atlases that grow as new characters come.

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

**Observed** (Counter-Strike 1.6, build 8684 through NextClient, de_dust2 at 640x480, 800x600,
1024x768, 1280x720, 1280x960, 1600x900 and 1920x1080, `gamma` 3: snapshots with `hud_draw` 1 and 0,
what the HUD added fitted to the sprites' texels): the HUD does not scale. The 640 entries of
`hud.txt` are drawn at every resolution from 640 wide up, at their size in pixels, and the elements
keep to the screen's edges and middle. The pictures are added (`SPR_DrawAdditive`) in (255, 160, 0)
at the alpha the HUD rests at, 100, their palettes through the texture gamma (the texels match to
within one level). A rectangle of w by h texels of a sprite is drawn from its x, y over w + 1 by
h + 1 pixels, its texels filtered linearly, but for its first row of pixels, which the original
leaves out: that row is not reproduced yet, its cause unknown. At W by H pixels, the pictures' top
left corners, a number's digits each the width of `number_0` (20) after the one before; at every
resolution measured but 1920x1080, the first lit pixel of the cross, the health, the suit, the
armor, the dollar and the money is where these rules put it, to the pixel:

| Element | Where |
|---------|-------|
| Health | `cross` at (10, H − 40); the digits at the right of room for three from x 36, y H − 40 |
| Armor | `suit_empty` at (W / 5, H − 37), `suit_full` over it; the digits right after the suit, at the right of room for three, y H − 37 |
| Money | The digits at the right of room for five that ends 16 from the right edge, y H − 75; `dollar` right before that room (W − 134) |
| Ammo (a Glock, 20 and 40) | The weapon list's `ammo` icon at (W − 42, H − 40); the reserve's digits ending at the icon, y H − 37; a 2 by 25 rectangle of the HUD's color added (`FillRGBA`) at (W − 114, H − 37); the clip's digits ending 10 before it |
| Round timer | `stopwatch` at (W / 2 − 64, H − 37); the minutes' digit at W / 2 − 20, the seconds' at W / 2 + 6 and W / 2 + 26, y H − 37; the colon two 2 by 2 rectangles of the HUD's color added at an alpha of 81 (they add (81, 50, 0) where the digits add (100, 62, 0) at most; 640x480, 1024x768 and 1280x720), at (W / 2 + 2, H − 28) and (W / 2 + 2, H − 22) |
| Status icons | Added green; at 1280x720 `buyzone` at (12, 286) and `c4` at (12, 323) |
| Radar | `radar` in the top left corner |
| Crosshair | Four rectangles of `cl_crosshair_color` (50 250 50), a pixel thick, about (W / 2, H / 2): added at full alpha (`FillRGBA`) with `cl_crosshair_translucent` 1, drawn as they are with 0. With s = W / 640 and f the size's factor (`cl_crosshair_size` `small` 0.625, `medium` 0.8, `large` 1; `auto` small at 640 wide, medium at 800, large from 1024), each is ⌊5·s·f⌋ long and the gap ⌊d·s·f⌋, d the weapon's distance (standing: the knife 7, the Glock and the Desert Eagle 8): the left one ends at W / 2 − gap and the right one starts at W / 2 + gap, the top and bottom ones likewise. Running opens the gap (at 1280x720 the Glock's to 24). NextClient draws it itself (`cl_crosshair_type` 0, the cross), to be checked against Valve's client |

The drawing itself is `nx.render:screen` ([05 §7.1](05-rendering.md#71-frame-passes-legacy-mode)).

**Today** (`cs.client:hud`): `next`, and `nxview --hud`, draw the health, armor and money of the
player as it spawns (100, 0 and 800: `cs.shared:player`) where the table puts them, each picture
drawn by `nx::render::spriteQuad`, while `hud_draw` is 1. A number with more digits than its room
widens the room, health and armor to the right and money to the left, and a longer health pushes the
suit right, keeping the 32 pixels it has at 640 wide (§3.4). Armor between none and 100 shows as
much of `suit_full` from the bottom up, which the references (0 and 100) do not check yet; a helmet worn
shows `suithelmet_empty` and `suithelmet_full` in their place, the entries hud.txt has for them, of the
suit's size, in the suit's place: no reference shows a helmet yet. The
crosshair of the knife in hand is drawn by the table's rule (`crosshairRects`, in whole numbers),
in the style `cl_crosshair_color`, `cl_crosshair_size` and `cl_crosshair_translucent` set
(`HudCvars`; `nxview` draws Counter-Strike's default). The ammo of the weapon in hand, one that
fires ammo (`HudAmmo`, `hudAmmoOf`), is drawn where the table puts it (`layoutAmmo`): its clip, the
bar and the rounds of its kind the player carries, then the picture of its kind of ammo
(`AmmoType::picture`, the `ammo` entry of the original's weapon lists read once into
`data/cstrike/ammo.toml`, [10 §7.2](10-counter-strike-gameplay.md#72-weapon-data-datacstrikeweaponsweapontoml-datacstrikeammotoml)),
so that a weapon made as the game runs shows its ammo as the game's own do; a number of more digits
widens its room to the left. The round's time is drawn where the table puts it (`layoutTimer`):
the whole seconds the game told the HUD as the player spawned and as the freeze ended, counted down
as the original's client counts them (`cs::RoundTimer`, [10 §3](10-counter-strike-gameplay.md#3-match-and-round-flow)),
its minutes at the right of room for two that ends in the middle, the stopwatch right before it (more
minutes widen the room to the left, pushing the stopwatch), the colon, and the seconds in two digits.
The first row of each picture, the 320 entries for screens under 640 wide, the helmet, the
flash of a number that changes, each weapon's crosshair (only the knife's is measured: it is drawn
for every weapon) and its opening as the player moves (`cl_dynamiccrosshair`), the weapon menu,
the timer's color as the round runs out and the planted bomb's (to be measured on the original),
the status icons and the radar come next.

`cl_showfps 1` (observed in the same build, the frames a second at 514 to 520 and `net_graph 3` on, at
640x480, 1024x768, 1280x720, 1280x960 and 1600x900) writes `N fps`, N a whole number without
padding, in the `EngineFont` of the windows' scheme (`resource/TrackerScheme.res`: Verdana, weight 600,
`dropshadow`, its `tall` by the screen's height: 12 up to 599 lines, 13 to 767, 14 to 1023 in that
build; the Steam build of today has taller ones from 13 to 70), in opaque white, its line's top left at
(2, 2) at every resolution. Its number was always a little below the one `net_graph` showed in the same
frame (514 against 517.8, 518 against 520.0): the original measures the rate twice, two ways.

**Today** (`cs.client:fpsLabel`): `next` draws that label while a map is played and `cl_showfps` is 1,
over the HUD whether `hud_draw` draws it or not, as the original's engine writes it over its client's
HUD, and `nxview --fps <frames a second>` over its frame; its font, place, colour and shadow match the
five references to the pixel. Its number is the one measure of the client's frame rate,
`nx::platform::FrameRateMeter` ([05 §12](05-rendering.md#12-latency-and-frame-pacing)), the one the
original's `net_graph` writes, which changes with every frame, and it is written as `net_graph` writes
it, to the nearest tenth (`520.8 fps`): a departure the user chose (2026-10-28) from the original's
whole number, which is counted over each second and lags the rate. `net_graph` is to show the same
number, so that the two never disagree as the original's do. Its font, like the
console's, comes from `cs::WindowFonts`, which reads the windows' scheme once.

**The kill feed** (`cs.client:killFeed`): while `hud_draw` draws the HUD, a row of each player's death
the server tells of (`cs::KillNotice`, [10 §7.4](10-counter-strike-gameplay.md#74-damage)), the oldest
first, 4 at most, a new one making the oldest go, each for `hud_deathnotice_time` seconds (6, not
archived). A row is the killer's name, the picture of what killed it, `d_<weapon>` of `sprites/hud.txt`
(`d_skull` for no weapon or one the list has no picture of, a weapon made as the game runs), `d_headshot`
for a kill in the head, and the victim's name; a death of no killer shows the picture and the victim's
name alone. **Observed** (build 8684 through NextClient, `hud_deathnotice_old 1`, its classic look; a
player killing itself with `kill` at 800x600, 1024x768, 1280x720, 1280x960 and 1600x900; to the pixel in
every one), with H the screen's height, each share in whole pixels:

| Part | Where |
|------|-------|
| The rows' tops | The first at H / 15, each next one H / 30 under it, whatever its font; with the player dead, 2 × H / 15, under the spectator's panel |
| The victim's name | Its pen ending H / 40 from the right edge, written in `EngineFont` (the same as `cl_showfps`) with its drop shadow |
| The pictures | Right before H / 96 of gap before that name, added as the HUD's sprites are (`nx::render::spriteQuad`), from the row's top; a skull's 32 wide |
| Colors | A terrorist's name (255, 63, 63), a counter-terrorist's (153, 204, 255); the pictures added in (255, 80, 0) |

The killer's name, H / 96 before the pictures, the head's picture right after what killed, a teammate's
kill's pictures in (10, 240, 10) and the console's lines (`<killer> killed <victim> with <weapon>`,
`<killer> killed his teammate <victim>`, `<victim> died`) are not measured yet, as the Half-Life SDK's
client has them: they need a second player, and that build's game has no bots ([TODO](TODO.md)). Valve's
own client of today (build 10210) draws the same rows at those resolutions, up to a pixel at 1024x768, where
its `EngineFont` writes its capitals one lower, and prints `<victim> died` in the console.

**The scoreboard** (`cs.client:scoreboard`, a layout of the project's own, [ADR-0020](adr/0020-in-house-ui-toolkit.md)):
while `+showscores` is held (`TAB`), over the HUD whether `hud_draw` draws it or not, a panel with the
teams of players, each under its row, of the most wins first (the terrorists of as many) and each of its
players from the most frags, of as many the fewest deaths, of as many by its slot; what it shows is
[10 §7.4](10-counter-strike-gameplay.md#74-damage)'s scores. **Observed** (Valve's client of today, build
10210; a player alone on its team, alive and after `kill`, at 800x600, 1024x768, 1280x720, 1280x960 and
1600x900), with H the screen's height and Y(n) = n × H / 480 in whole pixels, each place from the panel's
top left:

| Part | Where |
|------|-------|
| The panel | Y(520) wide, Y(340) tall, in the middle across, Y(70) from the top: black at half opacity over the scene; its border and the line under the heads (188, 112, 0) at half opacity over that; its corners cut by 5, 4, 2, 2 and 1 pixels |
| The heads | The server's name (`hostname`) from Y(5) and a pixel; `HP`, `Money`, `Score`, `Deaths` and `Latency` ending at Y(310), Y(367), Y(412), Y(458) and Y(508); all in (255, 176, 0) |
| What changes with the fonts | Under 768 lines the line under the heads at 23, the team's line at 51 and the names from 45; from 768 at 24, 53 and 61 |
| A team's row | `Terrorists   -   1 player` over its line, its wins ending in the score's column, in its color: (255, 64, 64) for the terrorists, (153, 204, 255) for the counter-terrorists; the line opaque, from 6 to 8 before the panel's right |
| A player's row | Its name, `Dead` from Y(215), its health and `$` and its money while the server tells them, its score and its deaths ending in their columns; its line of text Y(12) under the team's line less half the line's height and a pixel |
| The player's own row | White at 25 of 255 over the panel, Y(21) tall, from 2 under the team's line, 5 in from each side of the panel |

`hostname` is `Counter-Strike 1.6` until it is set, the game the project plays, a departure the user chose
(2026-11-02) from the original's `Half-Life`. The rows of more players are not measured, for that build's game
has no bots: the next row is drawn Y(21)
under the last, as tall as the band, and a next team's line two of them under its last row ([TODO](TODO.md)),
as the order of the teams and of their players, which come from the original's rules. The original writes
all of it in bitmap fonts of its own (`gfx/vgui/fonts/<size>_Scoreboard Text.tga` and its title's), which
`next` writes in `EngineFont` until it reads them. The room of the avatar of Steam's (at 16 under 768 lines,
21 from them) stays empty, and `Latency` has no numbers yet.

**The damage numbers** (`cs.client:damageNumbers`), the project's own, nothing of the original's, as
action games show the damage a hit deals: while `hud_damagenumbers` is 1 and `hud_draw` draws the HUD,
each hit of the player's on another player, a hostage or an entity's body shows a number of the damage
it dealt, rounded, 1 at least, each pellet of a shot that hits a number of its own: what the server tells
the player's client alone of its hits of each tick (`cs::DamageDealt`: over the head of the body the hit
met, the damage, whether in the head; what a player's health lost, what an entity's body was dealt). The
number shows over the head of the body, wherever the hit met it, in its stomach as in its head: the server
measures the head as the hit met it, the middle across of its head's hitboxes at their highest corner as
its traces pose them (`cs::headTopOf`, of a model with none of a head's all its hitboxes, of one of no
hitbox the top of its box), so that it stands over a ducked player's head as over a standing one's;
measured before the hit's harm, as a death poses the body anew. The scene's camera projects it each frame (`nx::render::screenPointOf`), and the number's
digits, at their whole size, stand a gap of the screen's height over it, however near or far the body is,
over everything else, walls and all, under the HUD; it moves by the time it has shown, each frame, whatever
the frame rate. As `data/cstrike/damageNumbers.toml` makes it look (`cs::DamageNumberLook`): in a heavy
italic Verdana among the system's fonts, its line 4.5% of the screen's height, its digits 1% of it over
the head, white, a head's red, outlined in black; from 0.3 of its size it fades in and pops up to
1.3 of it over 0.12 seconds, easing out, settles back to its size over 0.12, rises 6% of the screen's
height over its 0.9 seconds, easing out, from up to 2.5% of the screen's height to a side, and over its
last 0.3 fades out shrinking to 0.65 of its size; 64 show at most, the oldest going first. Its glyphs are
drawn once at the largest it shows and only ever made smaller, filtered linearly, which keeps it sharp.

**The step indicator** (`cs.client:stepIndicator`), the project's own, nothing of the original's, as games
show the sounds a player hears: while `hud_stepindicator` is 1 and `hud_draw` draws the HUD, each
entity's body heard stepping (a type with a stride: zombies, monsters; `cs::EntityStep`, which the server
tells every client of) shows a mark of bloody footprints on a ring about the middle of the screen, the
way the body is from where the player looks (ahead at the top, to the right on the right, behind at the
bottom), its toes toward it; the players' steps are not shown. As `data/cstrike/stepIndicator.toml` makes
it look (`cs::StepIndicatorLook`): deep red, 0.95 opaque at most, 6% of the screen's height across on a
ring 17% of it about the middle; a step fades its body's mark in over 0.3 seconds from what it showed,
pops it a quarter larger and back over 0.25, holds it 0.8 seconds and fades it out over 1; a body within
160 units shows wholly, fading out to none at 1600, farther than a bot hears a step (1100); at most 12
marks show, the loudest first, none closer to another than 0.8 of their size, of 64 bodies kept, the
faintest giving way. The prints are drawn once on the CPU, bare feet with their toes, a heel's smear
behind each and drops about them, their edges ragged and their blood darker in clots and at their rims,
thinly outlined in black, in shades that the look's color tints, turned 32 ways
(`StepIndicator::kStepMarkTurns`), the nearest way showing; the marks move each frame by the time since
the steps.

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
maximum, and a clip of 1000 and a reserve of 12345.

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
| Buying | The buy menu, below, over [10 §8](10-counter-strike-gameplay.md#8-buying)'s rules |
| Scoreboard | Name, frags, deaths, ping, status (dead, bomb, VIP), *mute* |
| MOTD | **Restricted and safe** text or HTML (a subset rendered by the UI, no scripts). External links open in the system browser after confirmation |

**The main menu** (`cs.client:mainMenu`, `:mainMenuLayout`, `:mainMenuPanel`): the original's pattern in the
project's own look. Its items are data, `data/cstrike/mainMenu.toml`, as the original lists its menu's in
`resource/GameMenu.res`: what each does (`action`) and when the menu lists it (`listed`: `always`, or `inGame` for
over a map played alone), from the top. Today it lists going back to the map played and leaving it, both over a map
alone, a new game, and ending the game; the options and the server browser come with what they open. It shows
the game's name at the top left and the items down the left, the last a fixed way over the screen's foot, where
the original's menu stands, with the keys under them: over its background while no map is played, over the scene
darkened while one is, which plays on under it, as a game of more than one player does.

Its background is the active content's own: `background` in the menu's data names a background layout among the
game's files, the original's `resource/BackgroundLayout.txt`, which places the pictures of the original's menu
(`resource/background/800_<row>_<column>_loading.tga`, twelve tiles of a screen of 800 by 600) on a screen of
its own size (`nx.assets:backgroundLayout`). They are put together into one picture as the menu is first drawn
(`cs.client:menuBackground`), which covers the screen whole without changing its shape, its middle on the
screen's and what is too wide or too tall left off the edges, where the original stretches it; and it is
darkened, so that what the menu writes reads over it. A background that cannot be read is logged, and the menu
shows over its own dark ground, as in a content that has none.

Up and Down select the item before and the one after, around the ends; Enter chooses the one selected; the mouse
selects the item it is over and its left button chooses it; Escape goes back to the map played. `next` opens on it
when no map is played, and comes back to it as the map is left; over a map played, Escape opens it whatever is
bound to that key, as in the original, the keys held for the game let go and the cursor shown. The keys bound to
`toggleconsole` open the console's window over it; the other keys run no bind while it is open. An item does what
the console's command does, queued as a bind queues its own (`app.client:mainMenu`): `disconnect`, `quit`. The
fonts it writes in are the project's own; a client that cannot have them does not start. What it writes is in
the player's language ([§7](#7-localization)), but the game's name.

An item that has more to ask opens a page beside the items, where the original opens a window: as tall as from the
game's name to the last item's foot, with what it is for on a button at its foot and another that goes back. **A
new game** lists the game's maps, the `maps/<name>.bsp` of its own folders (`cs::mapNamesOf`: the user's content
and the installation's `cstrike/`, not those of its base, `valve/`), in the order of the alphabet: Up, Down, Page Up, Page Down, Home and End select one, the list following it; the wheel scrolls the list,
three rows a notch, and a thumb down its right shows how far; the mouse selects the map it is pressed on. Enter, or
`START`, plays the map selected by the console's `map <name>`, a map the console could not be told of left out;
Escape, or `BACK`, goes back to the items, and another item chosen leaves the page.

**The loading screen.** A map takes seconds to load, and the frames wait for it. So `map` (and `nav_generate`,
which plays the map again) only asks for the map: the next frame tells that it is loading, over the menu's
background, the game's name where the menu writes it and `LOADING` and the map's name where its items stand; the
frame after loads it, and the screen stays as told until the first frame of the map shows. The console's commands
that follow on the same line run once the map is loaded, so that they act on it (`map de_dust2; bot_add`), held
as a `wait` holds them (`nx::console::Console::wait`), as the original's run after its own loading. A map that
cannot be loaded is reported as its command would report it, on the console, and the client stays where it was: on
its menu when it plays no map. The screen does not move while the map loads: a bar that follows the loading comes
with loading off the main thread.

**The menus' look** (`cs.client:menuDesign`) is one for the project's own menus: a design 1080 lines tall, made as
large as the screen (`MenuScale`); the sizes of text made of the families a menu's data names for that height
(`makeMenuFace`); and the colors, warm white text and gold over dark glass.

**The buy menu** (`cs.client:buyMenu`, `:buyMenuLayout`, `:buyMenuPanel`): the project's own look, where the
original has a key menu and a VGUI one, and the original's keys. `buy` (`b`) opens it, or closes it; `buyequip`
(`o`) opens it on its equipment. Over the scene darkened, under the HUD's numbers, it shows:

| Part | What it shows |
|------|---------------|
| What heads it | Its name, the player's team, the time left to buy, on a line that runs down with it, smoothly: its fill drawn each frame from the time left at the moment that frame shows, by less than a pixel at a time, while the clock beside it reads the same time in whole seconds; the money, and what would be left after the item selected, or how much it falls short, in red |
| Tabs, on the left | Pistols `1`, shotguns `2`, SMGs `3`, rifles `4`, machine gun `5` and equipment `8`, those with something for the player's team, each with the picture of its first item; under them, the ammo of the primary weapon `6` and of the pistol `7`, with what is carried and what a box costs |
| Items, on the right of the tabs | Those of the tab shown, three to a row, more when a mod sells more: each with its picture, its key, its name and its price; dimmed with its price in red when it costs more than the player has, `OWNED` or `WORN` when it has it |
| What the item selected is | A gun's damage, rate of fire, what armor lets through and move speed, with a bar of each; its clip, what is carried of its ammo at most, its reload and what a box costs; armor's `about`; whether it takes the place of a weapon held, and what buys it |
| Under them | What the player carries: its primary weapon and its pistol, with their rounds, and its armor; what `r`, or a click, buys again of what it last had, with what it costs, when there is anything; and the keys |

A first key opens a tab and the next buys on it, as the original's two menus do, so that `b` `4` `2` still buys
the CV-47 and `b` `8` `2` a vest and a helmet: the keys are those of the `slot1` to `slot10` commands, whatever
they are bound to; `slot10` (`0`) and Escape go back a level, then close; `cancelselect` too. `6` and `7` buy a
box at once. The mouse shows while it is open: over an item it selects it, and a click buys it or opens a tab;
Enter buys what is selected. Unlike the original's, it stays open as the player buys, and the player goes on
walking: the keys it has no use for go to their binds, and what comes up always reaches them. Its look is the
menus', so that it takes the same share of any screen, in the middle of a wider one.

It is not a screen of its own: where the player may not buy (outside a buy zone, its time over, dead), the menu
does not open, and closes as it comes to that. `cs::PlayerSnapshot::buying` tells the client so, and how long is
left, and the client weighs what the player can afford by the rules the game buys by (`cs.shared:buying`). A
refusal, of the menu or of a buy command, is told for two seconds over the game, or over the menu while it is
open, as well as printed on the console. `next` runs it for the map played as one part (`app.client:buyMenu`,
`ClientBuyMenu`): the commands, the events it takes, what draws it and the notices.

What it sells is data: a weapon shows by its file's `purchase.menu`, a weapon made as the game runs too once it is
complete, and the armor and the families it writes in by `data/cstrike/buyMenu.toml`. Its pictures are TGA files
among the game's: the original's `gfx/vgui/` today, read as a map's content is, and an item shows without one
that cannot be read. It writes in the project's own font, Barlow, regular and semi-bold (`data/fonts/`,
[ADR-0031](adr/0031-the-project-font.md)), the same on every system.

## 5. Console

- Opens with `~` (`toggleconsole`), with history, command and cvar autocompletion (with
  description and range), colors per log level and search.
- The same command language as the original: the `;` separator, quotes, `alias`,
  `+command`/`-command`, `wait`, `exec`, `echo`, `toggle`, `incrementvar`.
- **Explicit errors:** an unknown command or cvar, or an invalid value, yields a message
  with a suggestion ("did you mean...").
- **Content:** `content_list`, `content_scan`, `content_add <path>` and
  `content_profile <profile>` ([06 §2.5](06-assets-and-resources.md#25-local-configuration)).

**Today** (`nx.ui:consoleDialog`): `ConsoleDialog` is the console's window over `nx.console`, its
output a `RichText` and its input line a `TextEntry`. A line typed and sent with Enter is shown
after `] `, as the original shows it, kept in the history and queued on the console's buffer, which
runs it in its turn. As a command's name is typed, the window offers the names it completes to
(commands, cvars and aliases, at most eight), each with what the console tells of it (a cvar's
value and what it accepts, a command's description); Up and Down choose one, and Tab completes the
name to the one chosen, or the first, and a space. Without names offered, Up and Down walk the
history; Page Up and Page Down scroll the output by half its rows, and a line sent brings back the
newest. Replies are shown in one colour, failures in another, and what is logged in the colour of
its level: `ConsoleLogSink` keeps the lines logged on any thread until the main thread shows them.
`next` opens the window with `toggleconsole`, which the default binds put on `~` and `` ` ``, and
closes it with the same keys or Escape: while it is open the keys and the typed text are its, the
keys held for the game are let go and the cursor shows. Its font is the `Default` of the scheme of
the installation's windows (`resource/TrackerScheme.res`, Tahoma 16 at 768 lines and up: 13 pixels
per em, not smoothed), found among the system's fonts (`cs.client:windowFonts`, which `cl_showfps`'s
label shares); without the scheme or the face the console has no window, and the reason is logged. `cs::ConsoleWindow` is the window
`next` and `nxview` both show: the font, the atlas of its characters put on the GPU with the screen's
pictures, and the dialog, painted while it is open. What is logged and what the console
replies show in it as well as where they always went. Failures read as the player reads them:
what failed, then each cause by its name and detail (`map de_dust3 (NotFound: maps/de_dust3.bsp)`).
`nxview --console <line>` and `--typing <text>` draw the window over a frame. Its place (the middle
three quarters of the screen) and its colours are the project's until the original's are measured
from reference shots; search comes later.

### 5.1 Preserved client cvars (selection)

| Group | Cvars |
|-------|-------|
| Network | `rate`, `cl_updaterate`, `cl_cmdrate`, `cl_cmdbackup`, `ex_interp`/`cl_interp`, `cl_lw`, `cl_lc` |
| Video | `fps_max`, `fps_override`, `gamma`, `brightness`, `gl_vsync` (alias of `vid_vsync`), `fov` (with the original rules), `hud_draw`, `r_drawviewmodel`, `r_decals`, `mp_decals`, `cl_corpsestay` |
| HUD | `hud_fastswitch`, `hud_centerid`, `hud_saytext_time`, `hud_deathnotice_time`, `cl_righthand`, `cl_crosshair_*`, `cl_dynamiccrosshair`, `cl_showfps`, `net_graph`; the project's `hud_damagenumbers`, `hud_stepindicator` |
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
§5.1 that have a consumer today, with Counter-Strike 1.6's names and starting values, and the
project's keyboard mapping choice (`in_keymap`, §6):
`sensitivity` (3), `m_yaw` and `m_pitch` (0.022), `fps_max` (100, held as the original holds it,
[05 §12](05-rendering.md#12-latency-and-frame-pacing)), `fps_override` (0: `fps_max` at most 100),
`gl_vsync` (0, where the original starts it at 1: `fps_max` alone paces the frames, the project's
choice), `gamma` (2.5, from 1.8 to 3), `brightness` (1, from 0 to 3), `hud_draw` (1), `cl_showfps`
(0: the frames a second in the top left corner, §3.2), `r_fidelity`
(`legacy`, or `enhanced`: the enhanced look, [ADR-0025](adr/0025-enhanced-rendering.md)), `r_decals`
(4096, from 0, the most decals on faces) and `mp_decals` (300, from 0, what a game of more than one
player holds `r_decals` to as it is entered, [05 §8](05-rendering.md#8-bsp-world)),
`_cl_autowepswitch` (1, a weapon given heavier than the one held is drawn,
[10 §7.3](10-counter-strike-gameplay.md#73-behaviors)), `volume` (0.7,
from 0, the gain of everything heard, [11 §8](11-audio.md#8-cvars)), `s_device` (empty: the
system's default sound output; else one `s_devices` lists, opened once the startup files and the
command line ran), `s_play_unfocused` (0: silent while the window is not focused, as the original
is), all archived, and `texgamma` (2) and `lightgamma` (2.5), from 1.8 to 3, which the original does not archive.
`next` registers them, with the game's and the HUD's below, as one `ClientSettings` (`app.client:settings`),
reads them as it starts and writes `config.cfg` as it ends. A change from the console takes
effect as soon as what reads it reads it again: `in_keymap` with the next key press, the mouse's
as the mouse moves, `fps_max`,
`fps_override`, `hud_draw`, `cl_showfps`, `r_decals`, `volume` and `s_play_unfocused` with the next frame, `_cl_autowepswitch` with the next weapon
given, the picture's (`gamma`, `brightness`,
`texgamma`, `lightgamma`, `r_fidelity`) with the next map, and `gl_vsync` when `next` starts again
(the pace it sets with `fps_max`, with the next frame).
`config.cfg` keeps the binds too (§6), before the cvars, in the original's order. `cs::HudCvars` registers the
crosshair's `cl_crosshair_color` (`50 250 50`), `cl_crosshair_size` (`auto`) and
`cl_crosshair_translucent` (1), and `cl_righthand` (1, the weapons in the right hand, else in the
left: the view models mirrored by it, [05 §9](05-rendering.md#9-studio-models-and-gpu-skinning)),
archived, which each frame reads. `cs::CorpseCvars` registers `cl_corpsestay` (600, from 0 to 600),
archived: the seconds a dead body stays, a player's or an entity's, before it fades out over a second
and is drawn no more ([10 §7.4](10-counter-strike-gameplay.md#74-damage)). `cs::DamageNumberCvars`
registers `hud_damagenumbers` (1), archived: whether the damage of the player's hits shows (§3.2).
`cs::StepIndicatorCvars` registers `hud_stepindicator` (1), archived: whether the steps of the entities'
bodies heard show around the middle of the screen (§3.2).
`cs::GameCvars` registers the
cvars of the game's rules a server sets, which the local game plays by until the server comes, not
archived and told to the players (`Notify`), each a number, any of them, as the original's are,
held where the original's game holds it as it reads it and set back as it sets it:
`mp_freezetime` (6 seconds) and `mp_roundtime` (5 minutes), read as each round starts, cut to whole
seconds, the freeze held to 0 to 60 and the round to 1 to 9 minutes (`cs::readRoundCvars`,
ReadMultiplayCvars), `mp_startmoney` (800), given as the player joins and as the game restarts,
held to 800 to 16000 (`cs::readStartMoney`, CheckStartMoney), `sv_restartround` and `sv_restart`
(0), read once a second, cut to whole seconds and held to 60, both set back to 0 when one asks for
a restart (`cs::restartDelayOf`, CheckRestartRound,
[10 §3](10-counter-strike-gameplay.md#3-match-and-round-flow)), `mp_footsteps` (1: any value but
0 lets the steps sound), read at each step ([11 §3](11-audio.md#3-original-semantics-to-preserve)), and
`mp_friendlyfire` (0: any value but 0 lets a player's attacks hurt its teammates), read at each attack
([10 §7.4](10-counter-strike-gameplay.md#74-damage)), and `mp_buytime` (1.5 minutes), read at each purchase,
cut to whole seconds and held to 15 at least (`cs::readBuyTime`, CanPlayerBuy,
[10 §8](10-counter-strike-gameplay.md#8-buying)).
A number past what `int()` holds is cut as the original's processors cut it, to the least whole
number (`nx::truncateAsX86`). `vid_vsync` as the name of
`gl_vsync`, and the other cvars, come with their consumers.

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
  always reads the value back; a `TextRule` may narrow it further, as `cl_crosshair_size`
  takes `auto, small, medium or large` and writes the word) and `enum` with `Count` (accepts
  the number or the enumerator name and writes the number). Numbers may have a range. Rejected text
  never becomes 0: `mp_freezetime: expected a number, got "abc"`.
- **The input line's help:** `completions(prefix, limit)` gives the commands, cvars and aliases
  whose names begin with what is typed, in any case and in the order of their names, each with its
  description (an alias's body), and a cvar's value and what it accepts; `completionPrefix(line)`
  finds the name being typed: the first word of the last command, after the last `;` outside
  quotes, while nothing follows it. `CommandHistory` keeps the last 128 lines sent, the newest last,
  without blank lines or the newest one again, and walks them with the up and down keys back to
  the line being typed.

| Flag | Effect |
|------|--------|
| `Archive` | Goes into `config.cfg`: `Console::archivedCvars()` produces the `name "value"` lines |
| `Notify` | A change is announced to the players and the cvar is listed in `A2S_RULES`. The console only marks it; whoever announces compares `revision()` |
| `Cheat` | The console only changes it with cheats enabled (`Console::setCheats`, which follows the server's `sv_cheats`). Otherwise, `CheatProtected` |
| `ServerProtected` | The value comes from the server: the console never changes it (`ServerProtected`), only the code does. On the client, this is the case of the movement cvars replicated for prediction |

The code reads and writes through the typed handle (`Cvar<float>`), which validates the
type and range, but not the flags: `set` returns the refusal, and `setAccepted` takes a value the
code knows the cvar accepts (a rule holding a value back within its bounds), a refused one breaking
its precondition. The `Console` belongs to one thread (the main one,
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
  physical position (`Key`, the USB HID usage code) and their layout's base character
  (`KeyChanged::character`, from SDL's keycode), the mouse as relative motion in its own
  counts (`MouseMoved`), and the window gives a GPU backend its native handles. Typed text arrives
  apart from the keys, as the keyboard's layout and the system's input method make it
  (`TextEntered`, UTF-8), while text input is on (`Window::setTextInput`): the console and the
  menus' text entries turn it on while they have the keyboard.
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
each position with its layout's base character, separately from typed text. `in_keymap keycode`
(the default, also 1) selects the layout's printable ASCII character; `in_keymap scancode`
(also 0) selects the US position. Both keep the named controls and the keypad's `KP_` keys,
regardless of Num Lock or SDL's keypad hints, and the key below Escape as the console's `` ` ``
on every layout, as the original's positions make it: the `'` a Brazilian ABNT keyboard types
there, a French one's `²` or a German one's `^` would leave `toggleconsole` out of reach. A character outside the original's bind names,
or an unknown character, runs nothing in keycode mode; scancode mode remains an explicit choice.
Like the other enum cvars, the mode is archived by number. `bind <key> [command]`, `unbind`,
`unbindall` and `bindlist` set and list them: a key is one ASCII character (a letter in either
case) or one of the original's names in any case (`keyNamed`), and
a command holds no `"`, so that `config.cfg` reads it back. `defaultBindings` are Counter-Strike
1.6's, those its options restore, and `config.cfg` keeps the binds: `unbindall`, then a `bind
"<key>" "<command>"` line per bound key. `BoundInput` runs them as the original does: a key
going down queues its command, followed by the key's number when it starts with `+` (`+jump
32`); coming up, the same with its `+` turned to `-` (`-jump 32`), and nothing for other
commands. The number follows the whole command, as in the original: a key bound to `+jump;
wait; -jump` queues `+jump; wait; -jump 32`. A key the system repeats queues nothing more, a key
that comes up without having gone down here queues nothing, and every key down comes up when the
window loses the keyboard. Each physical key keeps the logical key it selected on its press,
even if the layout or `in_keymap` changes before release. Several physical keys selecting one
logical key (both Ctrl keys, for example) hold its action until the last comes up. A press keeps
its own release command, so rebinding or unbinding a held key cannot leave its action stuck.
A repeated down event received without its initial press does nothing, including after focus
loss. Each notch of the wheel presses its key and releases it at once.

`cs::PlayerInput` (`cs.client`) registers the player's buttons, `+forward`, `+back`,
`+moveleft`, `+moveright`, `+moveup`, `+movedown`, `+left`, `+right`, `+lookup`, `+lookdown`,
`+strafe`, `+speed`, `+jump`, `+duck`, `+use`, `+attack`, `+attack2` and `+reload`, and their cvars with Counter-Strike 1.6's
values: `cl_forwardspeed`, `cl_backspeed` and `cl_sidespeed` (400, archived), `cl_upspeed`
(320), `cl_movespeedkey` (0.52), `cl_yawspeed` (210), `cl_pitchspeed` (225) and
`cl_anglespeedkey` (0.67). `nextCommand` makes a user command as Counter-Strike 1.6's `CL_CreateMove`
makes it: each movement button asks its speed for the share of the command it was held (all of
it, but half when it went down during the command, as `CL_KeyState` counts), together they are cut
to the player's speed limit, and then `+speed` asks `cl_movespeedkey` of each, 0.52 at most: a player
walks at a share of its own speed, 130 with a knife, below the 150 its steps sound at, so that it
walks unheard (Half-Life's client takes the share before the limit and walks at 208). `+jump`,
`+duck`, `+use`, `+attack`, `+attack2` and `+reload` count also when pressed and released within
one command, so that `+jump` on `MWHEELDOWN` jumps once a notch. `turn` turns the view each frame as `CL_AdjustAngles` does:
`+left` and `+right` (which move instead while `+strafe` is held), `+lookup` and `+lookdown`, up
to 89 degrees. The other buttons come with what reads them; the mouse
always looks, so `+mlook` and `+klook` are not there. `selectWeapon` makes the next command select
a weapon: `next` registers `weapon_<id>` for each weapon (`weapon_knife`, `weapon_usp`, and those
made as the game runs: [10 §7.5](10-counter-strike-gameplay.md#75-weapons-made-as-the-game-runs)),
as the original's game takes them, and `lastinv`, which selects the weapon held before; `slot1` to
`slot10` are the keys of the buy menu ([§4](#4-game-menus-and-screens)), and select nothing yet with it closed:
that, `invnext` and `invprev` come with the weapon menu. `next` plays with them:
Counter-Strike's binds, and `noclip` on `v`. `play <sound>` and `playvol <sound> <volume>` play
a sound heard everywhere, as the original's do ([11 §2](11-audio.md#2-architecture)).

## 7. Localization

- UI strings in per-language files (`locale/pt-BR.toml`, `locale/en.toml`), with typed keys. A nonexistent
  key is a build error, not empty text.
- `titles.txt` and the original texts are still read and can be overridden by a
  translation.
- Plugins have their own locale files ([14](14-rook-language.md#12-strings-formatting-and-translation)),
  read by `rookc`.

**Today** (`cs.shared:texts`, [ADR-0035](adr/0035-texts-as-typed-data.md)): a text is a field. `cs::Texts` has
a struct for each part that writes (`MainMenuTexts`, the first) and a `std::string` for each of its texts, so
that code reads `texts.mainMenu.newGame` and a text no struct declares does not compile; no keys are
generated. A language is a file, `data/cstrike/locale/<language>.toml`, named after its tag (`en`, `pt-BR`,
the two there are): a table for each part and a key for each text, read as the rest of the game's data is
(`nx.data`). A file that lacks a text, writes one empty or has one the game does not ask for is refused: a
language is whole or it is not offered, and a test reads every language of the repository. `cl_language`
(archived, `en` unless chosen) names the language; it is read as `next` starts, once the player's files and
the command line ran, and one the data does not have stops the start, telling the languages there are and
how to choose again (`next +cl_language en`). A change takes the next start, as the original's does. The
main menu writes by it; the buy menu's and the HUD's own texts are constants still, in English, until they
move to it. Texts with values in them, and the original's own (`titles.txt`), come with what writes them.

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
