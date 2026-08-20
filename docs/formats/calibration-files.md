# Microphone calibration file formats

Parser: `core/calibration/CalibrationParser.kt`. Test fixtures (real vendor
files): `core/calibration/src/test/resources/calibration/`. Decision record:
[ADR-0006](../adr/0006-calibration-file-handling.md).

All three dialects are plain text: an optional header line, then one data
point per line, whitespace-separated, frequency ascending in Hz, gain in dB.

## Dialect 1 — miniDSP (UMIK-1 / UMIK-2)

```
"Sens Factor =-13.47dB, AGain =18dB, SERNO: 8105623"
10.054	-4.7069
10.179	-4.6129
...
```

- Header is a quoted string: `Sens Factor` (dB), optionally `AGain`
  (the UMIK-2's internal analog gain, dB), `SERNO`.
- Data: `frequency  gain_dB` (2 columns). No phase.
- miniDSP also issues a **90° incidence** variant with an extra quoted
  comment line after the header — comment lines must not be parsed as a
  second header:

```
"Sens Factor =-13.47dB, AGain =18dB, SERNO: 8105623"
"Auto-generated 90-degree calibration file"
10.054	-4.7069
```

- Sensitivity semantics (per miniDSP): with the mic at its reference gain,
  `dB SPL = dBFS_reading + 94 - SensFactor_dB` at 1 kHz (to be validated
  against a calibrator before the SPL feature ships — see docs/validation).

## Dialect 2 — OmniMic V2 (`.omm`)

```
"Sens Factor =-5.687dB, SERNO: 2080581"
4.6758 -0.0756 18.34
5.099 0.2503 18.02
...
```

- Same quoted-header style, no `AGain`.
- Data: `frequency  gain_dB  phase_deg` (**3 columns** — this is the only
  dialect carrying phase).

## Dialect 3 — Dayton Audio (iMM-6 / iMM-6C)

```
*1000Hz	-36.0
20.00	0.2
20.55	0.2
...
```

- Header starts with `*`: reference frequency and the mic's sensitivity
  figure at that frequency. Note the semantics differ from miniDSP's
  `Sens Factor` — do not treat the numbers as interchangeable.
- Data: `frequency  gain_dB` (2 columns). No phase.
- The **iMM-6C** is a USB-C (UAC) digital mic; the original iMM-6 was an
  analog TRRS mic. Same file format.

## Parsing rules (normative)

1. The **first** line beginning with `"` or `*` is the header; any later
   `"`/`*` lines are comments and are ignored (but preserved in the raw
   preview).
2. A data line is 2 or 3 columns of numbers separated by whitespace and/or
   commas: `freq gain [phase]`. Lines that don't parse are skipped silently.
3. Points are sorted by frequency after parsing. A file with zero data
   points is an error.
4. Gain (and phase, when present) is interpolated **linearly in
   log-frequency**; outside the calibrated range the endpoint value is held
   (clamped), and the UI should mark the uncalibrated region.
5. The raw header + first rows are kept (`previewLines`) and must be shown
   in the settings UI so users can compare what the file says against what
   the app parsed.
