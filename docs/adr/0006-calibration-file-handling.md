# ADR-0006: Calibration file dialects and sensitivity semantics

- Status: Accepted
- Date: 2026-08-19

## Context

The three measurement mics we target ship calibration files in three related
but distinct text dialects (full spec with real examples:
[../formats/calibration-files.md](../formats/calibration-files.md)):

- **miniDSP UMIK-1/2**: quoted header with `Sens Factor` and `AGain`, then
  `freq gain` pairs. Separate 90°-incidence file.
- **OmniMic V2** (`.omm`): quoted header with `Sens Factor`, then
  `freq gain phase` triples — includes phase.
- **Dayton iMM-6C**: star header `*1000Hz␉-36.0` (reference frequency +
  sensitivity), then `freq gain` pairs.

The header *semantics* differ across vendors even where the syntax looks
similar, and users may hold files that deviate from all three examples.

## Decision

1. One lenient parser (`core/calibration`, pure Kotlin): the first quoted or
   star line is the header; later quoted lines are comments (the UMIK 90°
   file has one); data lines are 2–3 numeric columns; anything else is
   skipped. Points are sorted; interpolation is linear in log-frequency,
   clamped at the endpoints.
2. The settings UI always shows the **raw header line and the first rows
   verbatim** (`previewLines`) next to the parsed interpretation, so users
   can spot a dialect mismatch themselves.
3. Frequency *shape* correction comes from the file. Absolute SPL comes from
   the header sensitivity where the semantics are known — all three target
   mics are self-describing USB (UAC) digital devices, so file sensitivity
   can anchor dBFS→SPL directly. A **manual SPL trim** (match against a
   reference SLM or a 94 dB calibrator) is always available and is the only
   absolute-level path for the phone's internal mic, which has no file.
4. Real vendor files are unit-test fixtures
   (`core/calibration/src/test/resources/calibration/`).

## Consequences

- New dialects extend the parser + fixtures + the formats doc together.
- SPL-mapping math per header type lands with the SPL meter (Phase 1) and
  gets a validation doc entry against a reference SLM.
