# SPDX-License-Identifier: MIT
import numpy as np
import os

def glf(x, k0, b, type_b):
    """Generalized Logistic Function (Keller).

    Per HK_LIB_ADNL.jsfx-inc lines 67-68, ``type`` is a continuous blend in
    [0, 1] between Type A and Type B, **not** a boolean.  Specifically:

        glf(x) = (1 - type_b) * type_A(x) + type_b * type_B(x)

    so type_b=0 selects Type A, type_b=1 selects Type B, and the canonical
    5E3 stages all use type_b=0.5 (50/50 mix).
    """
    va = np.log(k0) / np.log(1 + np.exp(b))
    kaa = -1 / va * (1 + np.exp(b)) ** (1 - va) / np.exp(b)
    type_a = (1 + np.exp(b - kaa * x)) ** va - k0

    vb = np.log(1 - k0) / np.log(1 + np.exp(-b))
    kab = -1 / vb * (1 + np.exp(-b)) ** (1 - vb) / np.exp(-b)
    type_b_val = 1 - (1 + np.exp(-b + kab * x)) ** vb - k0

    return (1 - type_b) * type_a + type_b * type_b_val


def _curve_to_adnl_table(f_closed, xmax, dx, ymin, ymax):
    """Pack a curve sampled at dx/3 into Keller's cubic + antiderivative table.

    ``f_closed`` must be sampled at the high-res grid
    ``np.arange(-xmax, xmax + dx, dx/3)`` (3 sub-samples per output segment
    plus a trailing endpoint).  Returns the same dict shape as
    :func:`gen_adnl_table`, including the ``z_at_xmax`` precomputation.
    """
    num_segments = int(2 * xmax / dx)
    table = []

    b00 = 0.0
    b10 = 0.0
    b20 = 0.0
    b30 = 0.0
    b40 = 0.0

    for i in range(num_segments):
        idx = i * 3
        v0 = f_closed[idx]
        v1 = f_closed[idx + 1]
        v2 = f_closed[idx + 2]
        v3 = f_closed[idx + 3]

        a0 = v0
        f1_rel = v1 - a0
        f2_rel = v2 - a0
        f3_rel = v3 - a0

        a1 = (f3_rel - 4.5 * f2_rel + 9.0 * f1_rel) / dx
        a2 = (-4.5 * f3_rel + 18.0 * f2_rel - 22.5 * f1_rel) / (dx ** 2)
        a3 = (4.5 * f3_rel - 13.5 * f2_rel + 13.5 * f1_rel) / (dx ** 3)

        b4 = a3 / 4.0
        b3 = a2 / 3.0
        b2 = a1 / 2.0
        b1 = a0
        b0 = b00 + dx * (b10 + dx * (b20 + dx * (b30 + dx * b40)))

        # ab[0..8] = a3, a2, a1, a0, b4, b3, b2, b1, b0
        table.append([a3, a2, a1, a0, b4, b3, b2, b1, b0])

        b00, b10, b20, b30, b40 = b0, b1, b2, b3, b4

    coeffs = np.array(table, dtype=np.float32)

    # Precompute z_at_xmax: the antiderivative evaluated at w = dx in the last
    # segment (i.e. at x = xmax). Saves 5 rdtable + 4 mul/add per sample at
    # runtime in the right-saturation branch.
    last = coeffs[-1]
    b4_l, b3_l, b2_l, b1_l, b0_l = last[4], last[5], last[6], last[7], last[8]
    z_at_xmax = float(((((b4_l * dx + b3_l) * dx + b2_l) * dx + b1_l) * dx + b0_l))

    return {
        "coeffs": coeffs,
        "num_segments": num_segments,
        "xmax": float(xmax),
        "dx": float(dx),
        "ymin": float(ymin),
        "ymax": float(ymax),
        "z_at_xmax": z_at_xmax,
    }


def gen_adnl_table(k0, b, type_b, kloop, xmax=15.0, dx=0.02):
    """Generate ADNL table coefficients (cubic for f, 4th order for F).

    Returns a dict with keys:
      - 'coeffs':       np.ndarray, shape (num_segments, 9), dtype float32
      - 'num_segments': int
      - 'xmax', 'dx':   float (the grid params used)
      - 'ymin', 'ymax': float (saturation limits, ymin = -k0, ymax = 1 - k0)
      - 'z_at_xmax':    float (antiderivative evaluated at the right edge of
                        the last segment; precomputed so the runtime does not
                        need to fetch and evaluate 5 trailing cells per sample)
    """
    # 1. Generate high-resolution grid for resampling if kloop > 0.
    # Keller uses dx1 = dx / 3 for the internal grid.
    dx1 = dx / 3.0
    x_internal = np.arange(-xmax, xmax + dx, dx1)
    f_internal = glf(x_internal, k0, b, type_b)

    if kloop > 0:
        x_ext_norm = (x_internal + kloop * f_internal) / (kloop + 1.0)
        x_target = np.arange(-xmax, xmax + dx, dx1)
        f_closed = np.interp(x_target, x_ext_norm, f_internal,
                             left=-k0, right=1.0 - k0)
    else:
        f_closed = f_internal

    return _curve_to_adnl_table(f_closed, xmax, dx, ymin=-k0, ymax=1.0 - k0)


