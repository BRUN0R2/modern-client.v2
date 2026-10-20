# 09 — Dedicated server

## 1. Principle

Anyone who ran a CS 1.6 HLDS should bring up a `nextds` **without relearning anything**: the
same command-line parameters, the same configuration files, the same cvars and commands,
with the server showing up in server browsers and working with monitoring tools that use
A2S.

The new features (more players, Rook plugins, secure RCON, metrics) are **additive**.

## 2. Command line

```bash
nextds -game cstrike -port 27015 +maxplayers 32 +map de_dust2 +sv_lan 0
```

| Parameter | Meaning | Original |
|-----------|---------|----------|
| `-game <dir>` | Game (`cstrike`) | ✅ |
| `-port <n>` | UDP port | ✅ |
| `-ip <addr>` | Bind address | ✅ |
| `+maxplayers <n>` | Slots (1–**1024**, default 32) | ✅ (1–32) |
| `+map <name>` | Initial map | ✅ |
| `+<cvar> <value>` / `+<command>` | Run at boot | ✅ |
| `-basedir <path>` | Original CS installation (`goldsrcInstall` source, optional) | ✅ (same name as in HLDS) |
| `--content <profile>` | Content profile: `classic`, `open`, `dev` or `custom` | new |
| `-console` / `-noconsole` | Interactive console | ✅ |
| `-pidfile <file>` | PID for supervisors | ✅ (ReHLDS) |
| `-config <file.toml>` | New structured configuration | new |
| `-tickrate <n>` | Shortcut for `sv_tickrate` | new |

## 3. Players: from 1 to 1024

- `maxplayers` accepts from **1** (the minimum, as in the original) up to **1024**
  (`kMaxPlayersSupported`). The default is still **32**.
- The 1024 ceiling is **a policy, not a format** limit. The client slot is encoded as a
  varint, and player sets are bitsets of 16 64-bit words. Raising the ceiling in the future
  is a constant change, without breaking the protocol.
- Above 64 players, the server logs recommendations at startup (`sv_maxupdaterate`,
  `sv_relevance_distance`, map size), because bandwidth and CPU grow close to O(n²).

| Range | Status |
|-------|--------|
| 1–32 | Parity with the original (continuously validated) |
| 33–128 | Expected common use (tested in the performance CI) |
| 129–1024 | Supported, with a stress test per milestone |

## 4. Configuration files

| File | Role | Compatible with the original |
|------|------|------------------------------|
| `cstrike/server.cfg` | Run when each map starts | ✅ |
| `cstrike/listenserver.cfg` | For the listen server | ✅ |
| `cstrike/mapcycle.txt` | Map rotation | ✅ |
| `cstrike/motd.txt` | Message of the day (text or safe HTML) | ✅ (restricted HTML, [12](12-ui-hud-and-console.md)) |
| `cstrike/listip.cfg` / `banned.cfg` | Bans by IP and by identity | ✅ (identity replaces SteamID) |
| `cstrike/maps/<map>.cfg` | Per-map config | ✅ (common plugin behavior) |
| `server/plugins.toml` | Enabled plugins and mods and their order ([13](13-mod-system.md)) | new (replaces `plugins.ini`) |
| `server/admins.toml` | Administrators and permissions | new (replaces `users.ini`) |
| `server/masterServers.toml` | Announced master servers | new |
| `server/server.toml` | Optional structured configuration (equivalent to the cvars) | new |

Errors in any file (unknown cvar, out-of-range value, invalid TOML key) show up in the
console with the file and line. There is no silent failure.

## 5. A2S queries and server browser

- Replies compatible with Valve's query protocol, so that GameTracker, Discord bots and
  third-party browsers keep working:
  - `A2S_INFO`: name, map, game, players, maximum, bots, type, OS, password, VAC (always
    false) and version;
  - `A2S_PLAYER`: name, frags and connected time;
  - `A2S_RULES`: cvars marked `Notify`;
  - a mandatory **challenge** on all of them, as Valve started to require, against
    amplification.
