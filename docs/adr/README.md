# Registro de decisões de arquitetura (ADR)

Cada decisão estrutural relevante é registrada aqui. Um ADR **aceito** prevalece sobre os
demais documentos. Para mudar uma decisão, cria-se um ADR novo que *substitui* o anterior,
sem apagar o histórico.

## Índice

| ADR | Título | Status |
|-----|--------|--------|
| [0001](0001-cpp26-e-modulos.md) | C++26 com módulos nomeados | Aceito |
| [0002](0002-ecs-por-arquetipos.md) | ECS próprio por arquétipos | Aceito |
| [0003](0003-job-system-work-stealing.md) | Job System com work-stealing, sem fibers na v1 | Aceito |
| [0004](0004-vulkan-primeiro-rhi-abstrata.md) | Vulkan primeiro, RHI compatível com DirectX 11, shaders em Slang | Aceito |
| [0005](0005-erros-tipados-sem-excecoes.md) | Erros tipados com `std::expected` e contratos, sem exceções | Aceito |
| [0006](0006-linguagem-de-plugins-propria-rook.md) | Linguagem de plugins própria (Rook) com compilador próprio | Aceito |
| [0007](0007-vm-propria-bytecode-tipado.md) | VM própria com bytecode tipado e verificado | Aceito |
| [0008](0008-protocolo-de-rede-proprio.md) | Protocolo de rede próprio, sem compatibilidade com o protocolo 48 | Aceito |
| [0009](0009-assets-originais-do-usuario.md) | Assets originais lidos da instalação do usuário | Aceito |
| [0010](0010-licenca.md) | Licença: GPL-3.0-or-later, MIT (Rook e SDK) e CC BY 4.0 (docs) | Aceito |
| [0011](0011-limite-de-jogadores-1024.md) | `maxplayers` de 1 a 1024 como limite de política | Aceito |
| [0012](0012-padrao-get-set-vars-tipado.md) | Padrão `get*Var`/`set*Var` com chaves verificadas em compilação | Aceito |
| [0013](0013-convencao-camelcase.md) | Convenção camelCase em todo o projeto (C++ e Rook) | Aceito |
| [0014](0014-modulo-std-compilado-pelo-projeto.md) | Módulo `std` compilado pelo projeto, sem o `import std` experimental do CMake | Aceito |

## Modelo

```markdown
# ADR-NNNN — Título

- **Status:** Proposto | Aceito | Substituído por ADR-XXXX | Rejeitado
- **Data:** AAAA-MM-DD

## Contexto
O problema, as forças e as restrições.

## Decisão
O que foi decidido, de forma direta.

## Alternativas consideradas
Cada alternativa e por que não foi escolhida.

## Consequências
O que fica mais fácil, o que fica mais difícil e os riscos e como mitigá-los.
```
