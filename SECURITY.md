# Security policy

## Supported versions

Modern-Client.v2 has no release yet. Security fixes land on `main`; once releases exist, this
table lists the ones that still receive fixes.

| Version | Supported |
|---------|-----------|
| `main` | Yes |

## Reporting a vulnerability

Report vulnerabilities **privately**, through GitHub's private vulnerability reporting:
the **Security** tab of the repository, then **Report a vulnerability**. Never open a public
issue, pull request or discussion about an unfixed vulnerability.

Include:

- the affected component and version (commit hash);
- the impact, as you understand it;
- the steps or the input that reproduce it (a packet capture, a file, a plugin);
- any fix or mitigation you suggest.

## What is in scope

The project's attack surface is where external data enters (docs/RULES.md, docs/02 §6.5):

- the network protocol, the handshake and the session encryption
  ([docs/08](docs/08-networking.md));
- the dedicated server: RCON, A2S queries and the master server
  ([docs/09](docs/09-dedicated-server.md));
- parsers of untrusted files: maps, models, textures, sprites, sounds and configs
  ([docs/06](docs/06-assets-and-resources.md));
- the Rook VM sandbox, the bytecode verifier and the plugin capabilities
  ([docs/13](docs/13-mod-system.md));
- resource downloads and their hash checks.

## How reports are handled

1. The maintainer acknowledges the report and confirms whether it is a vulnerability.
2. A fix is prepared privately, with a regression test.
3. The fix is published together with a GitHub security advisory that credits the reporter,
   unless they prefer to stay anonymous.

Please give the maintainer reasonable time to publish a fix before any public disclosure.
