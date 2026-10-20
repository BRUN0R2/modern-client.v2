# ADR-0014 — Módulo `std` compilado pelo projeto, sem o `import std` experimental do CMake

- **Status:** Aceito
- **Data:** 2026-10-20

## Contexto

O código do projeto usa `import std;` ([ADR-0001](0001-cpp26-e-modulos.md)). A primeira
versão do build usava o suporte do próprio CMake, que trouxe três problemas:

1. É **experimental**: exige um *gate* `CMAKE_EXPERIMENTAL_CXX_IMPORT_STD` cujo UUID muda a
   cada versão do CMake. O projeto precisava manter uma tabela de UUIDs que quebrava a cada
   atualização.
2. No MSVC, o CMake 4.4 chama a biblioteca do módulo de `@cmake_cxx_std.lib`. O `link.exe` lê
   todo argumento iniciado por `@` como arquivo de resposta, então a biblioteca nunca era
   linkada. Isso exigia um contorno no layout de saída.
3. Fontes fora da árvore ganham diretórios de objeto com *hash* longo, que se aproximavam do
   limite de 260 caracteres do Windows.

## Decisão

- O projeto **não usa** o suporte experimental do CMake a `import std`.
- `cmake/nxStdModule.cmake` localiza o manifesto de módulos que a própria biblioteca padrão
  publica (`modules.json` da Microsoft STL, `libstdc++.modules.json`, `libc++.modules.json`),
  lê a fonte do módulo `std` e as opções exigidas, e compila o módulo como o alvo comum
  **`nx::std`** (`FILE_SET CXX_MODULES`), recurso estável do CMake.
- O fonte é **copiado** para `build/<preset>/nxStd/` (`configure_file`, recopiado quando o
  toolchain muda), o que mantém os caminhos de objeto curtos.
- Só `std` é compilado. `std.compat` fica de fora porque reexporta a biblioteca C no namespace
  global, e o código do projeto usa sempre `std::`.
- Todo alvo do projeto linka `nx::std`. As flags que mudam o ABI continuam globais, então o
  módulo é compilado exatamente como quem o importa.

## Alternativas consideradas

| Alternativa | Por que não |
|-------------|-------------|
| Manter o `import std` experimental com a tabela de UUIDs | Quebra a cada versão do CMake e depende de contorno para o bug do `@` |
| Voltar a `#include` da biblioteca padrão | Perde o principal ganho de módulos (tempo de build e isolamento) |
| Header units (`import <vector>;`) | Suporte inconsistente entre compiladores e no CMake |

## Consequências

- ✅ Nenhum recurso experimental no build. Qualquer CMake 4.3+ funciona (validado com 4.3.1 e
  4.4.4), sem tabela de UUIDs nem contorno de nome de biblioteca.
- ✅ Caminhos de objeto curtos.
- ⚠️ O formato do manifesto pode evoluir. Mitigação: chave desconhecida em `local-arguments`
  é erro de configuração explícito, não é ignorada.
- ⚠️ Os caminhos de GCC e Clang seguem o mesmo manifesto, mas só serão validados junto com os
  presets Linux.
- Quando o CMake estabilizar `import std`, a troca pode ser reavaliada em um ADR novo.
