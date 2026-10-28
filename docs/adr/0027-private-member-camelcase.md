# ADR-0027 — Private members use camelCase

- **Status:** Accepted
- **Date:** 2026-10-26

## Context

The maintainer's engineering instructions require camelCase identifiers without underscores,
including private members. `RULES.md`, the coding standard and `.clang-tidy` still required
the `m_` prefix, contradicting that instruction and ADR-0013's camelCase fields.

## Decision

Private members use ordinary camelCase, without a prefix: `pressedKeys`, `releaseCommands`
and `lightStyles`. This amends [ADR-0013](0013-camelcase-convention.md). Externally imposed
names retain the exceptions defined in [02 §3.1](../02-coding-standards.md#31-project-rule-camelcase).

Apply the convention to every new or renamed identifier. Existing `m_` identifiers remain
known inconsistencies; do not rename unrelated code as part of another change.
`RULES.md`, the coding standard and the identifier-naming configuration use the same rule.

## Consequences

New implementation members follow the maintainer's convention without artificial prefixes.
Existing code migrates when its identifiers change, without broad unrelated renames.
Identifier-naming analysis remains enabled; the obsolete required prefix is removed.
