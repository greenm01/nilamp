# SPDX-License-Identifier: MIT
"""Keller oracle — mechanical NumPy translation of Keller's JSFX reference.

This module reproduces Keller's block-diagram + ADAA tube-amp model in NumPy
for regression testing the native C implementation. It is intentionally close-to-source,
not refactored.  Tolerance target: per-sample absolute error < 1e-3, RMS < -60 dB.

Two static-NL paths are provided:

* ``adnl_set_glf(...)``: Keller's behavioral generalized-logistic function,
  matched 1:1 against the JSFX reference.  Used for the v1 5E3 stages and
  for the 6V6 power tubes (where Keller's ``b > 0`` shape is intentional).

* ``adnl_set_dz_ck(...)`` / ``adnl_set_dz_cd(...)``: physically-motivated
  Dempwolf-Zölzer ECC83 / 12AX7 model with per-stage DC load-line solve.
  These replace Keller's symmetric-tanh fit (``b=0, type=0.5``) with the
  asymmetric grid-current / soft-turn-off behaviour of a real 12AX7.  Used
  for the v2 5E3 ECC83 stages (T1 in the 12AX7-mod variant, T2, T3).
"""

import numpy as np

# ---------------------------------------------------------------------------
# GLF (generalised logistic function) — Keller's static nonlinearity
# ---------------------------------------------------------------------------

def glf(x, k0, b, type_b):
    """Generalized Logistic Function (Keller).

    Per HK_LIB_ADNL.jsfx-inc lines 67-68, ``type`` is a continuous blend in
    [0, 1] between Type A and Type B (0 = Type A, 1 = Type B).  The canonical
    5E3 stages all use type_b=0.5.
    """
    va = np.log(k0) / np.log(1 + np.exp(b))
    kaa = -1 / va * (1 + np.exp(b)) ** (1 - va) / np.exp(b)
    type_a = (1 + np.exp(b - kaa * x)) ** va - k0

    vb = np.log(1 - k0) / np.log(1 + np.exp(-b))
    kab = -1 / vb * (1 + np.exp(-b)) ** (1 - vb) / np.exp(-b)
    type_b_val = 1 - (1 + np.exp(-b + kab * x)) ** vb - k0

    return (1 - type_b) * type_a + type_b * type_b_val


# ---------------------------------------------------------------------------
# ADNL — antiderivative anti-aliasing with polynomial lookup tables
# ---------------------------------------------------------------------------

