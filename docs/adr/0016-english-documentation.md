# ADR-0016 — English as the default language of the documentation

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

The documentation started in Brazilian Portuguese, with code identifiers and comments in
English. The repository is public and the project wants contributors and server operators
from the whole CS 1.6 community, most of whom read English. Two languages in the same
repository also split the vocabulary: a concept had one name in the code and another in the
documents that describe it. The maintainer decided that the documentation is written in
English.

## Decision

- English is the default language of every document in the repository: `docs/` (including
  [RULES.md](../RULES.md) and the ADRs), `README.md`, `TODO.md`, `CONTRIBUTING.md`,
  `SECURITY.md` and every `README.md` of a subdirectory, as it already was for code
  identifiers, code comments and commit messages.
- File names in `docs/` follow the language: numeric prefix and kebab-case English
  (`02-coding-standards.md`, `adr/0013-camelcase-convention.md`).
- Other languages appear only as content: translation files (`locale/pt-BR.toml`), the
  localized diagnostics of `rookc` (`--lang pt-BR`) and player-facing text.
- The existing Portuguese documents are translated, not summarized: the content, the section
  numbers and the decisions stay the same.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Keep pt-BR | Narrows the audience of a public project and keeps two vocabularies |
| Bilingual documents | Doubles the maintenance and the two versions drift apart |
| English for new documents only | Leaves the repository half in each language for years |

## Consequences

- ✅ One language for code, comments, commits and documents.
- ✅ The documentation reaches the whole community.
- ⚠️ Every link to a renamed document changes once; the links in code comments
  (`docs/02-coding-standards.md, section 6`) change in the same commit as the rename.
