# AudioAnalyzer documentation

The documentation is written so that someone with no prior context could
rebuild the project — tooling, decisions, formats, and verification — from
these files alone.

| Where | What |
| --- | --- |
| [building.md](building.md) | Exact toolchain versions, install steps, and paths needed to build from a clean machine |
| [roadmap.md](roadmap.md) | Development phases and what each one delivers |
| [adr/](adr/README.md) | Architecture Decision Records — every significant decision, with context and consequences |
| [formats/](formats/) | On-disk / on-air format specifications (calibration files, sync frame) |
| [validation/](validation/README.md) | How each measurement feature is verified against known references |

Conventions:

- Docs are plain GitHub-flavored Markdown, one topic per file.
- ADRs follow [MADR](https://adr.github.io/madr/)-style structure and are
  immutable once accepted — a change of mind becomes a *new* ADR that
  supersedes the old one.
- Anything a contributor must know to reproduce a result (tool version, path,
  command, reference value) belongs in a doc, not in a commit message.
