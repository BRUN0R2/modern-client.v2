# ADR-0020 — In-house retained UI toolkit for the menus, the screens and the console

- **Status:** Proposed
- **Date:** 2026-10-22

## Context

The classic HUD is drawn ([12 §3](../12-ui-hud-and-console.md#3-hud), `nx.render:screen`,
`cs.client:hud`). What remains of the interface is made of windows: the main menu, the options,
the server browser, the team and character selection, the buy menu, the scoreboard, the MOTD and
the console ([12 §4](../12-ui-hud-and-console.md#4-game-menus-and-screens),
[§5](../12-ui-hud-and-console.md#5-console)). [12 §2](../12-ui-hud-and-console.md#2-ui-components)
names an in-house retained toolkit for them, and the [TODO](../../TODO.md) keeps the choice open
against RmlUi, to be weighed by visual fidelity, performance and ease for plugins.

What decides it:

- **The original is VGUI2.** Counter-Strike 1.6's menus and its console are VGUI2 windows: panels,
  frames with a title bar, buttons, text entries, list views, combo boxes, check boxes, sliders,
  tabs and rich text, placed by absolute coordinates (with `r` and `c` prefixes that anchor to the
  right and the middle), coloured, bordered and lettered by schemes. The installation carries much
  of this as data: the schemes (`platform/resource/TrackerScheme.res`,
  `cstrike/resource/ClientScheme.res`), the layouts of many dialogs (`cstrike/resource/*.res`,
  `platform/resource/*.res`) and the strings (`*_english.txt` and the other languages). Other
  windows, the console among them, are built in code: their layout comes from reference shots, as
  the HUD's did.
- **Fidelity is measured to the pixel**, as the HUD is ([12 §3.2](../12-ui-hud-and-console.md#32-scale-and-resolution)):
  the toolkit must place, colour and letter each widget exactly where the original does.
- **Plugins draw screens too** ([13 §7](../13-mod-system.md#7-plugins-on-the-client),
  [12 §3.5](../12-ui-hud-and-console.md#35-hud-for-plugins)): full screens from declarative
  layouts, in a strict sandbox.
- **The project's rules:** C++26 modules, typed errors without exceptions (ADR-0005), the RHI and
  `nx.render` as the only way to the GPU (ADR-0004), declared dependencies only, GPL-3.0-compatible
  licences (ADR-0010).
- **Text** in VGUI2 is TrueType (Tahoma, Verdana and others named by the schemes, at given sizes,
  weights and antialiasing), unlike the HUD's bitmaps: whatever the toolkit, glyphs must be
  rasterized from TrueType fonts.

## Decision

- **`nx.ui` is an in-house, retained toolkit** (layer 3, [01](../01-architecture.md)) drawn through
  `nx.render:screen`: a tree of widgets that keep their state between frames, laid out, styled and
  drawn by the toolkit, receiving the platform's input (keys, text with IME, mouse) through focus
  and capture.
- **Its model is VGUI2's**, so that the original's data is read as it is:
  - **Widgets:** panel, frame (title bar, close, move, resize), label, image, button, check box,
    radio button, text entry (single and multiple lines, selection, clipboard), rich text, list
    and list view, combo box, slider, scroll bar, property sheet (tabs) and menu.
  - **Layout:** absolute positions and sizes in the units the `.res` files use, with their anchors
    (`r`, `c`, `pinCorner`, `autoResize`), and the proportional scaling the scheme asks for.
  - **Schemes:** colours, borders and fonts by the names the `.res` files and the schemes use.
- **The original's files are read when the `classic` profile is on**
  ([06 §2.5](../06-assets-and-resources.md#25-local-configuration)): its schemes, its `.res`
  layouts and its strings, through the VFS. Without it, the project's own free layouts, schemes
  and strings, in the same model, take their place (ADR-0015). Windows that the original builds in
  code (the console, the scoreboard) get layouts of the project's own, measured on the original.
- **Text:** TrueType glyphs rasterized into atlases on the GPU, by a dependency declared when the
  text arrives (FreeType is the candidate: the FreeType Licence is GPL-3.0-compatible), with
  kerning, the schemes' sizes, weights and antialiasing, and UTF-8 throughout.
- **The console comes first:** the original's console window over `nx.console` (output in colours
  per log level, the input line with history and completion,
  [12 §5](../12-ui-hud-and-console.md#5-console)); then the main menu and the options, then the
  game's menus.
- **Plugins** declare their screens in the same model (a typed layout, not a `.res` file), with
  the sandbox of [13 §7](../13-mod-system.md#7-plugins-on-the-client).
- **Dear ImGui** stays for development tools only, as [12 §2](../12-ui-hud-and-console.md#2-ui-components)
  says.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| RmlUi (HTML and CSS) | A mature library under the MIT licence, and HTML and CSS are familiar to plugin authors; but VGUI2's absolute layouts and schemes would have to be rewritten by hand, or converted, as documents and style sheets, its box and flow model is not VGUI2's, and matching the original to the pixel would fight the CSS engine instead of describing it. It brings its own font engine (FreeType) and a large dependency to keep within the project's module and error rules |
| A full HTML engine (CEF, Ultralight) | Far larger, slower to start and harder to sandbox; Ultralight is not free software. The MOTD needs only a restricted subset of HTML ([12 §4](../12-ui-hud-and-console.md#4-game-menus-and-screens)), which the toolkit's rich text can render |
| Dear ImGui for the game's interface | Immediate mode, built for tools: it cannot be styled into VGUI2's look, nor read its layouts |
| Valve's VGUI2 itself | Not available: its source is not public, and leaked code is forbidden ([20 §3](../20-legal-and-licensing.md#3-clean-room)) |

## Consequences

- ✅ The original's own layouts, schemes and strings drive the classic look, so fidelity is a
  matter of reading them right, and is checked to the pixel against reference shots.
- ✅ One toolkit for the console, the menus and the plugins' screens, drawn by the same screen
  renderer as the HUD, with no large dependency.
- ⚠️ More code to write and keep: the widgets, text editing, layout and focus. They arrive in the
  order the player needs them, the console first.
- ⚠️ A TrueType rasterizer becomes a dependency, and VGUI2's text was drawn by Windows: matching
  its glyphs to the pixel on every platform may need its hinting and antialiasing reproduced, or
  measured tolerances recorded where it cannot be.
- ⚠️ Plugin authors learn the project's layout model instead of HTML and CSS; the Rook SDK
  documents it with examples.
