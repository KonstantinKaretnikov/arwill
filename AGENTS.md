# Agent Guidance

This file is durable guidance for Codex sessions and other coding agents
working on Arwill.

Before changing architecture, read `MANIFESTO.md` and the relevant ADRs in
`docs/decisions/`.

Rules for future work:

- Do not silently change public contracts.
- Do not create cross-layer dependencies to save time.
- Do not introduce abstractions without a current use case.
- Do not add a dependency without documenting why.
- Do not copy code without license and attribution.
- Prefer small, reviewable changes.
- Keep the system bootable after each completed task.
- Run all available checks before declaring completion.
- Update documentation when behavior or architecture changes.
- Record substantial architectural decisions as ADRs.
- Explain uncertainty rather than inventing hardware facts.
- Never claim a test passed unless it was actually executed.
- Preserve separation between architecture-independent, architecture-specific,
  and platform-specific code.
- Keep all generated code understandable by a human reviewer.
- Treat shell Russian-layout input normalization as ASCII command-entry
  convenience, not as Cyrillic text support.
- When a durable workflow agreement is made with the project owner, update this
  file or another appropriate document in the same change so future sessions do
  not need to rediscover it.
- Commit completed, verified milestones locally, but do not push every commit
  automatically. Push when asked, when sharing is needed, or when the owner has
  clearly approved publishing the accumulated work.
- For longer tasks, play `/System/Library/Sounds/Glass.aiff` with `afplay`
  after the work is complete, if tool permissions allow it.

Avoid implementing future subsystems as placeholders. A missing scheduler,
allocator, filesystem, shell, graphics layer, network stack, interrupt layer, or
driver model should remain honestly absent until there is a current requirement.

## Completion Report Format

Use this format when reporting completed work:

Summary

Files changed

Architectural impact

Commands executed

Verification result

Remaining risks