def _curve_to_adnl_table(f_closed, xmax, dx, ymin, ymax):
    """Convert a curve sampled at dx/3 spacing into Keller's cubic + antider table.

    ``f_closed`` must be sampled at the high-res grid ``arange(-xmax, xmax+dx, dx/3)``
    (i.e. 3 sub-samples per output segment plus a trailing endpoint).  Each output
    segment fits a cubic ``a3 w^3 + a2 w^2 + a1 w + a0`` to the four sub-samples,
    and stores the antiderivative ``b4 w^4 + b3 w^3 + b2 w^2 + b1 w + b0`` with
    ``b0`` chained across segments so ADAA's quotient form is well-defined.
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

        # ab[0]=a3, ab[1]=a2, ab[2]=a1, ab[3]=a0, ab[4]=b4, ab[5]=b3,
        # ab[6]=b2, ab[7]=b1, ab[8]=b0
        table.append([a3, a2, a1, a0, b4, b3, b2, b1, b0])

        b00, b10, b20, b30, b40 = b0, b1, b2, b3, b4

    return {
        "coeffs": np.array(table, dtype=np.float32),
        "num_segments": num_segments,
        "xmax": xmax,
        "dx": dx,
        "ymin": ymin,
        "ymax": ymax,
    }


def adnl_set_glf(k0, b, type_b, kloop, xmax=15.0, dx=0.02):
    """Build the Keller ADNL table from a GLF curve (the v1 / behavioral path)."""
    # Keller uses dx1 = dx / 3 for the internal high-res grid
    dx1 = dx / 3.0
    x_internal = np.arange(-xmax, xmax + dx, dx1)
    f_internal = glf(x_internal, k0, b, type_b)

    if kloop > 0:
        # Closed-loop resampling: x_ext_norm = (x_internal + kloop * f_internal) / (kloop + 1)
        x_ext_norm = (x_internal + kloop * f_internal) / (kloop + 1.0)
        x_target = np.arange(-xmax, xmax + dx, dx1)
        f_closed = np.interp(x_target, x_ext_norm, f_internal, left=-k0, right=1.0 - k0)
    else:
        f_closed = f_internal

    return _curve_to_adnl_table(f_closed, xmax, dx, ymin=-k0, ymax=1.0 - k0)


# ---------------------------------------------------------------------------
# Dempwolf-Zölzer ECC83 / 12AX7 triode model
# ---------------------------------------------------------------------------
#
# Reference: K. Dempwolf and U. Zölzer, "A Physically-Motivated Triode Model
# for Circuit Simulations", DAFx-11.  Parameters and Jacobian taken directly
# from Jaromir Macák's NodalDKFramework (triode.m, ecc83_tube_model).
#
# Sign convention matches the MATLAB reference: ig and ip are returned with
# negative sign for the standard "current flows from plate to cathode" flow,
# i.e. ip < 0 means quiescent conduction.  Downstream callers either flip the
# sign when wiring against Keller's positive-current convention, or work with
# magnitudes (|ip|) directly.

# DZ ECC83 model parameters — Macák triode.m:35-58 lines.
DZ_ECC83 = {
    "Gg": 606e-6,
    "xi": 1.354,
    "Cg": 13.9,
    "Gp": 2.14e-3,
    "gamma": 1.303,
    "Cp": 3.04,
    "mu": 100.8,
}


def _softplus(x):
    """Numerically stable log(1+exp(x))."""
    # For x large positive, log(1+exp(x)) ≈ x; for x very negative, ≈ exp(x).
    return np.where(x > 30.0, x, np.log1p(np.exp(np.minimum(x, 30.0))))


def triode_dz_ecc83(vgk, vpk, params=DZ_ECC83):
    """Dempwolf-Zölzer ECC83 currents.

    Returns ``(ig, ip)`` matching the MATLAB sign convention (ig <= 0,
    ip <= 0 in conduction).  Both inputs may be scalars or NumPy arrays.
    """
    Gg = params["Gg"]
    xi = params["xi"]
    Cg = params["Cg"]
    Gp = params["Gp"]
    gamma = params["gamma"]
    Cp = params["Cp"]
    mu = params["mu"]

    # ig: grid-cathode diode (asymmetric, very stiff turn-on near vgk = 0).
    sg = _softplus(Cg * vgk) / Cg
    ig = -Gg * np.power(np.maximum(sg, 0.0), xi)

    # ip: plate current = -Gp*(softplus(Cp*(vpk/mu+vgk))/Cp)^gamma minus ig.
    sp = _softplus(Cp * (vpk / mu + vgk)) / Cp
    ip = -Gp * np.power(np.maximum(sp, 0.0), gamma) - ig

    return ig, ip


def triode_dz_ecc83_jac(vgk, vpk, params=DZ_ECC83):
    """Jacobian of the DZ ECC83 model.

    Returns ``(dig_dvgk, dig_dvpk, dip_dvgk, dip_dvpk)`` for scalar inputs.
    """
    Gg = params["Gg"]
    xi = params["xi"]
    Cg = params["Cg"]
    Gp = params["Gp"]
    gamma = params["gamma"]
    Cp = params["Cp"]
    mu = params["mu"]

    # ig branch
    expg_arg = Cg * vgk
    if expg_arg > 30.0:
        sg = vgk
        sigmoid_g = 1.0
    else:
        sg = np.log1p(np.exp(expg_arg)) / Cg
        sigmoid_g = 1.0 / (1.0 + np.exp(-expg_arg))
    if sg <= 0.0:
        dig_dvgk = 0.0
    else:
        dig_dvgk = -Gg * xi * (sg ** (xi - 1.0)) * sigmoid_g
    dig_dvpk = 0.0

    # ip branch — d/dv softplus(C v)/C = sigmoid(C v)
    expp_arg = Cp * (vpk / mu + vgk)
    if expp_arg > 30.0:
        sp = vpk / mu + vgk
        sigmoid_p = 1.0
    else:
        sp = np.log1p(np.exp(expp_arg)) / Cp
        sigmoid_p = 1.0 / (1.0 + np.exp(-expp_arg))
    if sp <= 0.0:
        dip_part_dvgk = 0.0
    else:
        dip_part_dvgk = -Gp * gamma * (sp ** (gamma - 1.0)) * sigmoid_p
    dip_dvgk = dip_part_dvgk - dig_dvgk
    dip_dvpk = dip_part_dvgk / mu

    return dig_dvgk, dig_dvpk, dip_dvgk, dip_dvpk


# ---------------------------------------------------------------------------
# DC load-line solver — DZ triode in a Keller CK / CD / CC topology
# ---------------------------------------------------------------------------

def loadline_solve_ck(vin, vs, ra, rl, rk, params=DZ_ECC83,
                      tol=1e-9, max_iter=80,
                      ip_init=0.0, ig_init=0.0, alpha=0.5):
    """Solve the DC operating point of a common-cathode triode stage.

    Topology (DZ in Keller's CK slot, anode loaded by rl to vs, cathode bypassed
    in the small-signal sense but DC-coupled through rk):

        vp = vs - |ip| * rl + ip_grid_correction
        vk = (|ip| + |ig|) * rk
        vgk = vin - vk
        vpk = vp - vk

    We Newton-iterate on the unknown ``ip`` (the plate current magnitude).
    ``ra`` is the small-signal plate resistance and is NOT used in the DC solve
    (it's already implicit in the DZ model); it's only kept in the signature
    for symmetry with Keller's tube_ck_set call site.

    ``ip_init`` / ``ig_init`` allow warm-starting from a previous solve;
    callers sweeping a vin grid should pass the previous result for stability
    in the high-positive-grid region (where naive ``ip=0`` start can oscillate
    between cutoff and saturation).  ``alpha`` controls the damped fixed-point
    mixing rate.
    """
    ip = float(ip_init)
    ig = float(ig_init)
    vk = 0.0
    vp = vs
    for _ in range(max_iter):
        vk = (-ip - ig) * rk  # ip, ig are negative in DZ convention -> vk >= 0
        vp = vs - (-ip) * rl
        vgk = vin - vk
        vpk = vp - vk
        ig_new, ip_new = triode_dz_ecc83(vgk, vpk, params)
        if abs(ip - ip_new) < tol:
            ip = ip_new
            ig = ig_new
            break
        # Damped fixed-point step.
        ip = (1.0 - alpha) * ip + alpha * ip_new
        ig = ig_new
    return ip, ig, vp, vk


def loadline_curve_ck(vs, ra, rl, rk, isat, ibias, vin_grid, params=DZ_ECC83):
    """Compute the static plate-current curve over an array of input voltages.

    Sweeps the grid using a warm-started fixed-point solver: the iteration
    is seeded with the previous solve's result and uses a stronger damping
    factor (alpha=0.1) once the sweep moves away from the quiescent point.
    This avoids the bistable oscillation the plain ip=0 / alpha=0.5 solver
    suffers at high positive vgk in low-headroom topologies (e.g. cathodyne).

    Returns ``ip_arr`` (shape == vin_grid.shape, DZ sign: ip <= 0) and the
    corresponding ``vk_arr`` and ``vp_arr`` for diagnostics / plotting.
    """
    n = len(vin_grid)
    ip_arr = np.zeros(n)
    vp_arr = np.zeros(n)
    vk_arr = np.zeros(n)

    # Solve at the closest-to-zero grid point first (canonical quiescent),
    # then sweep outward in both directions warm-starting from the previous
    # neighbour's result.
    mid = int(np.argmin(np.abs(vin_grid)))
    ip0, ig0, vp0, vk0 = loadline_solve_ck(
        float(vin_grid[mid]), vs, ra, rl, rk, params, alpha=0.5)
    ip_arr[mid] = ip0
    vp_arr[mid] = vp0
    vk_arr[mid] = vk0

    ip, ig = ip0, ig0
    for i in range(mid + 1, n):
        ip, ig, vp, vk = loadline_solve_ck(
            float(vin_grid[i]), vs, ra, rl, rk, params,
            ip_init=ip, ig_init=ig, alpha=0.1, max_iter=300)
        ip_arr[i] = ip
        vp_arr[i] = vp
        vk_arr[i] = vk

    ip, ig = ip0, ig0
    for i in range(mid - 1, -1, -1):
        ip, ig, vp, vk = loadline_solve_ck(
            float(vin_grid[i]), vs, ra, rl, rk, params,
            ip_init=ip, ig_init=ig, alpha=0.1, max_iter=300)
        ip_arr[i] = ip
        vp_arr[i] = vp
        vk_arr[i] = vk

    return ip_arr, vp_arr, vk_arr


def loadline_solve_cd(vin, vs, ra, rl, rk, params=DZ_ECC83,
                      tol=1e-9, max_iter=80,
                      ip_init=0.0, ig_init=0.0, alpha=0.5):
    """Solve the DC operating point of a cathodyne (split-load) triode stage.

    Topology: rl on the plate, rk on the cathode (rk == rl typically).
    Output is taken differentially across rl (anode) and rk (cathode).

    Same warm-start interface as :func:`loadline_solve_ck`; callers sweeping
    a grid should use :func:`loadline_curve_cd` (which warm-starts properly)
    rather than calling this directly.
    """
    ip = float(ip_init)
    ig = float(ig_init)
    vk = 0.0
    vp = vs
    for _ in range(max_iter):
        vk = (-ip - ig) * rk
        vp = vs - (-ip) * rl
        vgk = vin - vk
        vpk = vp - vk
        ig_new, ip_new = triode_dz_ecc83(vgk, vpk, params)
        if abs(ip - ip_new) < tol:
            ip = ip_new
            ig = ig_new
            break
        ip = (1.0 - alpha) * ip + alpha * ip_new
        ig = ig_new
    return ip, ig, vp, vk


def loadline_curve_cd(vs, ra, rl, rk, vin_grid, params=DZ_ECC83):
    """Sweep a vin grid through a cathodyne load-line, warm-starting.

    Mirrors :func:`loadline_curve_ck`'s sweep strategy.  Returns
    ``(ip_arr, ig_arr, vp_arr, vk_arr)`` (DZ sign convention: ip, ig <= 0).
    """
    n = len(vin_grid)
    ip_arr = np.zeros(n)
    ig_arr = np.zeros(n)
    vp_arr = np.zeros(n)
    vk_arr = np.zeros(n)

    mid = int(np.argmin(np.abs(vin_grid)))
    ip0, ig0, vp0, vk0 = loadline_solve_cd(
        float(vin_grid[mid]), vs, ra, rl, rk, params, alpha=0.5)
    ip_arr[mid] = ip0
    ig_arr[mid] = ig0
    vp_arr[mid] = vp0
    vk_arr[mid] = vk0

    ip, ig = ip0, ig0
    for i in range(mid + 1, n):
        ip, ig, vp, vk = loadline_solve_cd(
            float(vin_grid[i]), vs, ra, rl, rk, params,
            ip_init=ip, ig_init=ig, alpha=0.1, max_iter=300)
        ip_arr[i] = ip; ig_arr[i] = ig
        vp_arr[i] = vp; vk_arr[i] = vk

    ip, ig = ip0, ig0
    for i in range(mid - 1, -1, -1):
        ip, ig, vp, vk = loadline_solve_cd(
            float(vin_grid[i]), vs, ra, rl, rk, params,
            ip_init=ip, ig_init=ig, alpha=0.1, max_iter=300)
        ip_arr[i] = ip; ig_arr[i] = ig
        vp_arr[i] = vp; vk_arr[i] = vk

    return ip_arr, ig_arr, vp_arr, vk_arr


def adnl_set_dz_ck(vs, ra, rl, rk, isat, ibias, kpre, kloop_ignored=None,
                   xmax=15.0, dx=0.02, params=DZ_ECC83):
    """Build a Keller-format ADNL table whose curve is derived from the DZ
    ECC83 model + DC load-line of a common-cathode stage.

    The output is normalized to match Keller's convention exactly:

      * x-axis is normalized grid-drive ``x = kpre * vin``.
      * y-axis is the plate-current excursion from the DZ-derived quiescent,
        normalized by ``isat``: ``y = (|ip(vin)| - |ip(0)|) / isat``.
        At ``vin = 0`` this gives ``y = 0`` exactly (matching Keller's
        ``glf(0) = 0`` convention), so downstream Keller wiring (``*isat`` then
        ``+ksib*dvs``) keeps producing ``dia = ip - ibias_effective``.

    Note: Keller's ``ibias`` parameter is used only to define ``kbias`` for
    the table's nominal ``ymin/ymax``; the actual quiescent comes from the
    DZ load-line solve and may differ (typically by 5–25%).  This is the
    intended physical correction: a real ECC83 doesn't sit at exactly the
    Keller-fitted bias.

    The internal CK feedback loop (cathode self-bias) is already baked into
    the load-line solve, so ``kloop`` is not needed here — pass None or
    Keller's value (it's ignored, kept for call-site symmetry).
    """
    dx1 = dx / 3.0
    x_norm_grid = np.arange(-xmax, xmax + dx, dx1)
    vin_grid = x_norm_grid / kpre
    ip_arr, _vp, _vk = loadline_curve_ck(vs, ra, rl, rk, isat, ibias, vin_grid, params)

    # Center on the DZ-derived quiescent so f(x=0) = 0 (matches GLF convention).
    # |ip| is in amps; subtract the value at x=0 (closest grid index).
    ip_pos = -ip_arr  # to positive (Keller convention)
    zero_idx = int(np.argmin(np.abs(x_norm_grid)))
    ip_q = ip_pos[zero_idx]
    f_raw = (ip_pos - ip_q) / isat

    # Clip to GLF saturation range [-kbias_actual, 1-kbias_actual].  See
    # gen_tables.gen_adnl_table_dz_ck for rationale: the bare DZ model has
    # no plate-bottoming mechanism and produces unphysical ip ≫ isat at
    # large positive grid drive; the real-world clamp comes from grid current
    # at the previous coupling cap, modelled by Keller's PKD path.
    kbias_actual = ip_q / isat
    ymin = float(-kbias_actual)
    ymax = float(1.0 - kbias_actual)
    f_closed = np.clip(f_raw, ymin, ymax)
    return _curve_to_adnl_table(f_closed, xmax, dx, ymin=ymin, ymax=ymax)


def adnl_set_dz_cd(vs, ra, rl, rk, isat, ibias, kpre,
                   xmax=15.0, dx=0.02, params=DZ_ECC83):
    """Build a Keller-format ADNL table for a DZ-modelled cathodyne stage.

    Same normalization convention as :func:`adnl_set_dz_ck`.
    """
    dx1 = dx / 3.0
    x_norm_grid = np.arange(-xmax, xmax + dx, dx1)
    vin_grid = x_norm_grid / kpre
    n = len(vin_grid)
    ip_arr = np.zeros(n)
    for i, vin in enumerate(vin_grid):
        ip, _ig, _vp, _vk = loadline_solve_cd(float(vin), vs, ra, rl, rk, params)
        ip_arr[i] = ip
    ip_pos = -ip_arr
    zero_idx = int(np.argmin(np.abs(x_norm_grid)))
    ip_q = ip_pos[zero_idx]
    f_raw = (ip_pos - ip_q) / isat
    kbias_actual = ip_q / isat
    ymin = float(-kbias_actual)
    ymax = float(1.0 - kbias_actual)
    f_closed = np.clip(f_raw, ymin, ymax)
    return _curve_to_adnl_table(f_closed, xmax, dx, ymin=ymin, ymax=ymax)


def _adnl_eval(table, x):
    """Evaluate Keller's ADNL at a single sample x (scalar)."""
    coeffs = table["coeffs"]
    num_segments = table["num_segments"]
    xmax = table["xmax"]
    dx = table["dx"]
    ymin = table["ymin"]
    ymax = table["ymax"]

    # Runtime clamp + index
    if x <= -xmax:
        return ymin
    if x >= xmax:
        return ymax

    raw_index = int((x + xmax) / dx)
    index = min(num_segments - 1, max(0, raw_index))
    w = x + xmax - float(index) * dx

    ab = coeffs[index]
    a3, a2, a1, a0, b4, b3, b2, b1, b0 = ab

    y1 = (((a3 * w + a2) * w + a1) * w + a0)
    z1 = ((((b4 * w + b3) * w + b2) * w + b1) * w + b0)

    # For ADAA, caller needs both y1 and z1 — this helper returns both
    return y1, z1


