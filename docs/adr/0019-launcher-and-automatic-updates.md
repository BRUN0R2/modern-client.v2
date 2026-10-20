# ADR-0019 — Automatic, independent updates for the client and the server

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

Online play needs the client and the server on compatible versions: the protocol rejects an
incompatible one ([08 §10](../08-networking.md#10-versioning)). Players who download builds by
hand fall behind, servers fill with rejections, and every release turns into support work. The
maintainer asked for modern, robust, dynamic and automatic updates: opening the launcher must
update the game first, intelligently, whenever there is an update; and the client and the
server must both receive updates, independently of each other.

## Decision

- **Two products, updated independently:** `client` (the launcher, `next`, the client's data
  and the free content) and `server` (`nextds` and its data), each with its own releases,
  versions, channels and signed manifests ([21 §2](../21-launcher-and-updates.md#2-two-products-updated-independently)).
  Compatibility between them is the protocol major, not the release number.
- **Client:** players open a launcher (`nextLauncher`, `app.launcher`) that updates itself,
  then the client, and only then starts the game; it pre-downloads the next release while the
  game runs.
- **Server:** `nextds` updates automatically at start and, while running, switches to a
  downloaded release only when its operator's policy allows (`sv_update_policy`: when empty by
  default, at a map change, on the operator's command, or never), never during a match.
- Update metadata follows **The Update Framework** principles: manifests signed with Ed25519
  by an offline release key, a short-lived online timestamp, rollback and freeze protection.
- Releases are **content-defined chunks addressed by BLAKE3**, shared by both products: any
  older release updates by downloading only the chunks it lacks.
- Releases install side by side and switch with **one atomic rename** per product; the previous
  one is kept until the new one starts, with automatic rollback.
- Installs from a package manager or a container image leave updates to it.
- It arrives in **M4**, with online play, using the HTTPS, Ed25519 and BLAKE3 support that M2
  and M4 bring.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| One release for client and server together | Every fix to either would force an update of both, and servers would restart for client-only changes |
| Manual downloads | Players and servers fall behind and online play breaks on every protocol change |
| Updating from inside the running game or server | A running program cannot replace itself cleanly, and a broken update would leave nothing to roll back to |
| Server updates only by hand | Abandoned servers stay on old protocols; the operator still controls *when* through the policy |
| Binary patches per pair of versions (bsdiff) | One patch per pair of versions; content-defined chunks serve any jump |
| A full download per release | Wastes bandwidth for small changes |
| HTTPS without signed metadata | A compromised server or mirror could ship any binary |
| A store (Steam) | Not available to an independent recreation, and it would tie the project to one platform |

## Consequences

- ✅ Players and servers stay current with small, verified downloads, each at its own pace.
- ✅ A failed update never leaves the game or a server unusable, and no match is cut.
- ⚠️ A protocol major change needs releases of both products; the handshake tells which side
  is behind until both have updated.
- ⚠️ The project runs release infrastructure: the signing keys (one offline), the metadata and
  a CDN. Losing the online key must not allow a release; losing the release key needs the
  rotation procedure of [21 §4](../21-launcher-and-updates.md#4-security).
- ⚠️ The launcher and the server's updater are more code to keep secure; they run nothing they
  did not verify.
