# Conformance suite

The executable reference of the language: [`grammar.ebnf`](../grammar.ebnf) says what Rook is, and
these sources prove the compiler agrees. `rookConformance`, the runner beside this directory, runs
them all as the CTest test `Rook.Conformance`, and needs nothing but the compiler, so the toolchain
built on its own runs them too.

| Directory | Holds | The rule |
|-----------|-------|----------|
| `accepts/` | Source that must parse | No diagnostic, and no comment expecting one |
| `refuses/` | Source that must not parse | At least one diagnostic, each expected by a comment |

For every case the runner also checks that the tree writes the source back byte for byte, and that
every diagnostic renders with its code, a snippet and a fix. For the whole suite, it checks that every
code of `kDiagnosticCodes` is reached by a case, and that the reserved words of the grammar are
the compiler's.

## Expecting a diagnostic

A comment says which diagnostic a line must have:

```rook
if count > 0 { //~ error[R0030]
```

Several codes may share a comment, `//~ error[R0031] error[R0032]`. A line that swallows comments,
such as one with an unterminated string, is expected from the line above it, with `//~v`:

```rook
//~v error[R0006]
const TEXT: string = "never closed
;
```

The line and the code must match exactly: a diagnostic nobody expected fails the case, and so does
one expected and not reported.

## Trees

A case with a `.tree` file beside it must parse into that tree, which is how precedence is proved:
`precedence.tree` shows what `a + b * c` groups into. A tree lists one node on each line with the tokens
directly under it, without whitespace and plain comments. To add one, create the empty file; to write it
again, after reading what changed:

```bash
rookConformance rook/spec/conformance rook/spec/grammar.ebnf --update-trees
```

## Adding a case

A new diagnostic gets a case in `refuses/`, named after what it refuses, and the runner fails until
it has one ([ADR-0033](../../../docs/adr/0033-rook-compiler-and-types.md)). `SourceTooLarge` and
`TooManyDiagnostics` are the codes no file here reaches, one needing a source of 16 MiB: the runner makes
those sources itself. Keep the bytes of
`refuses/invalidUtf8.rook`, `refuses/controlCharacter.rook` and `refuses/textDirectionCharacter.rook` as
they are: they hold what an editor would repair.