class AdnlProcessor:
    """Stateful ADNL processor that implements Keller's ADAA runtime."""

    def __init__(self, table):
        self.table = table
        self.x_prev = 0.0
        idx_zero = int(table["xmax"] / table["dx"])
        zero_row = table["coeffs"][idx_zero]
        self.y_prev = 0.0
        self.z_prev = float(zero_row[8])

    def process_block(self, x_buf):
        """Process a 1-D NumPy array."""
        out = np.empty_like(x_buf)
        coeffs = self.table["coeffs"]
        num_segments = self.table["num_segments"]
        xmax = self.table["xmax"]
        dx = self.table["dx"]
        ymin = self.table["ymin"]
        ymax = self.table["ymax"]

        for n in range(len(x_buf)):
            x = float(x_buf[n])

            # --- direct evaluation (same as _adnl_eval but inlined for speed) ---
            if x <= -xmax:
                y1 = ymin
                z1 = ymin * (x + xmax)
            elif x >= xmax:
                # last-base z_at_xmax; keep this close to the runtime logic.
                last_base = num_segments - 1
                ab = coeffs[last_base]
                b4, b3, b2, b1, b0 = ab[4], ab[5], ab[6], ab[7], ab[8]
                z_at_xmax = ((((b4 * dx + b3) * dx + b2) * dx + b1) * dx + b0)
                y1 = ymax
                z1 = z_at_xmax + ymax * (x - xmax)
            else:
                raw_index = int((x + xmax) / dx)
                index = min(num_segments - 1, max(0, raw_index))
                w = x + xmax - float(index) * dx
                ab = coeffs[index]
                a3, a2, a1, a0, b4, b3, b2, b1, b0 = (float(v) for v in ab)
                y1 = (((a3 * w + a2) * w + a1) * w + a0)
                z1 = ((((b4 * w + b3) * w + b2) * w + b1) * w + b0)
                if x == 0.0:
                    y1 = 0.0

            # --- ADAA ---
            # Keller's JSFX (HK_LIB_ADNL.jsfx-inc:245):
            #   reldx0 < 1e-4   -> 0.5 * (y1 + y0)   (avg / limit form)
            #   reldx0 >= 1e-4  -> (z1 - z0) / dx0   (antiderivative quotient)
            # The native tables store float coefficients, so tiny absolute
            # steps also use the stable limit form to avoid near-zero
            # antiderivative cancellation.
            dx0 = x - self.x_prev
            reldx0 = abs(dx0) / (abs(x + self.x_prev) + 1e-7)

            z0 = self.z_prev
            y0 = self.y_prev

            if reldx0 < 0.0001 or abs(dx0) < 0.001:
                adaa_result = 0.5 * (y1 + y0)
            else:
                # safe_dx0 guards against the (theoretical) reldx0 >= 1e-4
                # but dx0 == 0 case.
                safe_dx0 = dx0 if dx0 != 0.0 else 1e-20
                adaa_result = (z1 - z0) / safe_dx0

            out[n] = adaa_result

            self.x_prev = x
            self.z_prev = z1
            self.y_prev = y1

        return out


