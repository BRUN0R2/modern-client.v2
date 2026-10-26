# ADR-0026 — One clean room source policy

- **Status:** Accepted
- **Date:** 2026-10-26

## Context

`RULES.md` prohibited consulting ReHLDS and ReGameDLL_CS, while the clean room policy
allowed consulting their public source to understand behavior. Contributions already cite
that policy. Repeating the source permissions in two documents let them contradict each other.

## Decision

The maintainer confirms that consulting these open-source projects is permitted.
[20 §3](../20-legal-and-licensing.md#3-clean-room) is the single normative source for
permissions and restrictions on reference material. `RULES.md` requires compliance with it
instead of maintaining another list.

The existing distinction between consulting behavior and copying code remains: consulting
the listed open-source projects is allowed, implementations are written for this project,
and behavioral parity is verified against the original. The source table in that document
defines the policy; this ADR records why it has one owner.

## Consequences

Contributors have one source policy to follow and update. References in code remain useful
provenance; the release's legal review remains a separate pending task in
[TODO.md](../TODO.md).
