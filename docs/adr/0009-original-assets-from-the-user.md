# ADR-0009 — Original assets read from the user's installation

- **Status:** Superseded by [ADR-0015](0015-free-content-and-optional-original-assets.md)
  (2026-10-20). The original installation is no longer mandatory and became an optional
  content source.
- **Date:** 2026-10-20

## Context

Recreating the game "just like the original" requires the CS 1.6 maps, models, sounds and
sprites, which are Valve's property and cannot be redistributed.

## Decision

- The game **reads the user's legitimate installation directly** (Steam), detected
  automatically or configured, mounted in the VFS with a priority below mods and downloads
  ([06](../06-assets-and-resources.md)).
- The original formats (BSP v30, MDL v10, WAD3, SPR, WAV) are read without a mandatory
  conversion.
- The repository and the binaries **do not contain** Valve assets. Tests use synthetic
  assets and numeric parity data.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| Distribute the assets | Copyright infringement |
| Require a prior conversion to an own format | Friction for the user and risk of visual divergence |
| Free assets only | Makes parity unfeasible now. It stays as a future community project |

## Consequences

- ✅ Full visual and audio parity, legally.
- ✅ Community maps work without conversion.
- ⚠️ Requires the original game to be installed. Documented at the first start, with a
  wizard.

Every decision above, except the mandatory installation, remains valid in ADR-0015.