# ---------------------------------------------------------------------------
# PKD — peak detector
# ---------------------------------------------------------------------------

def pkd_process_block(xth, xdiode, k1, k2, x_buf):
    """Keller peak detector, translated from HK_LIB_PKD.jsfx-inc.

    xth      — threshold
    xdiode   — diode knee width
    k1       — attack coefficient (from pkd_k1)
    k2       — release coefficient (from pkd_k2)
    """
    eps = 1e-10
    xd = max(eps, xdiode)
    out = np.empty_like(x_buf)
    s1 = 0.0
    s2 = 0.0
    for n in range(len(x_buf)):
        x = x_buf[n]
        # diode region
        if x <= xth:
            x_val = 0.0
        elif x >= xth + 2.0 * xd:
            x_val = (x - xth) - xd
        else:
            x_val = 0.25 * (x - xth) * (x - xth) / xd
        # attack LP
        s1 = (x_val - s1) * k1 + s1
        # max release
        s2 = max(s1, k2 * s2)
        out[n] = s2
    return out


def pkd_k1(tau_attack, sr):
    """Attack coefficient."""
    if tau_attack > 0:
        return 1.0 - np.exp(-1.0 / (tau_attack * sr))
    else:
        return 1.0


def pkd_k2(tau_release, sr):
    """Release coefficient."""
    if tau_release > 0:
        return np.exp(-1.0 / (tau_release * sr))
    else:
        return 0.0