- With more than 255 players, the 1-byte A2S fields saturate at 255 and the real value
  goes in a `players_real` rule and in the in-house protocol. This is documented as a
  limitation of the legacy format.
- **In-house master server**, open source (`nextmaster`), with an authenticated heartbeat
  protocol. Servers announce themselves to every master in `masterServers.toml`.
- **LAN:** broadcast discovery on the default port (`sv_lan 1` restricts to the LAN).

### 5.1 Content required by the server

- The server declares which content sources it depends on ([ADR-0015](adr/0015-free-content-and-optional-original-assets.md))
  in its `content.toml` (`requires = ["cstrikeOriginal"]` with the `classic` profile).
- The requirement goes in the A2S rule `content_requires` and in the handshake. The server
  browser shows "requires original CS 1.6" and whether the player already has the source
  configured.
- Files from those sources are **never** sent by the server. The client must have them
  locally, with the same hash. If it does not, it receives a clear rejection
  (`RejectReason::MissingLocalContent`), with the list of what is missing and how to
  configure it.
- A server with the `open` profile requires nothing beyond what it can send itself.

## 6. Cvars

### 6.1 Compatibility

Every relevant CS 1.6 server cvar exists with the **same name, the same default and the
same effect**. Initial list (the full catalog lives in `data/cstrike/cvars.toml`):

| Group | Cvars |
|-------|-------|
| Round and match | `mp_roundtime`, `mp_freezetime`, `mp_buytime`, `mp_c4timer`, `mp_timelimit`, `mp_maxrounds`, `mp_winlimit`, `mp_fraglimit`, `sv_restart`, `sv_restartround`, `mp_chattime` |
| Economy | `mp_startmoney` |
| Teams | `mp_autoteambalance`, `mp_limitteams`, `mp_autokick`, `mp_tkpunish`, `mp_friendlyfire`, `mp_hostagepenalty` |
| Spectator | `mp_forcecamera`, `mp_forcechasecam`, `mp_fadetoblack`, `allow_spectators` |
| Communication | `sv_alltalk`, `sv_voiceenable`, `mp_flashlight`, `mp_footsteps`, `decalfrequency`, `mp_logmessages` |
| Physics | `sv_gravity`, `sv_friction`, `sv_accelerate`, `sv_airaccelerate`, `sv_stopspeed`, `sv_maxspeed`, `sv_stepsize`, `edgefriction`, `sv_bounce`, `sv_maxvelocity` |
| Network | `sv_maxrate`, `sv_minrate`, `sv_maxupdaterate`, `sv_minupdaterate`, `sv_unlag`, `sv_maxunlag`, `sv_unlagpush`, `sv_timeout` |
| Server | `hostname`, `sv_password`, `rcon_password`, `sv_lan`, `sv_region`, `sv_contact`, `sv_cheats`, `pausable`, `sv_allowdownload`, `sv_allowupload`, `sv_downloadurl`, `mp_consistency` |
| Logs | `log on/off`, `sv_logfile`, `sv_logecho`, `mp_logdetail` |

### 6.2 New cvars (own prefix when there is no equivalent)

| Cvar | Default | Role |
|------|---------|------|
| `sv_tickrate` | 100 | Simulation frequency |
| `sv_relevance_distance` | 0 (off) | Maximum relevance distance |
| `sv_download_rate` | conservative | Per-client built-in download bandwidth |
| `rook_budget_ms` | see [13](13-mod-system.md) | CPU budget per plugin callback |
| `rcon_secure_port` | 0 (off) | Secure RCON port (TCP + TLS) |
| `metrics_port` | 0 (off) | Metrics endpoint (Prometheus) |

### 6.3 Cvar typing

Cvars are **typed** (`bool`, ranged integer, ranged float, string, enum), but they accept
the classic textual syntax in the console. An invalid value is rejected with a clear message
(`mp_freezetime: expected a whole number in [0, 60], got "abc"`). There is no silent
conversion to 0.

## 7. Player identity

