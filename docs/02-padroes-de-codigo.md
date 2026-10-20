# 02 — Padrões de código (C++26)

Este documento é **normativo** para todo código C++ do repositório. Revisões de código
usam a [lista de verificação](#13-lista-de-verificação-de-revisão) do final.

## 1. Versão da linguagem e recursos

- Padrão: **C++26** (`CMAKE_CXX_STANDARD 26`), sem extensões de compilador
  (`CMAKE_CXX_EXTENSIONS OFF`).
- Código próprio em **módulos nomeados** (`export module nx.ecs;`), com `import std;`.
- **Sem exceções** e **sem RTTI** nos módulos da engine e do jogo (ver
  [§6](#6-erros-tipados) e [ADR-0005](adr/0005-erros-tipados-sem-excecoes.md)).

### 1.1 Recursos adotados

| Recurso | Proposta | Uso no projeto | Sem suporte no compilador |
|---------|----------|----------------|---------------------------|
| Reflexão estática | P2996 | Metadados de componentes, serialização, esquemas TOML, replicação, bindings Rook, `enum` ↔ string | Nomes de tipo e enum: `nx.core:introspection` (por assinatura do compilador). Membros e anotações: registro explícito em `nx.meta` ([§1.2](#12-política-de-toolchain-e-fallbacks)) |
| Anotações para reflexão | P3394 | `[[=nx::net::Replicated{}]]`, `[[=nx::script::Expose{}]]`, `[[=nx::data::Range{...}]]` | Mesmo fallback |
| Expansion statements (`template for`) | P1306 | Iterar membros refletidos | Mesmo fallback |
| Contratos | P2900 | `pre`, `post`, `contract_assert` para invariantes | Funções `nx::expects`, `nx::ensures` e `nx::invariant`, com o mesmo handler ([§6.5](#65-contratos)) |
| `std::execution` | P2300 | Integração do Job System com senders/receivers | Implementação de referência `stdexec` |
| `std::inplace_vector` | P0843 | Coleções pequenas de capacidade fixa sem heap | Implementação própria em `nx.core` |
| `#embed` | P1967 | Embutir shaders e assets mínimos de fallback do binário | Geração de arrays no build |
| `= delete("motivo")` | P2573 | Proibir conversões perigosas com mensagem clara | `= delete` simples |
| Pack indexing (`Ts...[I]`) | P2662 | Metaprogramação de queries do ECS | `std::tuple_element` |
| Variável placeholder `_` | P2169 | Ignorar valores em structured bindings | Nome descritivo |
| Aritmética saturada (`std::add_sat` etc.) | P0543 | Dinheiro, contadores, acumuladores | **Sempre** as funções próprias de `nx.core` (`addSaturated`…), com os mesmos resultados: o GCC 16 anuncia o recurso em `<version>`, mas seu módulo `std` não o exporta |
| `std::function_ref` | P0792 | Callbacks sem alocação | Implementação própria |
| `<debugging>` | P2546 | `breakpoint_if_debugging()` no handler de contratos | Intrínsecos por plataforma |
| `std::optional<T&>` | P2988 | Referências opcionais sem ponteiros crus | `nx::OptionalRef<T>` |

Recursos de C++20/23 usados livremente: módulos, concepts, ranges, `std::expected`,
`std::print`/`std::format`, `std::span`, `std::mdspan`, `std::flat_map`,
*deducing this*, `std::move_only_function`, `consteval`, `constinit`.

### 1.2 Política de toolchain e fallbacks

- A matriz exata de suporte (GCC, Clang, MSVC) é mantida em
  [17-build-e-ferramentas.md](17-build-e-ferramentas.md#2-toolchains) e revisada a cada
  release de compilador.
- Todo recurso da tabela acima que ainda não tenha suporte universal fica **atrás de uma
  fachada** em `nx.core`/`nx.meta`. O resto do código chama a fachada, nunca o recurso
  direto, e assim troca-se a implementação sem mexer nos chamadores.
- Detecção por *feature-test macros* (`__cpp_impl_reflection`, `__cpp_contracts` etc.),
  nunca pela versão do compilador.
- O CI compila os dois caminhos (nativo e fallback) e um teste garante que os metadados
  gerados são **idênticos**.

## 2. Organização de arquivos e módulos

```
engine/ecs/
├── CMakeLists.txt
├── src/
│   ├── nx.ecs.cppm           # interface primária: export import :world; export import :query; ...
│   ├── world.cppm            # partição  nx.ecs:world
│   ├── world.cpp             # implementação (module nx.ecs;)
│   ├── archetype.cppm        # partição  nx.ecs:archetype
│   └── ...
└── tests/
    └── worldTests.cpp
```

- Um módulo por biblioteca. Partições (`:world`, `:query`) organizam o interior.
- Interfaces de módulo: `.cppm`. Unidades de implementação: `.cpp`.
- Arquivos e diretórios em `camelCase` (`jobSystem.cppm`, `worldTests.cpp`, `rhiVulkan/`).
  A interface primária leva o nome do módulo (`nx.ecs.cppm`). Nomes de módulo
  `nx.<área>[.<subárea>]`.
- Todo arquivo de código (C++, CMake, scripts) começa com o **cabeçalho SPDX** da licença do
  seu diretório ([ADR-0010](adr/0010-licenca.md), `REUSE.toml`). `MIT` em `rook/`,
  `GPL-3.0-or-later` no resto:

```cpp
// SPDX-FileCopyrightText: 2026 Modern-Client.v2 contributors
// SPDX-License-Identifier: GPL-3.0-or-later WITH LicenseRef-Rook-Plugin-Exception
```

- **Headers de terceiros** apenas no *global module fragment*:

```cpp
module;
#include <vulkan/vulkan.h>   // third-party headers only here
#include <vk_mem_alloc.h>
export module nx.rhi.vulkan;

import std;
import nx.core;
import nx.rhi;
```

- Macros de terceiros nunca vazam para interfaces exportadas. Tipos de terceiros não
  aparecem em assinaturas exportadas fora do próprio backend.

## 3. Nomenclatura

| Elemento | Estilo | Exemplo |
|----------|--------|---------|
| Namespace | minúsculas (`camelCase` se tiver mais de uma palavra) | `nx::ecs`, `cs::weapons` |
| Módulo | `nx.area.sub` | `nx.rhi.vulkan` |
| Tipo, concept, enum | `PascalCase` | `CommandBuffer`, `ComponentType`, `BspError` |
| Valor de enum | `PascalCase` | `BspError::LumpOutOfBounds` |
| Função, método, variável | `camelCase` | `parseBsp`, `entityCount` |
| Membro privado | `m_camelCase` | `m_chunks` |
| Constante (`constexpr`/`constinit`) | `kPascalCase` | `kDefaultTickRate` |
| Parâmetro de template | `PascalCase` | `template <class Component>` |
| Macro (evitar) | `NX_SCREAMING_CASE` | `NX_PLATFORM_WINDOWS` |
| Arquivo e diretório | `camelCase` | `jobSystem.cppm`, `contractsTests.cpp`, `rhiVulkan/` |

### 3.1 Regra do projeto: camelCase

**Todo o projeto usa camelCase** para funções, métodos, variáveis, parâmetros e campos, tanto
no C++ quanto na Rook. Isso inclui a API de plugins (`getWeaponVar`, `setEntityVar`,
`precacheModel`, `pluginPrecache`) e as chaves dos catálogos `vars` (`"maxClip"`,
`"viewModel"`). O `snake_case` dos natives do AMXX (`get_weapon_var`) não é usado em código
novo ([ADR-0013](adr/0013-convencao-camelcase.md)).

| Elemento | C++ | Rook |
|----------|-----|------|
| Funções, métodos, variáveis, parâmetros | `camelCase` (`parseBsp`) | `camelCase` (`setWeaponVar`) |
| Campos e chaves de dados | `camelCase` (`clipSize`) | `camelCase` (`maxClip`) |
| Membros privados | `m_camelCase` (`m_chunks`) | `camelCase` |
| Tipos, concepts, enums e valores de enum | `PascalCase` | `PascalCase` |
| Constantes | `kPascalCase` | `SCREAMING_SNAKE_CASE` |
| Namespaces e módulos | minúsculas (`nx::ecs`, `nx.core`) | minúsculas (`rezombie.api.classes`) |
| Arquivos e diretórios | `camelCase` (`contractsTests.cpp`, `enumArray.cppm`, `rhiVulkan/`) | `camelCase` (`infectionTests.rook`) |
| Alvos de build, executáveis, funções CMake e scripts | `camelCase` (`nxCoreTests`, `rookLsp`, `nxAddTest`, `devShell.ps1`) | — |

Exceções, porque o nome vem de fora do código ou é exigido por uma ferramenta:

- **Cvars e comandos de console** mantêm os nomes clássicos (`mp_roundtime`, `sv_gravity`,
  `rz_infection_delay`, `say /menu`). Eles fazem parte da compatibilidade com servidores,
  configs e binds existentes.
- **Classnames de entidades de mapa** vêm do BSP (`func_buyzone`, `info_player_start`).
- **Nomes exigidos por ferramentas:** `CMakeLists.txt`, `CMakePresets.json`, `vcpkg.json`,
  `.clang-format`, `.clang-tidy`, `.editorconfig`, os triplets do vcpkg
  (`x64-windows-nx.cmake`: o arquivo precisa ter o nome do triplet, que só aceita minúsculas
  e hífens) e as variáveis de configuração do CMake (`NX_BUILD_TESTS`, em maiúsculas como as
  do próprio CMake).
- **Arquivos convencionais de repositório:** `README.md`, `TODO.md`, `LICENSE`,
  `CONTRIBUTING.md`, `SECURITY.md`, `THIRD_PARTY_NOTICES.md`.
- **Documentação em `docs/`:** prefixo numérico e kebab-case (`02-padroes-de-codigo.md`),
  para manter a ordem de leitura.
- **Arquivos do jogo original:** `server.cfg`, `listenserver.cfg`, `mapcycle.txt`,
  `motd.txt`, `liblist.gam`.
- **Tags de idioma** em arquivos de tradução (`pt-BR.toml`).
- **Chaves de tradução** (`RZ_INFECTED_BY`).
- **Nomes impostos por terceiros** (biblioteca padrão, Vulkan, formatos de arquivo).

A regra é verificada pelo `clang-tidy` (`readability-identifier-naming`, como erro) no C++ e
pelo lint `naming` do `rookc` na Rook.

### 3.2 Outras regras de nomes

- Nada de abreviações obscuras (`ent`, `pl`, `wpn`). Exceções consagradas: `id`, `rhi`,
  `bsp`, `fps`, `rtt`, `pvs`.
- Booleanos com prefixo semântico: `isAlive`, `hasDefuser`, `canBuy`.
- Funções que podem falhar descrevem a ação (`loadModel`). Funções que buscam algo
  opcional usam `find*` e retornam `std::optional`. Funções que exigem existência usam
  `require*` e retornam `Result`.

## 4. Tipos

### 4.1 Tipos fortes e unidades

Valores com significado de domínio **NÃO DEVEM** circular como `int`/`float` crus.

```cpp
export namespace nx {

template <class Rep, class Tag>
class Quantity {
public:
    constexpr explicit Quantity(Rep value) noexcept : m_value{value} {}
    [[nodiscard]] constexpr auto value() const noexcept -> Rep { return m_value; }
    constexpr auto operator<=>(const Quantity&) const = default;

    friend constexpr auto operator+(Quantity a, Quantity b) noexcept -> Quantity {
        return Quantity{a.m_value + b.m_value};
    }
    friend constexpr auto operator*(Quantity a, Rep scale) noexcept -> Quantity {
        return Quantity{a.m_value * scale};
    }

private:
    Rep m_value;
};

using HammerUnits     = Quantity<float, struct HammerUnitsTag>;      // 1 unit ≈ 1 inch
using UnitsPerSecond  = Quantity<float, struct UnitsPerSecondTag>;
using HitPoints       = Quantity<float, struct HitPointsTag>;     // float, as in the original: fractional damage accumulates
using ArmorPoints     = Quantity<float, struct ArmorPointsTag>;
using Money           = Quantity<std::int32_t, struct MoneyTag>;

namespace literals {
consteval auto operator""_hu(long double v)  -> HammerUnits    { return HammerUnits{static_cast<float>(v)}; }
consteval auto operator""_ups(long double v) -> UnitsPerSecond { return UnitsPerSecond{static_cast<float>(v)}; }
} // namespace literals

} // namespace nx
```

- Tempo de simulação: `nx::GameTime` (ticks + fração) e `nx::Seconds`/`nx::Milliseconds`
  (`std::chrono::duration<float>`). Relógio de parede só via `nx.platform`.
- Ângulos: `nx::Degrees` e `nx::Radians`, sem conversão implícita entre eles.
- IDs e handles: tipos dedicados (`Entity`, `AssetHandle<Model>`, `ClientSlot`),
  **nunca** índices crus.

### 4.2 Enums

- Sempre `enum class` com tipo subjacente explícito (`: std::uint8_t`).
- Conversão `enum ↔ string` vem da reflexão (`nx::enumName(value)`), nunca de tabelas
  escritas à mão.
- Enums usados como índice de array têm sentinela `Count` e `nx::EnumArray<E, T>`.

```cpp
enum class HitGroup : std::uint8_t { Generic, Head, Chest, Stomach, LeftArm, RightArm, LeftLeg, RightLeg, Count };

nx::EnumArray<HitGroup, float> damageMultiplier; // filled from data/cstrike/hitGroups.toml
```

### 4.3 Conversões

- Conversões de estreitamento são explícitas: `nx::narrow<T>(v)` (verificada por contrato)
  ou `nx::truncate<T>(v)` (perda intencional e documentada).
- Construtores de um argumento são `explicit`.
- Conversões perigosas são apagadas com motivo:

```cpp
class Entity {
public:
    Entity(std::int32_t rawIndex) = delete("entities are handles issued by World; never build one from an index");
};
```

## 5. Sem números mágicos

Regra: **todo literal numérico com significado de domínio tem nome.** São permitidos sem
nome apenas `0` e `1` em contextos de identidade, contagem ou indexação óbvia.

| ❌ Proibido | ✅ Correto |
|------------|-----------|
| `if (player.health < 25)` | `if (player.health < kLowHealthWarning)` |
| `velocity *= 0.8f;` | `velocity *= movement.duckSpeedMultiplier;` (valor de dado) |
| `bits.write(value, 13);` | `bits.write(value, kOriginBits);` |
| `std::array<Slot, 32> slots;` | `std::array<Slot, kMaxWeaponSlots> slots;` |
| `sleep(16)` | `sleep(kFrameBudget)` com `constexpr auto kFrameBudget = 16ms;` |

Hierarquia de onde um valor deve morar:

1. **Dado de jogo** (TOML em `data/`): qualquer coisa que um designer de paridade ou um
   mod possa querer ajustar (preço de arma, dano, velocidade, tempo de round).
2. **Cvar**: o que o administrador do servidor ou o jogador configura
   (`mp_roundtime`, `sv_gravity`).
3. **Constante nomeada** (`constexpr`) no módulo dono: invariantes técnicas (versão de
   formato, tamanho de lump, bits de quantização).

O `clang-tidy` roda com `readability-magic-numbers` e
`cppcoreguidelines-avoid-magic-numbers` como **erro**. Em testes, literais são permitidos
como dados de entrada e valores esperados ([18](18-testes-e-qualidade.md#convenções-dos-testes-c)).

## 6. Erros tipados

### 6.1 Classificação

| Tipo de falha | Exemplo | Mecanismo |
|---------------|---------|-----------|
| **Esperada** (entrada externa, E/S, rede, dado de mod) | arquivo BSP corrompido, pacote malformado | `nx::Result<T, E>` (`std::expected`) |
| **Violação de contrato** (bug de programação) | índice fora do chunk, handle de outro mundo | `pre`/`post`/`contract_assert` → handler de contrato |
| **Irrecuperável** (sem memória, device perdido sem recuperação) | `VK_ERROR_DEVICE_LOST` repetido | `nx::fatal(...)`: crash report e término controlado |

### 6.2 Tipos de erro

Cada módulo declara seus próprios códigos de erro como `enum class`. Não existe código
de erro genérico ("Unknown", "Failed") nem `bool` para indicar falha.

```cpp
export namespace nx {

template <class Code>
    requires std::is_scoped_enum_v<Code>
struct Failure {
    Code                 code;
    std::string          detail;   // human-readable context (path, value, limit)
    std::source_location where;
    Cause                cause;    // optional type-erased chain; only allocated on error paths
};

template <class T, class Code>
using Result = std::expected<T, Failure<Code>>;

template <class Code>
[[nodiscard]] auto fail(Code code, std::string detail = {},
                        std::source_location where = std::source_location::current())
    -> std::unexpected<Failure<Code>>;

template <class Code, class InnerCode>
[[nodiscard]] auto fail(Code code, Failure<InnerCode> inner,
                        std::source_location where = std::source_location::current())
    -> std::unexpected<Failure<Code>>;   // chains `inner` as the cause

} // namespace nx
```

### 6.3 Exemplo

```cpp
export module nx.assets.goldsrc:bsp;

import std;
import nx.core;

export namespace nx::assets::goldsrc {

inline constexpr std::int32_t kBspVersion = 30;

enum class BspError : std::uint8_t {
    TooSmall,
    UnsupportedVersion,
    LumpOutOfBounds,
    MisalignedLump,
};

[[nodiscard]] auto parseBsp(std::span<const std::byte> bytes) -> nx::Result<BspView, BspError>;

} // namespace nx::assets::goldsrc
```

```cpp
module nx.assets.goldsrc;

namespace nx::assets::goldsrc {

auto parseBsp(std::span<const std::byte> bytes) -> nx::Result<BspView, BspError> {
    if (bytes.size() < sizeof(BspHeader)) {
        return nx::fail(BspError::TooSmall, std::format("{} bytes", bytes.size()));
    }

    const auto header = readHeader(bytes);
    if (header.version != kBspVersion) {
        return nx::fail(BspError::UnsupportedVersion, std::format("version {}", header.version));
    }

    return validateLumps(header, bytes).transform([&] { return BspView{header, bytes}; });
}

} // namespace nx::assets::goldsrc
```

Ao atravessar camadas, o erro é **traduzido** para o vocabulário da camada de cima,
preservando o original como causa:

```cpp
auto MapLoader::load(MapName name) -> nx::Result<Map, MapLoadError> {
    auto bytes = m_vfs.readAll(name.bspPath());
    if (!bytes) {
        return nx::fail(MapLoadError::NotFound, std::move(bytes.error()));
    }
    auto bsp = goldsrc::parseBsp(*bytes);
    if (!bsp) {
        return nx::fail(MapLoadError::CorruptBsp, std::move(bsp.error()));
    }
    return buildMap(*bsp);
}
```

### 6.4 Regras

- Toda função que pode falhar retorna `nx::Result` e é `[[nodiscard]]`. O tipo `Result`
  é `[[nodiscard]]` por definição.
- **Proibido** ignorar um `Result`. Para descartar intencionalmente:
  `std::ignore = ...;` com comentário do motivo.
- **Proibido** fallback silencioso: se um recurso falta, o erro sobe ou vira um evento de
  diagnóstico visível. Nunca troque silenciosamente por outro recurso.
- Preferir as operações monádicas (`and_then`, `transform`, `or_else`) ou o retorno
  antecipado explícito. Não usar macros de propagação.
- Mensagens de `detail` em inglês, com o valor problemático e o limite esperado.
- Erros expostos para mods são mapeados para `error enum` Rook gerados por reflexão
  ([15](15-api-de-mods.md#9-catálogo-de-erros)).

### 6.5 Contratos

```cpp
auto Chunk::row(RowIndex row) const -> RowView
    pre(row.value() < m_count)
{
    return RowView{m_columns, row};
}

auto JobSystem::submit(JobDesc desc) -> JobHandle
    pre(desc.function != nullptr)
    post(handle : handle.isValid());
```

- Em builds de desenvolvimento, uma violação chama o handler de contrato do projeto:
  log com *stack trace*, `breakpoint_if_debugging()` e crash report.
- Em release, a semântica padrão é `enforce` nos módulos de rede, assets e script (as
  fronteiras com dados externos) e `observe` nos caminhos quentes medidos por benchmark.
  A escolha é por módulo, documentada no `CMakeLists.txt`.

## 7. Memória e posse

- RAII sempre. **Proibido** `new`/`delete` crus fora de alocadores.
- Posse única: `std::unique_ptr`. `std::shared_ptr` só com justificativa escrita
  (posse realmente compartilhada entre threads de vida independente).
- Parâmetros não-donos: `T&`, `const T&`, `std::span`, `std::string_view`,
  `std::function_ref`. Ponteiro cru apenas para "talvez nulo e não-dono", e mesmo assim
  preferir `std::optional<T&>`.
- Alocadores especializados (em `nx.core`):
  - `FrameArena`: linear por frame, zerada no fim do frame. Para dados temporários de jobs.
  - `PoolAllocator<T>`: blocos de tamanho fixo (chunks do ECS, nós de grafo).
  - `TrackedHeapAllocator`: contabiliza memória por categoria (assets, scripts, rede).
- Contêineres da engine recebem o alocador explicitamente quando a memória é de fase
  curta. Não usar `std::pmr` em caminhos quentes sem medição.

## 8. Concorrência

- Todo dado compartilhado entre threads tem **um dono documentado** e um protocolo de
  acesso: fase do ECS, fila sem lock, atômico ou mutex.
- `std::mutex` é permitido apenas fora de caminhos quentes. Nunca segure um lock ao
  chamar código de outro módulo ou callback de mod.
- Atômicos com `memory_order` explícito e um comentário justificando a ordem quando ela
  não for `seq_cst`.
- Nada de `thread_local` com estado que sobreviva ao job (os jobs migram entre threads).
- `std::this_thread::sleep_for` é proibido em jobs. Esperas usam contadores do Job System.
- Todo código concorrente novo vem com teste sob **ThreadSanitizer**.

## 9. Funções e classes

- Funções pequenas, com uma responsabilidade. Mais de cerca de 60 linhas exige
  justificativa na revisão.
- Assinaturas com retorno à direita (`auto f() -> T`) em APIs exportadas.
- `[[nodiscard]]` em toda função cujo retorno importa. `noexcept` em tudo que não aloca
  nem falha.
- `constexpr`/`consteval` sempre que possível (parsers de formato, tabelas, hashing).
- Parâmetros booleanos posicionais são proibidos em APIs públicas. Use `enum class`
  ou struct de opções com inicialização designada:

```cpp
// ❌ trace(start, end, true, false);
// ✅
auto hit = collision.trace(TraceQuery{
    .start  = eyePosition,
    .end    = eyePosition + forward * weapon.range,
    .hull   = Hull::Point,
    .filter = TraceFilter::ignoring(shooter),
});
```

- Herança apenas para interfaces. Composição e ECS para comportamento.
- Classes com invariantes têm membros privados. Dados simples (componentes, descritores)
  são structs agregadas.

## 10. Log

```cpp
inline constexpr nx::log::Category kAssetsLog{"assets"};
inline constexpr nx::log::Category kNetLog{"net"};

nx::log::info(kAssetsLog, "loaded map {} in {}", map.name(), elapsed);
nx::log::warning(kNetLog, "client {} exceeded snapshot budget by {} bytes", slot, overflow);
```

- Formatação verificada em compilação (`std::format_string`). Mensagens de um nível
  desligado não são formatadas.
- Cada registro leva nível (`Trace`, `Debug`, `Info`, `Warning`, `Error`), categoria, local
  da chamada e horário (UTC).
- Categorias são constantes do **módulo que registra**. O `nx.core` não conhece os
  subsistemas. O nível por categoria é ajustável (`Logger::setCategoryLevel`; por cvar,
  ex.: `log_level_net debug`, quando o console existir).
- Destinos são `nx::log::Sink` (console hoje; arquivo e JSON lines a seguir).
- Logs em caminhos quentes passam por `nx::log::RateLimit`, que é *thread-safe*:

```cpp
static nx::log::RateLimit budgetWarnings{std::chrono::seconds{1}};
if (budgetWarnings.allow()) {
    nx::log::warning(kNetLog, "snapshot budget exceeded ({} suppressed)", budgetWarnings.takeSuppressed());
}
```

## 11. Comentários e documentação

- Comentários de código em **inglês**, explicando o *porquê*, não o *o quê*.
- APIs exportadas têm comentário de documentação `///` com pré-condições e erros possíveis.
- Comportamento herdado do original que pareça estranho é documentado com a etiqueta
  `// Parity:` e um link para o teste de paridade que o cobre.

```cpp
// Parity: CS 1.6 scales horizontal speed after landing from a jump (stamina);
// removing this breaks bunny-hop feel. Covered by tests/parity/movement/bhopPenalty.
```

- `TODO` só com referência ao item do [TODO.md](../TODO.md) ou a uma issue.

## 12. Formatação e análise estática

- `clang-format` com o `.clang-format` do repositório (base LLVM, 4 espaços, coluna 110).
- `clang-tidy` com o perfil do repositório. Grupos habilitados: `bugprone-*`,
  `cppcoreguidelines-*`, `modernize-*`, `performance-*`, `readability-*`, `concurrency-*`.
  Exceções ficam listadas no `.clang-tidy` com justificativa.
- Warnings como erro (`-Werror` / `/WX`) em todos os alvos próprios.
- Sanitizers no CI: TSan (GCC, em todo push e PR), ASan + UBSan e MSan (Linux/Clang).

## 13. Lista de verificação de revisão

- [ ] Há números mágicos? Literais de domínio têm nome, cvar ou dado.
- [ ] Todo caminho de falha retorna um erro tipado com contexto? Algum `Result` é ignorado?
- [ ] Há fallback silencioso?
- [ ] Novas dependências entre módulos respeitam as camadas ([01](01-arquitetura.md))?
- [ ] Há estado global, singleton ou `thread_local` com estado?
- [ ] O acesso concorrente tem dono e protocolo documentados?
- [ ] O comportamento de paridade alterado tem teste de paridade?
- [ ] Há testes unitários para a lógica nova e benchmark para caminho quente novo?
- [ ] A API pública exportada tem documentação `///`?
- [ ] Algum tipo de terceiro vazou para a interface exportada?
