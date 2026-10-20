# ADR-0007 — Own VM with typed, verified bytecode

- **Status:** Accepted
- **Date:** 2026-10-20

## Context

Rook plugins run on the server and, when they are `client`/`shared`, also on the client,
with code downloaded from the server. This requires a strong sandbox, a CPU and memory
budget per plugin, failure isolation, hot reload with state migration, debugging and
performance good enough for gameplay.

## Decision

- **Own VM** with **typed register bytecode** (`.rbc`).
- **Verifier at load time** (types, registers and jumps), which makes it safe to run bytecode
  coming from the network.
- **One isolate per plugin**, with its own precise, incremental GC, without a global pause.
- **Instruction fuel** for the CPU budget, heap and stack limits, and typed failures.
- Interpreter with *computed goto*. An optional JIT remains future work, without changing
  the format.

## Alternatives considered

| Alternative | Why not |
|-------------|---------|
| WebAssembly (Wasmtime/WAMR) | Excellent sandbox and a ready JIT, but the integration with GC, engine handles, strings, hot reload with typed migration and Rook-level debugging becomes more complex. It remains a possible future backend, because the IR (RIR) is target-independent |
| Compile Rook to C++/native | No sandbox, and unfeasible on the client |
| Existing VMs (Lua, Mono) | They do not preserve Rook's types and bring their own memory model and sandbox |

## Consequences

- ✅ Sandbox, budgets, hot reload and debugger under the project's control.
- ✅ Typed bytecode eliminates dynamic type checks.
- ⚠️ Interpreter performance. Mitigation: static types, optimizations on the RIR, `pure fn`
  in jobs and a future JIT if the profiler justifies it.
- ⚠️ Security surface of the VM on the client. Mitigation: verifier, continuous fuzzing and
  an audit before M7.