def _clip_curve(f_raw, ymin, ymax):
    """Clip ``f_raw`` to [ymin, ymax].

    The DZ-derived curves (especially cathodyne, which has a high
    rk+rl load-line) can rise much faster than the GLF tanh-like ones
    and saturate within a single segment.  We hard-clip here; an
    earlier smoothing prototype (Gaussian over the high-resolution
    grid) actually worsened the cubic fit because it shifted sample
    values across segment boundaries, so we keep the clip simple and
    rely on the test tolerances accommodating the cubic-fit precision
    floor at the transition (see tests/regression.rs).
    """
    return np.clip(f_raw, ymin, ymax)


def gen_adnl_table_dz_ck(vs, ra, rl, rk, isat, ibias, kpre,
                         xmax=15.0, dx=0.02):
    """Generate an ADNL table for a common-cathode DZ ECC83 stage.

    Imports the load-line solver from keller_oracle (kept there as the
    single source of truth for the DZ model + Newton iteration).  The
    output curve is centered on the DZ-derived quiescent so that f(0)=0,
    matching Keller's GLF convention exactly. Downstream runtime wiring can
    then use the same normalized-current convention as Keller's JSFX.

    Saturation handling: at the negative end (cutoff) the load-line solve
    naturally gives ``ip → 0``, so f → -ip_q/isat = -kbias_actual.  At the
    positive end the bare DZ model has no plate-bottoming mechanism and
    happily produces unphysical ip ≫ isat; we clip f at ``+1 - kbias_actual``
    to mirror Keller's GLF saturation level (the real-world limit comes from
    grid current clamping at the previous coupling cap, which is modelled by
    Keller's PKD path, not the static curve).
    """
    from keller_oracle import loadline_curve_ck  # lazy: avoids circular import
    dx1 = dx / 3.0
    x_norm_grid = np.arange(-xmax, xmax + dx, dx1)
    vin_grid = x_norm_grid / kpre
    ip_arr, _vp, _vk = loadline_curve_ck(vs, ra, rl, rk, isat, ibias, vin_grid)
    ip_pos = -ip_arr  # DZ returns negative; flip to positive (Keller convention)
    zero_idx = int(np.argmin(np.abs(x_norm_grid)))
    ip_q = ip_pos[zero_idx]
    f_raw = (ip_pos - ip_q) / isat
    kbias_actual = ip_q / isat
    # Match GLF range [-kbias, 1-kbias].  Clip + smooth the corner so the
    # cubic fit in _curve_to_adnl_table doesn't glitch at the transition.
    ymin = -kbias_actual
    ymax = 1.0 - kbias_actual
    f_closed = _clip_curve(f_raw, ymin, ymax)
    return _curve_to_adnl_table(f_closed, xmax, dx, ymin=float(ymin), ymax=float(ymax))


def gen_adnl_table_dz_cd(vs, ra, rl, rk, isat, ibias, kpre,
                         xmax=15.0, dx=0.02):
    """Generate an ADNL table for a cathodyne DZ ECC83 stage.

    Same saturation clipping as :func:`gen_adnl_table_dz_ck` — see that
    function's docstring for rationale.
    """
    from keller_oracle import loadline_curve_cd
    dx1 = dx / 3.0
    x_norm_grid = np.arange(-xmax, xmax + dx, dx1)
    vin_grid = x_norm_grid / kpre
    ip_arr, _ig, _vp, _vk = loadline_curve_cd(vs, ra, rl, rk, vin_grid)
    ip_pos = -ip_arr
    zero_idx = int(np.argmin(np.abs(x_norm_grid)))
    ip_q = ip_pos[zero_idx]
    f_raw = (ip_pos - ip_q) / isat
    kbias_actual = ip_q / isat
    ymin = -kbias_actual
    ymax = 1.0 - kbias_actual
    f_closed = _clip_curve(f_raw, ymin, ymax)
    return _curve_to_adnl_table(f_closed, xmax, dx, ymin=float(ymin), ymax=float(ymax))


def c_symbol(name):
    return "nilamp_" + "".join(ch if ch.isalnum() or ch == "_" else "_" for ch in name)

def export_c_table_decl(name, table):
    flat_len = int(table["coeffs"].size) + 3
    sym = c_symbol(name)
    return f"extern const float {sym}[{flat_len}];\nextern const size_t {sym}_len;\n"

def export_c_table_def(name, table):
    coeffs = table["coeffs"]
    flat = list(np.asarray(coeffs).flatten())
    flat += [table["ymin"], table["ymax"], table["z_at_xmax"]]
    sym = c_symbol(name)
    lines = [f"const float {sym}[{len(flat)}] = {{"]
    for i in range(0, len(flat), 6):
        chunk = ", ".join(f"{v:.10e}f" for v in flat[i:i + 6])
        suffix = "," if i + 6 < len(flat) else ""
        lines.append(f"    {chunk}{suffix}")
    lines.append("};")
    lines.append(f"const size_t {sym}_len = {len(flat)};")
    lines.append("")
    return "\n".join(lines)

if __name__ == "__main__":
    # Smoke-test: 12AX7 common-cathode (ibias / isat).
    k0 = 0.00076 / 0.00165
    spec = gen_adnl_table(k0, b=0, type_b=0, kloop=0)
    print(f"Generated table: {spec['num_segments']} segments, "
          f"ymin={spec['ymin']:.4f}, ymax={spec['ymax']:.4f}, "
          f"z_at_xmax={spec['z_at_xmax']:.4f}")
