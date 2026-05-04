# tests/

Test fixtures and audio files for ABX/regression validation.

Empty for now. Eventually:

- `audio/` — short input signals (DI guitar, sine sweeps, chord stabs) used to compare ports against Keller's JSFX reference
- `expected/` — reference output rendered by Keller's JSFX in REAPER (via YSFX)
- Spec for the regression harness: render the same input through both plugins, compare per-sample to within numerical noise

Audio files are large and gitignored by default; use `git lfs` or out-of-tree storage if collections grow.
