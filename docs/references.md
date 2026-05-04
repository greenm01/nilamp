# References

Catalog of papers and notes in this directory. The `dsp-project-notes.md` § 10 has a fuller bibliography with links to threads and articles that aren't included as PDFs here.

## Notes (`notes/`)

- **`dsp-project-notes.md`** — primary design document. § 2 has the actionable direction (tier-ranked improvements). Read this first.
- **`conversation-log.txt`** — full transcript of the exploration that produced the notes. Useful for tracing why a decision was made; not needed for day-to-day work.

## Papers (`papers/`)

### The foundation we extend

- **`Keller-2024-tube-amp-modeling.pdf`** — Helmut Keller, "A Tube Amp Modeling Project" V1.0.3 (2024). Block-diagram + ADAA + lookup-table approach for the Fender Tweed Deluxe 5E3. The architecture nilamp is built on. JSFX source: `/home/niltempus/Downloads/hk jsfx v1.0.4 b/HK JSFX V1.0.4 B/`.

### Direct sources for the implementation

- **`Macak-2012-thesis.pdf`** — Jaromír Mačák PhD thesis, "Real-time Digital Simulation of Guitar Amplifiers as Audio Effects" (Brno UT, 2012). Three OPT models (Frohlich, Gyrator-Capacitor, Jiles-Atherton) worked through with real-time implementations. § 6.4 has the ABX listening test that downgraded OPT modeling from "research frontier" to "optional refinement" (see `notes/dsp-project-notes.md` § 6.1).

- **`Macak-Holters-Schimmel-2012-DAFx.pdf`** — DAFx-12 paper, "Simulation of Fender Type Guitar Preamp using Approximation and State-Space Model." Introduces (a) cubic spline interpolation with constant access, (b) Algorithm 1 data reduction for nonuniform grids, (c) the decomposition trick for two-tube circuits, (d) parametric tone stack inside the K matrix. T2.3 and T3.2 in the project notes draw from this.

### Background — Hegglun's PAK Project

- **`Hegglun-PAK-Project-outline.pdf`** — 3-page overview of all 5 PAK levels and ~60 sections.
- **`Hegglun-PAK1-user-guide.pdf`** — full PAK1 (2012) user guide. Foundational Lambert W BJT/diode solutions with worked examples.
- **`Hegglun-PAK213-TriJmos.pdf`** — PAK213 (2023) tube paper. TriJmos LTspice model: triodes/SITs/jFETs on top of VDMOS using power-law + Island-effect lookup table + SoftPlus Anlauf region. Notably uses no Lambert W — confirms tubes are power-law, not exponential.

## External resources (not included)

See `notes/dsp-project-notes.md` § 10 for:

- Meriläinen, "Current-Driving of Loudspeakers" (current-drive.info)
- Hegglun's diyAudio threads (current-drive amp designs)
- Hegglun's Linear Audio articles (Vol 1, 8, 13)
- Yeh 2009 Stanford CCRMA dissertation
- Cohen & Hélie 2010, 2011 — IRCAM Lambert W triode/pentode
- Dempwolf & Zölzer 2011 — physically-motivated triode model (the model nilamp uses for T2.2)
- Werner 2016 — R-type WDF
- Parker, Esqueda, Bilbao 2016 — ADAA
- NodalDKFramework MATLAB reference: `~/src/NodalDKFramework/` (not in this repo; non-commercial license)
