# Documentação Central do Projeto (Docs)

> **Importância deste Diretório:**  
> A pasta `docs/` é o coração arquitetural e de negócios da plataforma. Enquanto a pasta `.agents/` instrui a inteligência artificial sobre *como agir e programar*, a pasta `docs/` explica o *que é o projeto, por que ele existe e como ele se estrutura*. 
>
> Este diretório deve ser mantido **perfeito e atualizado**. Ele é lido por:
> 1. **Humanos (Desenvolvedores/Stakeholders):** Para entenderem rapidamente o contexto do negócio, o problema resolvido e as funcionalidades sem ler código.
> 2. **Agentes de Inteligência Artificial:** Como contexto primordial para o planejamento de tarefas, refatorações, onboarding entre ciclos de desenvolvimento e garantia do alinhamento Bottom-Up (Spec-Driven Development).

---

## 📂 Estrutura de Arquivos

| Arquivo / Diretório | Propósito Principal | Leitura Mandatória Para |
| :--- | :--- | :--- |
| 📄 **[objetivo_sistema.md](/home/luska/Documents/projects/esp32_mosfet_analysis/docs/objetivo_sistema.md)** | **O "Porquê" e "Para Quê" (Negócios/Produto).** Define o problema resolvido pela plataforma (barreira financeira de SMUs), os atores (quem usa) e os fluxos centrais (varreduras e extração de $G_m$/$V_{th}$). | Onboarding de novos desenvolvedores e entendimento de negócios pela IA antes de propor features. |
| 📄 **[arquitetura_sistema.md](/home/luska/Documents/projects/esp32_mosfet_analysis/docs/arquitetura_sistema.md)** | **O "Como Funciona" (Técnico/Arquitetura).** Mapeia a separação física e de software em 6 camadas estritas. Detalha padrões de projeto, fluxo IPC entre cores do FreeRTOS e topologia do projeto. | Planejamento de código, refatoração e auditoria estrutural. |
| 📄 **[future_updates_2026_2.md](/home/luska/Documents/projects/esp32_mosfet_analysis/docs/future_updates_2026_2.md)** | **O "Futuro" (Débitos e Roadmap).** Centraliza problemas sistêmicos descobertos (como o autoaquecimento de MOSFET) e soluções panejadas (ex: modo de varredura pulsada, testes mock). | Revisão de backlog técnico e preparação para próximas fases. |
| 📁 **[legacy/](/home/luska/Documents/projects/esp32_mosfet_analysis/docs/legacy/)** | **O "Passado" (Histórico).** Armazena documentações originais, prompts legados e arquivos do período PIBIC que não refletem mais o estado 100% atual da aplicação. | Consulta a decisões passadas e referências acadêmicas antigas. |

---

## 📐 Integração com o Spec-Driven Development (SDD)

Este projeto opera no modelo **Spec-Driven Bottom-Up**. Isso significa que a documentação aqui presente não é um mero acessório, ela é a **verdade absoluta** (Single Source of Truth). 

Se houver uma discrepância entre o que está implementado no código (`src/`) e o que está definido aqui (especialmente em `arquitetura_sistema.md`), a regra é:
1. Uma auditoria deve listar os desvios.
2. O desenvolvedor decide se o código será corrigido para voltar à arquitetura original **ou** se a documentação será atualizada caso a mudança no código tenha sido proposital. 

## 🛠️ Manutenção

* **Agentes de IA:** Sempre que um PR for concluído ou uma arquitetura mudar significativamente (ex: integração do Mock Pytest planejado), a IA (`doc-updater` ou `version_control`) deverá atualizar os arquivos desta pasta.
* **Proibido Código:** Esta pasta não deve conter trechos exaustivos de código (apenas exemplos rápidos e diagramas lógicos). O detalhamento técnico deve ir para os `README.md` locais das pastas de código (ex: `src/README.md`) ou `.agents/rules/`.