# ---------------------------------------------------------------------------
# Filters — Keller's HK_LIB_FLT_II / HK_LIB_FLT_SV
# ---------------------------------------------------------------------------
#
# Direct sample-by-sample ports of the JSFX implementations so we can pin
# the native C filters against an oracle that doesn't depend on scipy.signal.
# All functions take a numpy float32 input buffer and return a same-length
# float32 output buffer.

class FltIi1Lp:
    """Stateful 1st-order impulse-invariant LP."""

    def __init__(self, f, sr):
        self.k = 1.0 - np.exp(-2.0 * np.pi * f / sr)
        self.s1 = 0.0

    def process_sample(self, x):
        self.s1 = (float(x) - self.s1) * self.k + self.s1
        return self.s1


class FltIi1Hp:
    """Stateful 1st-order HP: input minus matching impulse-invariant LP."""

    def __init__(self, f, sr):
        self.lp = FltIi1Lp(f, sr)

    def process_sample(self, x):
        lp_y = np.float32(self.lp.process_sample(x))
        return np.float32(np.float32(x) - lp_y)


def flt_ii1_lp_block(f, sr, x_buf):
    """1st-order impulse-invariant LP.  See HK_LIB_FLT_II.jsfx-inc:32."""
    flt = FltIi1Lp(f, sr)
    out = np.zeros_like(x_buf, dtype=np.float32)
    for i, x in enumerate(x_buf):
        out[i] = flt.process_sample(x)
    return out


def flt_ii1_hp_block(f, sr, x_buf):
    """1st-order impulse-invariant HP — input minus the matching LP."""
    flt = FltIi1Hp(f, sr)
    out = np.zeros_like(x_buf, dtype=np.float32)
    for i, x in enumerate(x_buf):
        out[i] = flt.process_sample(x)
    return out


class FltSv2Tst:
    """Stateful 2nd-order TPT SVF used as a tonestack.

    See HK_LIB_FLT_SV.jsfx-inc:127 (flt_sv2_set_tst) and :200 (process).
    Output mix is t*hp + (kq*m)*bp + b*lp.

    pwf, pwQ: 0 = no prewarp, 1 = tan-prewarp.  We ignore the JSFX
    smoothing layer (sm=0 equivalent): coefficients are computed once.
    """

    def __init__(self, b, m, t, f, Q, pwf, pwQ, sr):
        pi_t = np.pi / sr
        k = f * pi_t
        if pwQ == 0:
            kq = 1.0 / Q
        else:
            aux1 = np.sqrt(1.0 + 4.0 * Q * Q)
            aux2 = (k / np.sin(2.0 * k)) * np.log((aux1 + 1.0) / (aux1 - 1.0))
            kq = np.exp(aux2) - np.exp(-aux2)
        if pwf == 1:
            k = np.tan(f * pi_t)
        self.k = k
        self.kf = kq + k
        self.kdiv = 1.0 / (1.0 + k * (k + kq))
        self.b0 = t
        self.kb1 = kq * m
        self.b2 = b
        self.s1 = 0.0
        self.s2 = 0.0

    def process_sample(self, x):
        hp = (float(x) - self.kf * self.s1 - self.s2) * self.kdiv
        aux = self.k * hp
        bp = aux + self.s1
        self.s1 = aux + bp
        aux = self.k * bp
        lp = aux + self.s2
        self.s2 = aux + lp
        return self.b0 * hp + self.kb1 * bp + self.b2 * lp


