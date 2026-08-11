# GEMINI.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Prioritize additive changes over radical refactoring.**

When editing existing code:
- **Think Additive:** Prefer adding new functions or "addons" over re-writing existing logic.
- **No Radical Shifts:** Avoid "hardcore" or abrupt modifications that change the script's structure unless absolutely necessary.
- **Surgical Precision:** Every change must be minimal. Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

## 5. Code Modification Strategy

**One file at a time. Strict isolation between tool calls.**

- **Atomic Tool Calls**: You MUST NOT modify multiple files in a single turn or within a single multi-tool call transaction.
- **One File Per Turn**: When asked to make changes across multiple files, plan the changes but execute them sequentially. Apply the modification to exactly ONE file, then pause and wait for the user's approval before moving to the next file.
- **Independent Approvals**: Treat every single file edit as an independent transaction. Never batch file operations together so that a rejection of one file does not impact previously accepted files.
- **Rejection Handling**: If the user rejects a modification (e.g., choosing option 3), immediately stop that specific path, acknowledge the rejection, and ask for feedback or move to the next independent file. Do not attempt to batch the rejected file into subsequent calls.

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

## 6. Prefer Mermaid for Flows

- **Visual Diagrams:** Sempre que possível, utilize diagramas do Mermaid para criar fluxos de entendimento visual e facilitar a compreensão de processos, dependências e arquiteturas complexas para o usuário.
- **Default Styling (Standard):** Ao criar diagramas do Mermaid, **evite** definir colorações customizadas (com `style` ou `classDef`), pois elas podem ficar ilegíveis dependendo se o usuário está usando tema claro ou escuro em seu editor. Sempre prefira as colorações e temas padrão (standard) nativos do interpretador Mermaid, garantindo máxima legibilidade em qualquer ambiente.
