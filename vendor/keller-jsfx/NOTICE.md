# Keller JSFX — third-party source

This directory contains Helmut Keller's JSFX plugin source code (V1.0.4 B), included as **reference material** for the nilamp port.

## License (Keller's terms, verbatim)

From the paper "A Tube Amp Modeling Project V1.0.3" by Helmut Keller (2024):

> "The author encourages everybody interested in tube amp modeling to use the free-of-charge source code and the underlying DSP block library as a template for their own non-commercial projects, the only condition being that the author is properly cited in publications. Commercially oriented use, however, needs a written permission from the author."

## What this means for nilamp

- **The contents of this `vendor/keller-jsfx/` directory are NOT covered by nilamp's MIT license.** They are governed by Keller's terms above.
- The rest of nilamp (everything outside `vendor/keller-jsfx/`) is MIT-licensed.
- nilamp is a non-commercial project. If anyone forks nilamp for commercial use, they must either:
  1. Remove `vendor/keller-jsfx/` entirely, or
  2. Obtain written permission from Helmut Keller for the included source.

## Citation

When publishing work derived from this source, cite:

> Keller, H. (2024). *A Tube Amp Modeling Project V1.0.3*. https://www.helmutkelleraudio.de/

## Original source

Files in this directory are copied verbatim from Keller's distribution (`hk jsfx v1.0.4 b/HK JSFX V1.0.4 B/`):

- `TWD DLX.jsfx`, `TWD DLX  II.jsfx` — 5E3 amp models (the foundation nilamp ports)
- `Libs/` — underlying tube/filter/ADAA library
- `360 Degree Panner.jsfx`, `Blender.jsfx`, `Volume Wah.jsfx`, `X Feed.jsfx`, `X Feed II.jsfx` — unrelated effect plugins from the same distribution; preserved for completeness, not used by nilamp
- `A Tube Amp Modeling Project V1.0.3.pdf` — Keller's full paper accompanying the source. The same paper is also in `docs/papers/Keller-2024-tube-amp-modeling.pdf` for research bibliography purposes.

## Contact

Helmut Keller Audio: https://www.helmutkelleraudio.de/