def flt_sv2_tst_block(b, m, t, f, Q, pwf, pwQ, sr, x_buf):
    """Block wrapper for :class:`FltSv2Tst`."""
    flt = FltSv2Tst(b, m, t, f, Q, pwf, pwQ, sr)
    out = np.zeros_like(x_buf, dtype=np.float32)
    for i, x in enumerate(x_buf):
        out[i] = flt.process_sample(x)
    return out


class FltSv1Hs:
    """Stateful 1st-order TPT high shelf."""

    def __init__(self, kgain, fs, pwf, sr):
        k_raw = fs * np.pi / sr
        if pwf == 1:
            k_raw = np.tan(fs * np.pi / sr)
        self.kgain = kgain
        self.k = np.sqrt(kgain) * k_raw
        self.kdiv = 1.0 / (1.0 + self.k)
        self.s1 = 0.0

    def process_sample(self, x):
        hp = (float(x) - self.s1) * self.kdiv
        aux = self.k * hp
        lp = aux + self.s1
        self.s1 = aux + lp
        return hp * self.kgain + lp


def flt_sv1_hs_block(kgain, fs, pwf, sr, x_buf):
    """1st-order TPT high shelf. See HK_LIB_FLT_SV.jsfx-inc flt_sv1_set_hs."""
    flt = FltSv1Hs(kgain, fs, pwf, sr)
    out = np.zeros_like(x_buf, dtype=np.float32)
    for i, x in enumerate(x_buf):
        out[i] = flt.process_sample(x)
    return out


class FltSv2Peq:
    """Stateful 2nd-order TPT peak EQ."""

    def __init__(self, kgain, f, Qc, pwf, pwQ, sr):
        pi_t = np.pi / sr
        k_for_q = f * pi_t
        if pwQ == 0:
            kq = 1.0 / (np.sqrt(kgain) * Qc)
        else:
            q_eff = Qc * np.sqrt(kgain)
            aux1 = np.sqrt(1.0 + 4.0 * q_eff * q_eff)
            aux2 = (k_for_q / np.sin(2.0 * k_for_q)) * np.log(
                (aux1 + 1.0) / (aux1 - 1.0)
            )
            kq = np.exp(aux2) - np.exp(-aux2)
        k = f * pi_t
        if pwf == 1:
            k = np.tan(f * pi_t)
        self.k = k
        self.kq = kq
        self.kgain = kgain
        self.kf = kq + k
        self.kdiv = 1.0 / (1.0 + k * (k + kq))
        self.s1 = 0.0
        self.s2 = 0.0

    def process_sample(self, x):
        hp = (float(x) - self.kf * self.s1 - self.s2) * self.kdiv
        aux = self.k * hp
        bp = aux + self.s1
        self.s1 = aux + bp
        aux = self.k * bp
        lp = aux + self.s2
        self.s2 = aux + lp
        return hp + (self.kq * self.kgain) * bp + lp


def flt_sv2_peq_block(kgain, f, Qc, pwf, pwQ, sr, x_buf):
    """2nd-order TPT peak EQ. See HK_LIB_FLT_SV.jsfx-inc flt_sv2_set_peq."""
    flt = FltSv2Peq(kgain, f, Qc, pwf, pwQ, sr)
    out = np.zeros_like(x_buf, dtype=np.float32)
    for i, x in enumerate(x_buf):
        out[i] = flt.process_sample(x)
    return out


# ---------------------------------------------------------------------------
# Tube stage — common cathode (CK)
# ---------------------------------------------------------------------------

class TubeCk:
    """Common-cathode tube stage with local-feedback loop."""

    def __init__(self, kpre, isat, rl, kpk, kspre, kspost, ksva, ksib, kfb,
                 pk_xth, pk_xdiode, pk_k1, pk_k2, avg_f,
                 neq_b0, neq_b1, neq_b2, neq_a1, neq_a2,
                 sr, adnl_table):
        self.kpre = kpre
        self.isat = isat
        self.rl = rl
        self.kpk = kpk
        self.kspre = kspre
        self.kspost = kspost
        self.ksva = ksva
        self.ksib = ksib
        self.kfb = kfb

        self.pk_xth = pk_xth
        self.pk_xdiode = pk_xdiode
        self.pk_k1 = pk_k1
        self.pk_k2 = pk_k2
        self.avg_f = avg_f

        self.neq_b0 = neq_b0
        self.neq_b1 = neq_b1
        self.neq_b2 = neq_b2
        self.neq_a1 = neq_a1
        self.neq_a2 = neq_a2

        self.sr = sr
        self.adnl = AdnlProcessor(adnl_table)

        # state variables
        self.advk = 0.0
        self.pk_s1 = 0.0
        self.pk_s2 = 0.0
        self.eq_s1 = 0.0
        self.eq_s2 = 0.0
        self.avg_s = 0.0

    def _pkd(self, x):
        """One-sample peak detector."""
        xth = self.pk_xth
        xd = max(1e-10, self.pk_xdiode)
        if x <= xth:
            x_val = 0.0
        elif x >= xth + 2.0 * xd:
            x_val = (x - xth) - xd
        else:
            x_val = 0.25 * (x - xth) * (x - xth) / xd
        self.pk_s1 = (x_val - self.pk_s1) * self.pk_k1 + self.pk_s1
        self.pk_s2 = max(self.pk_s1, self.pk_k2 * self.pk_s2)
        return self.pk_s2

    def _avg_lp(self, x):
        """Impulse-invariant 1st-order LP."""
        k = 1.0 - np.exp(-2.0 * np.pi * self.avg_f / self.sr)
        self.avg_s = (x - self.avg_s) * k + self.avg_s
        return self.avg_s

    def _eq(self, x):
        """Direct Form 2 biquad."""
        b0, b1, b2 = self.neq_b0, self.neq_b1, self.neq_b2
        a1, a2 = self.neq_a1, self.neq_a2
        # Standard DF2: y = b0*x + b1*s1 + b2*s2 - a1*y1 - a2*y2
        # Using state form: s1, s2
        y = b0 * x + self.eq_s1
        self.eq_s1 = b1 * x - a1 * y + self.eq_s2
        self.eq_s2 = b2 * x - a2 * y
        return y

    def process_sample(self, v, dvs):
        """Process one sample.  Returns (v_out, dia)."""
        v1 = v + self.advk
        v2 = v1 * self.kpre
        v3 = v2 / (1.0 + self.kspre * dvs)
        v4 = v3 - self.kpk * self._pkd(v3)
        v5 = self.adnl.process_block(np.array([v4], dtype=np.float32))[0]
        v6 = self._eq(v5)
        v7 = v6 * (1.0 + self.kspost * dvs)
        v8 = v7 * self.isat
        dia = v8 + self.ksib * dvs
        v_out = dia * (-self.rl) + self.ksva * dvs
        self.advk = self._avg_lp(v_out - dvs) * self.kfb
        return v_out, dia

    def process_block(self, v_buf, dvs_buf):
        """Process full buffers (NumPy 1-D arrays)."""
        n = len(v_buf)
        v_out = np.empty(n, dtype=np.float32)
        dia = np.empty(n, dtype=np.float32)
        for i in range(n):
            v_out[i], dia[i] = self.process_sample(v_buf[i], dvs_buf[i])
        return v_out, dia


