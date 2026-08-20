# ADR-0001: Record architecture decisions

- Status: Accepted
- Date: 2026-08-19

## Context

AudioAnalyzer is a measurement tool: its value depends on results being
trustworthy and reproducible. That extends to the project itself — a
contributor (or the original author, a year later) must be able to trace *why*
the code is shaped the way it is, and rebuild the project from documentation
alone.

## Decision

Record every significant technical decision as an ADR in `docs/adr/`,
MADR-style, numbered sequentially. ADRs are immutable once accepted;
superseding decisions get new numbers and cross-links. Alongside ADRs:

- `docs/building.md` pins the exact toolchain (versions **and** paths).
- `docs/formats/` specifies every on-disk and on-air format the app reads or
  emits.
- `docs/validation/` records how each measurement feature was verified.

## Consequences

- Slight overhead per decision; paid back the first time anyone asks "why
  Oboe?" or "why minSdk 31?".
- Docs are part of the definition of done: a PR that changes a decision or a
  build requirement without updating the docs is incomplete.
