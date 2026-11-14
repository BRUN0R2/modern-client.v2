# ADR-0036 — The player's language takes effect at once

- **Status:** Accepted
- **Date:** 2026-11-14
- **Amends:** [ADR-0035](0035-texts-as-typed-data.md), its `cl_language` decision

## Context

[ADR-0035](0035-texts-as-typed-data.md) read `cl_language` as the client starts and let a change take the next
start, as the original's language does, and foresaw that choosing it in the options would ask for a restart or
for the parts to be made again. The options now list it
([12 §4](../12-ui-hud-and-console.md#4-game-menus-and-screens)), and every other setting they list takes effect
at once, the picture's among them: the language alone tells the player to start the game again.

What decides it:

- **Nothing the texts make has to be made again but their layout**: the menus write them as they draw, by the
  fields of `cs::Texts`, and their fonts hold every character the languages use (Latin-1,
  [ADR-0031](0031-the-project-font.md)). What a menu laid out of the old texts is laid out again.
- **A setting that waits is friction** ([RULES §10](../RULES.md#10-interface-and-experience)), and a note telling
  so is one more state of the options to keep.
- **The console can set the cvar to anything as the game runs**, after the start checked it.

## Decision

- **`cl_language` is read as the client starts and again as it changes**, between two frames, whoever changes
  it: the texts of the language it names are read (`cs::PlayerTexts::ask`) in place of those the client had,
  and what writes them lays its texts out again.
- **A language the data does not have stops the start**, as ADR-0035 decided. **Asked for later, it is told**:
  the log names it and the languages there are, the texts stay those of the language before, and it is not
  read again until another language is asked for. Never another language in silence, and no retry each frame.
- **A choice's values are written as the menu draws**, by a text of the options' or as they are (a language's
  tag, `cs::ChoiceText`), as the names of the settings are: nothing the options show holds a copy of a text.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| The next start, as the original's language does (ADR-0035) | The only setting of the options that waits; the page needs a note to tell so, and the player a restart to see a choice they just made |
| Asking for a restart from the options | A dialog and a restart for what costs one read of a small file and a layout |
| Making the main menu again with the new texts | It forgets what the menu shows (the page open, the setting selected), and loads its fonts and background again, for texts it writes as it draws |

## Consequences

- ✅ Every setting of the options takes effect at once: the page's note of when a change takes effect, and its
  line, are gone.
- ✅ A language set from the console later is told and changes nothing else.
- ⚠️ A part that copies a text where it is made (the buy menu's and the HUD's constants, when they move to
  `cs::Texts`) must write it as it draws, or be made again as the language changes.
