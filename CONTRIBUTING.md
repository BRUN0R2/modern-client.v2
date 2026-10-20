# Contributing to Modern-Client.v2

Thank you for helping. This guide covers the workflow; the rules themselves live in
[docs/RULES.md](docs/RULES.md), which binds every contribution, human or AI-assisted.

## Before you start

- Read [docs/RULES.md](docs/RULES.md), the
  [coding standards](docs/02-coding-standards.md) and the
  [definition of done](docs/18-testing-and-quality.md#definition-of-done).
- Pick an item from [TODO.md](TODO.md) or open an issue first for anything that is not there.
  A relevant structural decision needs an [ADR](docs/adr/README.md) before the code.
- Never send Valve assets or code derived from leaked or incompatible sources: the project is
  a clean room ([docs/20 §3](docs/20-legal-and-licensing.md#3-clean-room)).

## Building and testing

The toolchains, presets and dependencies are described in
[docs/17](docs/17-build-and-tools.md). In short:

```powershell
# Windows (Visual Studio 2026 with "Desktop development with C++")
. .\scripts\devShell.ps1
cmake --preset dev-windows-msvc
cmake --build --preset dev-windows-msvc
ctest --preset dev-windows-msvc
```

```bash
# Linux (GCC 16, CMake 4.3+, Ninja, vcpkg with VCPKG_ROOT set)
cmake --preset dev-linux-gcc
cmake --build --preset dev-linux-gcc
ctest --preset dev-linux-gcc
```

Concurrent code also runs under ThreadSanitizer (`tsan-linux-gcc`).

## Local checks

The CI's `checks` job rejects a commit that fails any of these, so run them before pushing:

```bash
python -m pip install "reuse[charset-normalizer]>=6,<7" "clang-format>=22,<23"
reuse lint
git ls-files -z -- '*.cpp' '*.cppm' '*.hpp' | xargs -0 clang-format --dry-run --Werror
git ls-files --eol | grep -Ev '^i/(lf|none|-text)[[:space:]]'   # must print nothing
```

New files need an SPDX header (or a `REUSE.toml` annotation) and LF line endings
([docs/02 §2](docs/02-coding-standards.md#2-file-and-module-organization)).

## Commits

- One commit per finished logical change, with its tests, documentation and `TODO.md`
  updated in the same commit.
- Conventional Commits in English with a short imperative summary:
  `feat(console): add cvarlist, cmdlist and find`, `fix(ecs): ...`, `docs: ...`, `ci: ...`.
- Sign off every commit (`git commit -s`). The `Signed-off-by` line is your statement, under
  the [Developer Certificate of Origin](https://developercertificate.org), that you have the
  right to submit the change under the project's licenses.

## Pull requests

- Open the pull request against `main`; it merges only with every required CI job green.
- Describe what changed and why, and link the `TODO.md` item or the issue.
- Reviews use the checklist of [docs/02 §13](docs/02-coding-standards.md#13-review-checklist).
- Keep the pull request to one logical change; split unrelated work.

## Licensing of contributions

Contributions are licensed like the files they touch
([ADR-0010](docs/adr/0010-license.md), [`REUSE.toml`](REUSE.toml)):

| Part | License |
|------|---------|
| Engine, game, server, tools, data and build | GPL-3.0-or-later with the Rook Plugin Exception |
| Rook toolchain and plugin SDK (`rook/`) | MIT |
| Documentation (`docs/`, `README.md`, `TODO.md`, this file) | CC BY 4.0 |

## Reporting bugs and vulnerabilities

- Bugs: open an issue with the steps to reproduce, the expected and the actual behavior, and
  the platform and preset. Gameplay bugs include a replay once replays exist.
- Vulnerabilities: never in a public issue; follow [SECURITY.md](SECURITY.md).
