# ADR-0031 — The project has a font of its own: Barlow

- **Status:** Accepted
- **Date:** 2026-11-03

## Context

The buy menu ([12 §4](../12-ui-hud-and-console.md#4-game-menus-and-screens)) has the project's own look, and
what it looks like depends on its font. It first wrote in the first of a list of families the system had: Windows'
Bahnschrift, then Verdana, then DejaVu Sans. So it looked different on each system, with widths its layout was
not seen with, and its heavier text was bold simulated on a regular face. Bahnschrift cannot be shipped: it is
Microsoft's. The maintainer chose to bundle a font of an open license.

## Decision

- **Barlow** ([jpt/barlow](https://github.com/jpt/barlow)), a grotesque of the DIN family's kind, is the project's
  font, for the interface the project draws in its own look. Its license, the SIL Open Font License 1.1, allows
  bundling it with a program of any license; it declares no reserved font name, and its faces may be embedded
  (`fsType` 0).
- **Two faces, in `data/fonts/`:** `Barlow-Regular.ttf` (family `Barlow`, weight 400) and `Barlow-SemiBold.ttf`
  (family `Barlow SemiBold`, weight 600), 212 KiB together, as GDI's family names (name ID 1) separate them, which
  `nx::ui::FontCatalog` follows. Weights arrive with what needs them.
- **They come with the program's data**, and nothing stands in for them: `data/cstrike/buyMenu.toml` names their
  families, and a family that cannot be found is a typed failure, logged, and no menu: never another font, in
  silence, with other widths ([RULES §5.2](../RULES.md#52-design)).
- **Where it comes from:** `ofl/barlow` of [google/fonts](https://github.com/google/fonts) at commit `6cdf01867df0813c2390f90dff7dc66c87f14cf7`:

  | File | SHA-256 |
  |------|---------|
  | `Barlow-Regular.ttf` | `95aa02c7c43096e0dd44d787ba6216864a67157e402adab59b35572e0c1577ea` |
  | `Barlow-SemiBold.ttf` | `86577cb32f8abe3673db53ca0f4221e6856751a4f6730c867e00f720f8bb1fc5` |

  Its license is beside them (`data/fonts/OFL.txt`), as it asks of a copy, and in `LICENSES/OFL-1.1.txt`, as REUSE
  asks.

What keeps the original's look keeps the original's fonts, read from the installation
([12 §2.1](../12-ui-hud-and-console.md#21-fonts)): the console, `cl_showfps`, the scoreboard.

## Alternatives considered

- **The system's fonts, a list of them:** no file to ship, and a different menu on each system, the case above.
- **Bahnschrift alone:** the look of the preview, and not ours to ship, nor on Linux.
- **Inter, Roboto Condensed:** of the same license, and good. Barlow is the nearest to the approved preview's look,
  and its digits are as wide as each other, as money and time want.

## Consequences

- The menu looks the same on every system, and its layout is checked with the font it ships with. A test reads the
  fonts from the data, and writes with them every character the menu's own text needs.
- 212 KiB more in the repository and in every copy of the program.
- A language whose characters Barlow lacks needs a font of its own for them when localization arrives
  ([TODO](../TODO.md)).
