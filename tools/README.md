# tools/

Python scripts for offline preprocessing.

Empty for now. Eventually:

- `gen_tables.py` — generate ADAA lookup tables from physical tube curves (Dempwolf-Zölzer triode, Cohen-Hélie / Dempwolf pentode); emits Faust constant tables for `dsp/tables/`
- `loadline.py` — solve the load-line constraint `Ia = f_tube(Vg, Va)` with `Va = Vs - Ia·Rl`
- `reduce.py` — Mačák Algorithm 1 data reduction for nonuniform table grids (T2.3)
- `validate.py` — ABX comparison helpers for diff-checking ports against Keller's JSFX output

Dependencies: NumPy, SciPy, optionally Matplotlib for development plots.
Run as a virtual environment, e.g. `python -m venv .venv && source .venv/bin/activate.fish`.
