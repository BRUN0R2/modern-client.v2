# ADR-0011 — `maxplayers` from 1 to 1024 as a policy limit

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

The original limits servers to 32 players. The request is to keep the original's minimum
and raise the maximum to about a thousand. The options discussed were 1000 and 1024.

## Decision

- `maxplayers` accepts **1 to 1024**, with a default of **32**.
- The cap is the named constant `kMaxPlayersSupported = 1024`, **a policy limit, not a format
  limit**: the slot is encoded as a varint and no network or file format depends on the cap.
- Player sets use bitsets of exactly 16 64-bit words.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| 1000 | 24 bits would be left unused in the bitsets, plus invalid values in the 10-bit index, with special cases in every loop |
| No cap | Impossible to validate and test. The policy cap is testable and can be raised later |

## Consequences

- ✅ Exact bitsets and indices, without special cases.
- ✅ Raising the cap in the future means changing one constant, without breaking the
  protocol.
- ⚠️ Bandwidth and CPU grow close to O(n²). Mitigation: interest management, per-client
  snapshots in parallel, automatic recommendations in the log and a weekly stress test
  ([08 §6.4](../08-networking.md#64-parallelism-and-scale)).
- ⚠️ A2S 1-byte fields saturate at 255 (a limitation of the legacy format, documented).
