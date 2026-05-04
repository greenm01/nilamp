# SPDX-License-Identifier: MIT
"""Plot Keller's GLF curves alongside the Dempwolf-Zölzer load-line
curves at each ECC83/12AX7 stage's operating point.

Produces ``docs/notes/figures/dz_vs_glf.png`` (also a per-stage ``.png``
per panel) with three subplots — T1 (12AX7-mod), T2, T3 (cathodyne) —
each showing the normalized output ``y = (ip - ip_q) / isat`` vs
normalized grid drive ``x = kpre * vin`` over [-3, +3].

Run from the repo root::

    python3 tools/plot_dz_vs_glf.py

Requires matplotlib.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).parent))

import gen_5e3_tables as t5e3  # noqa: E402
from gen_tables import gen_adnl_table, gen_adnl_table_dz_ck, gen_adnl_table_dz_cd  # noqa: E402


FIG_DIR = Path("docs/notes/figures")


def _eval_table(spec, x_arr):
    """Evaluate the cubic-fit ADNL table at arbitrary x (vectorised)."""
    coeffs = spec["coeffs"]
    xmax = spec["xmax"]
    dx = spec["dx"]
    nseg = spec["num_segments"]
    ymin = spec["ymin"]
    ymax = spec["ymax"]
    out = np.empty_like(x_arr, dtype=np.float64)
    for i, x in enumerate(x_arr):
        if x <= -xmax:
            out[i] = ymin
            continue
        if x >= xmax:
            out[i] = ymax
            continue
        idx = int((x + xmax) / dx)
        idx = max(0, min(idx, nseg - 1))
        w = (x + xmax) - idx * dx
        a3, a2, a1, a0 = coeffs[idx, :4]
        out[i] = ((a3 * w + a2) * w + a1) * w + a0
    return out


def _plot_panel(ax, cfg, topology, x_lim=3.0):
    glf_spec = gen_adnl_table(cfg.kbias, cfg.b, cfg.type_b, cfg.kloop)
    if topology == "ck":
        dz_spec = gen_adnl_table_dz_ck(
            vs=cfg.vs, ra=cfg.ra, rl=cfg.rl, rk=cfg.rk,
            isat=cfg.isat, ibias=cfg.ibias, kpre=cfg.kpre,
        )
    else:
        dz_spec = gen_adnl_table_dz_cd(
            vs=cfg.vs, ra=cfg.ra, rl=cfg.rl, rk=cfg.rk,
            isat=cfg.isat, ibias=cfg.ibias, kpre=cfg.kpre,
        )

    x = np.linspace(-x_lim, x_lim, 1201)
    y_glf = _eval_table(glf_spec, x)
    y_dz = _eval_table(dz_spec, x)

    ax.plot(x, y_glf, label="GLF (Keller)", lw=1.4, color="#1f77b4")
    ax.plot(x, y_dz, label="DZ + load-line", lw=1.4, color="#d62728")
    ax.axhline(0.0, color="0.7", lw=0.5)
    ax.axvline(0.0, color="0.7", lw=0.5)
    ax.axhline(glf_spec["ymax"], color="#1f77b4", lw=0.5, ls=":", alpha=0.6)
    ax.axhline(glf_spec["ymin"], color="#1f77b4", lw=0.5, ls=":", alpha=0.6)
    ax.axhline(dz_spec["ymax"], color="#d62728", lw=0.5, ls=":", alpha=0.6)
    ax.axhline(dz_spec["ymin"], color="#d62728", lw=0.5, ls=":", alpha=0.6)
    ax.set_title(
        f"{cfg.name}  ({topology.upper()})\n"
        f"GLF kbias={cfg.kbias:.3f}   DZ kbias={-dz_spec['ymin']:.3f}"
    )
    ax.set_xlabel("x = kpre · vin")
    ax.set_ylabel("y = (ip - ip_q) / isat")
    ax.set_xlim(-x_lim, x_lim)
    ax.legend(loc="best", fontsize=8)
    ax.grid(True, alpha=0.3)


def main():
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is required for plot_dz_vs_glf.py", file=sys.stderr)
        sys.exit(1)

    FIG_DIR.mkdir(parents=True, exist_ok=True)

    panels = [
        (t5e3.T1_12AX7, "ck"),
        (t5e3.T2_12AX7, "ck"),
        (t5e3.T3_CD,    "cd"),
    ]

    fig, axes = plt.subplots(1, 3, figsize=(15, 4.5))
    for ax, (cfg, topology) in zip(axes, panels):
        _plot_panel(ax, cfg, topology)
    fig.suptitle("DZ ECC83 + load-line vs Keller GLF static-NL curves", y=1.02)
    fig.tight_layout()
    out_path = FIG_DIR / "dz_vs_glf.png"
    fig.savefig(out_path, dpi=110, bbox_inches="tight")
    print(f"wrote {out_path}")

    # Also emit a per-stage zoomed plot for documentation use.
    for cfg, topology in panels:
        fig, ax = plt.subplots(figsize=(6, 4))
        _plot_panel(ax, cfg, topology, x_lim=3.0)
        fig.tight_layout()
        out_path = FIG_DIR / f"dz_vs_glf_{cfg.name}.png"
        fig.savefig(out_path, dpi=110, bbox_inches="tight")
        plt.close(fig)
        print(f"wrote {out_path}")


if __name__ == "__main__":
    main()