The original used SteamID. Without Steam, each player has a **cryptographic identity**:

- An Ed25519 key pair generated on the first run and stored in the user profile.
- `PlayerId` = the fingerprint of the public key, displayed as
  `NX:XXXX-XXXX-XXXX-XXXX`.
- The handshake proves ownership of the key ([08 §4.1](08-networking.md#41-handshake)), so
  nobody can impersonate another player.
- Bans, admins and statistics use `PlayerId`. IP bans remain available.
- Known limitation: a player can generate another identity (as on *non-steam* servers).
  Mitigations: IP bans, per-server reputation and, in the future, optional identity
  providers.

## 8. Server console commands

| Command | Role |
|---------|------|
| `status` | Lists players (slot, name, `PlayerId`, ping, loss, IP) |
| `users`, `listid`, `listip` | User and ban lists |
| `kick <#userid\|name>`, `banid <minutes> <id>`, `addip <minutes> <ip>`, `removeid`, `removeip`, `writeid`, `writeip` | Moderation (original syntax) |
| `changelevel <map>`, `map <map>`, `restart` | Maps |
| `say <text>` | Server message |
| `exec <cfg>`, `cvarlist`, `cmdlist`, `find <text>` | Configuration |
| `stats` | CPU, tick, bandwidth, entities |
| `quit` / `exit` | Shut down |
| `plugins list\|load\|unload\|reload\|info` | Rook plugins ([13](13-mod-system.md)) |
| `mods list\|info` | Installed mods |

## 9. RCON

| Mode | Use | Default |
|------|-----|---------|
| `rcon` in the game client | Over the encrypted game session, with `rcon_password` | Enabled if `rcon_password` is set |
| Secure TCP + TLS RCON | External tools and hosting panels | Off (`rcon_secure_port`) |
| Legacy plain-text UDP RCON | Old tools | **Removed** for security. Legacy tools use an adapter |

Incorrect password attempts face a progressive per-IP lockout (`rcon_ban_after`,
`rcon_ban_minutes`), recorded in the log.

## 10. Logs

- A log format compatible with HLDS for game events (`L 10/20/2026 - 21:03:11:
  "name<2><NX:...><CT>" killed ...`), so that statistics parsers (e.g. HLstatsX) work with
  little or no adaptation.
- An additional structured log in JSON lines (`log_format json`) for modern observability.

## 11. Operation

- **Graceful shutdown:** `SIGTERM`/`quit` warns the players, saves the plugins' persistent
  state and closes the sockets.
- **Map change without reconnecting** the clients (as in the original), with incremental
  download of the new resources.
- **Plugin hot reload** without a map change (`plugins reload <id>`).
- **Metrics:** tick p50/p99, players, bandwidth, memory per plugin, job queues.
- **Docker:** an official Linux `nextds` image. With the `classic` profile, the original
  installation is mounted as a read-only volume.
- **Automatic updates** (planned, M4): the server product updates independently of the client.
  `nextds` applies a new release at start and, while running, switches to a downloaded one only
  when `sv_update_policy` allows (`empty` by default, `mapchange`, `notify`, `off`), never
  during a match; `sv_update_interval` sets how often it checks, and the `update` command
  applies a pending release ([21 §3.2](21-launcher-and-updates.md#32-server-automatic-never-during-a-match)).
  In the Docker image, the image is what updates.

## 12. Typed errors

| Enum | Examples |
|------|----------|
| `ServerError` | `PortInUse`, `MapNotFound`, `InvalidMaxPlayers{value, range}`, `ConfigInvalid` |
| `CvarError` | `InvalidName`, `NameTaken`, `InvalidDefinition`, `InvalidValue`, `OutOfRange` (the `CheatProtected` and `ServerProtected` protections belong to the console, [12 §9](12-ui-hud-and-console.md#9-typed-errors)) |
| `RconError` | `BadPassword`, `Locked{until}`, `Disabled` |
| `MasterError` | `Unreachable`, `Rejected`, `HeartbeatTimeout` |