# ---------------------------------------------------------------------------
# Tube stage — cathodyne (CD)
# ---------------------------------------------------------------------------

class TubeCd:
    """Cathodyne tube stage (split-load / phase inverter)."""

    def __init__(self, kpre, isat, rl, rkl, kpk, kspre, kspost, ksva, ksvk, ksib,
                 pk_xth, pk_xdiode, pk_k1, pk_k2,
                 neq_b0, neq_b1, neq_b2, neq_a1, neq_a2,
                 sr, adnl_table):
        self.kpre = kpre
        self.isat = isat
        self.rl = rl
        self.rkl = rkl
        self.kpk = kpk
        self.kspre = kspre
        self.kspost = kspost
        self.ksva = ksva
        self.ksvk = ksvk
        self.ksib = ksib

        self.pk_xth = pk_xth
        self.pk_xdiode = pk_xdiode
        self.pk_k1 = pk_k1
        self.pk_k2 = pk_k2

        self.neq_b0 = neq_b0
        self.neq_b1 = neq_b1
        self.neq_b2 = neq_b2
        self.neq_a1 = neq_a1
        self.neq_a2 = neq_a2

        self.sr = sr
        self.adnl = AdnlProcessor(adnl_table)

        # state
        self.pk_s1 = 0.0
        self.pk_s2 = 0.0
        self.eq_s1 = 0.0
        self.eq_s2 = 0.0

    def _pkd(self, x):
        xth = self.pk_xth
        xd = max(1e-10, self.pk_xdiode)
        if x <= xth:
            x_val = 0.0
        elif x >= xth + 2.0 * xd:
            x_val = (x - xth) - xd
        else:
            x_val = 0.25 * (x - xth) * (x - xth) / xd
        self.pk_s1 = (x_val - self.pk_s1) * self.pk_k1 + self.pk_s1
        self.pk_s2 = max(self.pk_s1, self.pk_k2 * self.pk_s2)
        return self.pk_s2

    def _eq(self, x):
        b0, b1, b2 = self.neq_b0, self.neq_b1, self.neq_b2
        a1, a2 = self.neq_a1, self.neq_a2
        y = b0 * x + self.eq_s1
        self.eq_s1 = b1 * x - a1 * y + self.eq_s2
        self.eq_s2 = b2 * x - a2 * y
        return y

    def process_sample(self, v, dvs):
        v2 = v * self.kpre
        v3 = v2 / (1.0 + self.kspre * dvs)
        v4 = v3 - self.kpk * self._pkd(v3)
        v5 = self.adnl.process_block(np.array([v4], dtype=np.float32))[0]
        v6 = self._eq(v5)
        v7 = v6 * (1.0 + self.kspost * dvs)
        v8 = v7 * self.isat
        dia = v8 + self.ksib * dvs
        vk_out = dia * self.rkl + self.ksvk * dvs
        v_out = dia * (-self.rl) + self.ksva * dvs
        return v_out, vk_out, dia

    def process_block(self, v_buf, dvs_buf):
        n = len(v_buf)
        v_out = np.empty(n, dtype=np.float32)
        vk_out = np.empty(n, dtype=np.float32)
        dia = np.empty(n, dtype=np.float32)
        for i in range(n):
            v_out[i], vk_out[i], dia[i] = self.process_sample(v_buf[i], dvs_buf[i])
        return v_out, vk_out, dia


# ---------------------------------------------------------------------------
# PSU sag — tube_pss
# ---------------------------------------------------------------------------

class TubePss:
    """Power supply sag stage (single RC stage)."""

    def __init__(self, r, tau, sr):
        self.r = r
        self.tau = tau
        self.sr = sr
        self.s = 0.0
        self.dvs_s = 0.0

    def process_sample(self, dia, snext, dvs_in):
        k = 1.0 - np.exp(-1.0 / (max(1e-10, self.tau) * self.sr))
        self.s = (dia + snext - self.s) * k + self.s
        self.dvs_s = (dvs_in - self.dvs_s) * k + self.dvs_s
        dvs_out = self.dvs_s - self.r * self.s
        return dvs_out, self.s

    def process_block(self, dia_buf, snext_buf, dvs_in_buf):
        n = len(dia_buf)
        dvs_out = np.empty(n, dtype=np.float32)
        s_buf = np.empty(n, dtype=np.float32)
        for i in range(n):
            dvs_out[i], s_buf[i] = self.process_sample(dia_buf[i], snext_buf[i], dvs_in_buf[i])
        return dvs_out, s_buf


