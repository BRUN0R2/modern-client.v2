# ADR-0007 — VM própria com bytecode tipado e verificado

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

Plugins Rook rodam no servidor e, quando são `client`/`shared`, também no cliente, com
código baixado do servidor. É preciso sandbox forte, orçamento de CPU e memória por
plugin, isolamento de falhas, hot reload com migração de estado, depuração e desempenho
suficiente para gameplay.

## Decisão

- **VM própria** com **bytecode de registradores tipado** (`.rbc`).
- **Verificador na carga** (tipos, registradores e saltos), o que torna seguro executar
  bytecode vindo da rede.
- **Isolate por plugin**, com GC preciso e incremental próprio, sem pausa global.
- **Combustível de instruções** para orçamento de CPU, limites de heap e de pilha, e
  falhas tipadas.
- Interpretador com *computed goto*. JIT opcional fica como trabalho futuro, sem mudar o
  formato.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| WebAssembly (Wasmtime/WAMR) | Sandbox excelente e JIT pronto, mas a integração com GC, handles da engine, strings, hot reload com migração tipada e depuração no nível do Rook fica mais complexa. Continua como possível backend futuro, porque a IR (RIR) é independente do alvo |
| Compilar Rook para C++/nativo | Sem sandbox e inviável no cliente |
| VMs existentes (Lua, Mono) | Não preservam os tipos do Rook e trazem modelo de memória e sandbox próprios |

## Consequências

- ✅ Sandbox, orçamentos, hot reload e depurador sob controle do projeto.
- ✅ Bytecode tipado elimina verificações dinâmicas de tipo.
- ⚠️ Desempenho de interpretador. Mitigação: tipos estáticos, otimizações na RIR,
  `pure fn` em jobs e JIT futuro se o profiler justificar.
- ⚠️ Superfície de segurança da VM no cliente. Mitigação: verificador, fuzzing contínuo e
  auditoria antes do M7.
