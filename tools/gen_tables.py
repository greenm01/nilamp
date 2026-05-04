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

    # Saturation limits used by ADAA outside [-xmax, xmax].
    ymin = -k0
    ymax = 1.0 - k0

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

def export_faust_table(name, table):
    """Export a generated ADNL table to a Faust waveform definition.

    Layout:
        [coeffs flattened (num_segments * 9 floats),
         ymin, ymax, z_at_xmax]

    Total length is num_segments * 9 + 3.  The runtime (hk_adnl.lib) reads the
    three trailing metadata cells via rdtable at offsets t_size-3, t_size-2,
    and t_size-1.
    """
    coeffs = table["coeffs"]
    flat = list(np.asarray(coeffs).flatten())
    flat += [table["ymin"], table["ymax"], table["z_at_xmax"]]
    body = ", ".join(f"{v:.10e}" for v in flat)
    return f"{name} = waveform {{{body}}};\n"

if __name__ == "__main__":
    # Smoke-test: 12AX7 common-cathode (ibias / isat).
    k0 = 0.00076 / 0.00165
    spec = gen_adnl_table(k0, b=0, type_b=0, kloop=0)
    print(f"Generated table: {spec['num_segments']} segments, "
          f"ymin={spec['ymin']:.4f}, ymax={spec['ymax']:.4f}, "
          f"z_at_xmax={spec['z_at_xmax']:.4f}")