# ---------------------------------------------------------------------------
# Self-test — invoked via `python -m tools.keller_oracle`
# ---------------------------------------------------------------------------

def _self_test():
    """Smoke-test the oracle primitives on a 1 kHz sine."""
    sr = 48000
    n = 4800
    t = np.arange(n) / sr
    x = (0.5 * np.sin(2 * np.pi * 1000.0 * t)).astype(np.float32)

    # ADNL (GLF, type B, with light feedback resampling)
    spec = adnl_set_glf(0.5, 1.5, 1, 4.0)
    assert spec["coeffs"].shape == (spec["num_segments"], 9)
    adnl = AdnlProcessor(spec)
    y_adnl = adnl.process_block(x.copy())
    assert np.all(np.isfinite(y_adnl)), "ADNL produced non-finite output"
    rms_adnl = float(np.sqrt(np.mean(y_adnl * y_adnl)))

    # PKD (peak detector, fast attack / slow release)
    k1 = pkd_k1(0.001, sr)
    k2 = pkd_k2(0.05, sr)
    y_pk = pkd_process_block(0.0, 0.001, k1, k2, x.copy())
    assert np.all(np.isfinite(y_pk)), "PKD produced non-finite output"
    rms_pk = float(np.sqrt(np.mean(y_pk * y_pk)))

    # ---- DZ ECC83 model ----
    # Reference values from MATLAB triode.m at vgk = -1.5 V, vpk = 250 V.
    # Hand-calc (Gp=2.14e-3, Cp=3.04, gamma=1.303, mu=100.8, Cg=13.9):
    #   sp = log1p(exp(3.04*(250/100.8 - 1.5)))/3.04 ≈ 0.978
    #   ip ≈ -2.14e-3 * 0.978^1.303 ≈ -2.07e-3 A   (dominated by the ip term;
    #   ig ≈ -Gg*log1p(exp(-20.85))^xi/Cg^xi → ~0).
    ig_ref, ip_ref = triode_dz_ecc83(-1.5, 250.0)
    assert abs(ig_ref) < 1e-12, f"DZ ig at vgk=-1.5V should be ~0, got {ig_ref}"
    assert -3e-3 < ip_ref < -1e-3, f"DZ ip at -1.5V/250V should be ~-2mA, got {ip_ref}"

    # Grid-current onset: at vgk = +1.0 V we should see clear ig conduction.
    ig_pos, _ = triode_dz_ecc83(1.0, 100.0)
    assert ig_pos < -1e-5, f"DZ ig at vgk=+1V should be clearly negative, got {ig_pos}"

    # ---- Load-line at T2 (12AX7) quiescent point ----
    # Keller TWD-DLX-II.jsfx:180 → mu=100, ra=62.5k, isat=1.55mA, ibias=0.76mA,
    # vs=238, rl=100k, rk=1.5k.  At vin=0 the load-line should give ip ≈ ibias.
    ip_q, _ig_q, vp_q, vk_q = loadline_solve_ck(
        vin=0.0, vs=238.0, ra=62500.0, rl=100_000.0, rk=1500.0
    )
    ibias_keller = 0.00076
    err = abs(-ip_q - ibias_keller) / ibias_keller
    assert err < 0.4, (
        f"DZ-derived T2 quiescent ip={-ip_q*1e3:.3f}mA differs from Keller's "
        f"ibias={ibias_keller*1e3:.3f}mA by {err*100:.1f}% (>40%)."
    )

    # ---- DZ table generation (T2 stage) — same dx/xmax as Keller ----
    kpre_t2 = 100.0 / 0.00155 / (62500.0 + 100_000.0 + 101.0 * 1500.0)
    spec_dz = adnl_set_dz_ck(
        vs=238.0, ra=62500.0, rl=100_000.0, rk=1500.0,
        isat=0.00155, ibias=0.00076, kpre=kpre_t2,
    )
    assert spec_dz["coeffs"].shape == (spec_dz["num_segments"], 9)
    assert np.all(np.isfinite(spec_dz["coeffs"]))
    # Sanity: DC excursion at x=0 should be exactly 0 (curve is centered there).
    # Process a long DC ramp so ADAA settles.
    dz = AdnlProcessor(spec_dz)
    y_at_zero = dz.process_block(np.zeros(64, dtype=np.float32))[-1]
    assert abs(y_at_zero) < 1e-3, f"DZ table at x=0 = {y_at_zero}, expected ~0"
    # Asymmetry sanity: positive drive should saturate (clip) before negative
    # drive runs out — confirms the DZ curve is asymmetric (unlike GLF tanh).
    y_pos = dz.process_block(np.full(64, 5.0, dtype=np.float32))[-1]
    y_neg = dz.process_block(np.full(64, -5.0, dtype=np.float32))[-1]
    asymmetry = abs(y_pos + y_neg) / max(abs(y_pos), abs(y_neg), 1e-9)
    assert asymmetry > 0.05, (
        f"DZ-derived curve is too symmetric (asymmetry={asymmetry:.3f}); "
        f"expected clear ECC83 plate-current asymmetry."
    )

    print(
        f"[oracle self-test] ADNL rms={rms_adnl:.4f}  PKD rms={rms_pk:.4f}  "
        f"DZ_ip(T2 Q)={-ip_q*1e3:.3f}mA  y0={y_at_zero:.4f}  "
        f"asym={asymmetry:.3f}  OK"
    )


if __name__ == "__main__":
    _self_test()
