# ADR-0035 — The game's texts are typed data: a field for each text, a file for each language

- **Status:** Accepted
- **Date:** 2026-11-10

## Context

The project's own menus wrote their texts as constants in the code that draws them, in English
([12 §4](../12-ui-hud-and-console.md#4-game-menus-and-screens)). [12 §7](../12-ui-hud-and-console.md#7-localization)
asked for per-language files with typed keys *generated* for C++ and Rook, a nonexistent key being a build
error and not empty text. The main menu is the first consumer.

What decides it:

- **A text the code asks for must exist in every language**, and the failure must come early: at build time for
  the code, and before a player sees it for a language's file.
- **The project already reads typed data**: `nx.data` reads a TOML file into a struct described to `nx.meta`,
  and refuses a key the struct does not declare and a field the file does not write
  ([02 §5](../02-coding-standards.md#5-no-magic-numbers)). The buy menu's and the main menu's data are read so.
- **A generator is one more moving part** ([RULES §14](../RULES.md#14-engineering-decisions)): a tool to build
  before the code, its output to keep out of the repository or in step with it, and a second description of what
  the struct already says.

## Decision

- **A text is a field.** The game's texts are `cs::Texts` (`cs.shared:texts`): a struct for each part that writes
  (`MainMenuTexts`), a `std::string` field for each of its texts. Code reads `texts.mainMenu.newGame`: a text no
  struct declares does not compile. No keys are generated: the struct is the list of keys, written once.
- **A language is a file**, `data/cstrike/locale/<language>.toml`, named after its language tag (`en`,
  `pt-BR`): a table for each part, a key for each text, by the fields' names. `cs::readTexts` reads it through
  `nx.data` and refuses a file that lacks a text, writes one empty, or has one the game does not ask for:
  a language is whole or it is not offered. A test reads every language of the repository, so a text added to
  the struct and not to a language fails the CI, not the player.
- **`cl_language`** (archived, `en` unless chosen) names the language by its tag. It is read as the client
  starts, once the player's files and the command line ran, and a language the data does not have stops the
  start with the languages there are and how to choose again (`next +cl_language en`): never another language
  in silence ([RULES §5.2](../RULES.md#52-design)). A change takes the next start, as the original's language
  does.
- **English is written first**: `en.toml` is the language the code is written beside, and what a new text is
  first written in.
- **Parts move to it as they are touched**: the main menu first; the buy menu and the HUD's own texts follow,
  each in its change, their constants removed as they do.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Keys generated from `en.toml` into a C++ enum and a Rook module, as 12 §7 first said | The same guarantee (a nonexistent key does not build) for more parts: a generator, a build step before every module that writes, and generated sources. The struct gives it with what `nx.data` and `nx.meta` already do. Rook's own keys are `rookc`'s matter ([14 §12](../14-rook-language.md#12-strings-formatting-and-translation)): a mod's locale files are read by its compiler |
| The original's `resource/*_<language>.txt` tokens (`#GameUI_GameMenu_NewGame`) | They are the installation's, absent without the `classic` profile and without the texts the project adds; and a token is a string looked up as the game runs, which fails as it runs. They stay what the original's own layouts are read with (`nx.assets:localization`) |
| A string table by enum (`EnumArray<Text, std::string>`) | One flat enum for the whole game, or one for each part plus a table for each: the struct says the same with the names the data already uses, and `nx.data` reads it as it is |
| A missing text falling back to English | A hidden recovery ([RULES §5.2](../RULES.md#52-design)): a language half translated would ship without anyone seeing it. The file is refused, and the test names the text |

## Consequences

- ✅ No new mechanism: a struct, its description and a file, as every other piece of the game's data.
- ✅ A text asked for and not declared does not build; a language that lacks one fails the tests.
- ⚠️ A text with values in it (a name, a count) needs its placeholders checked against what the code gives: that
  arrives with the first such text, as a typed format the struct declares, not as a string formatted as the game
  runs.
- ⚠️ A language whose characters Barlow lacks needs a font of its own ([ADR-0031](0031-the-project-font.md)):
  the menus draw Latin-1 today.
- ⚠️ The language is read as the client starts: choosing it in the options will ask for a restart, or for the
  parts to be made again, when the options arrive.
