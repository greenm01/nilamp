"""Keller oracle — mechanical NumPy translation of Keller's JSFX reference.

This module reproduces Keller's block-diagram + ADAA tube-amp model in NumPy
for regression testing the Faust port.  It is intentionally close-to-source,
not refactored.  Tolerance target: per-sample absolute error < 1e-3, RMS < -60 dB.
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

def adnl_set_glf(k0, b, type_b, kloop, xmax=15.0, dx=0.02):
    """Build the Keller ADNL table (cubic per segment + antiderivative coeffs).

    Returns a dict with the raw polynomial table and metadata (ymin, ymax,
    num_segments, xmax, dx) so the runtime is self-contained.
    """
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

    # append metadata at the tail so the runtime can read it back
    ymin = -k0
    ymax = 1.0 - k0
    return {
        "coeffs": np.array(table, dtype=np.float32),
        "num_segments": num_segments,
        "xmax": xmax,
        "dx": dx,
        "ymin": ymin,
        "ymax": ymax,
    }


def _adnl_eval(table, x):
    """Evaluate Keller's ADNL at a single sample x (scalar)."""
    coeffs = table["coeffs"]
    num_segments = table["num_segments"]
    xmax = table["xmax"]
    dx = table["dx"]
    ymin = table["ymin"]
    ymax = table["ymax"]

    # Faust-style clamp + index
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
        self.z_prev = 0.0
        self.y_prev = 0.0

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
            x = x_buf[n]

            # --- direct evaluation (same as _adnl_eval but inlined for speed) ---
            if x <= -xmax:
                y1 = ymin
                z1 = ymin * (x + xmax)
            elif x >= xmax:
                # last-base z_at_xmax — precompute once outside the loop if needed,
                # here keep close to Faust logic
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
                a3, a2, a1, a0, b4, b3, b2, b1, b0 = ab
                y1 = (((a3 * w + a2) * w + a1) * w + a0)
                z1 = ((((b4 * w + b3) * w + b2) * w + b1) * w + b0)

            # --- ADAA ---
            # Keller's JSFX (HK_LIB_ADNL.jsfx-inc:245):
            #   reldx0 < 1e-4   -> 0.5 * (y1 + y0)   (avg / limit form)
            #   reldx0 >= 1e-4  -> (z1 - z0) / dx0   (antiderivative quotient)
            dx0 = x - self.x_prev
            reldx0 = abs(dx0) / (abs(x + self.x_prev) + 1e-7)

            z0 = self.z_prev
            y0 = self.y_prev

            if reldx0 < 0.0001:
                adaa_result = 0.5 * (y1 + y0)
            else:
                # safe_dx0 guards against the (theoretical) reldx0 >= 1e-4
                # but dx0 == 0 case; matches Faust's safe_dx0 in hk_adnl.lib.
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
# the Faust port (dsp/hk_filters.lib) against an oracle that doesn't depend
# on scipy.signal.  All functions take a numpy float32 input buffer and
# return a same-length float32 output buffer.

def flt_ii1_lp_block(f, sr, x_buf):
    """1st-order impulse-invariant LP.  See HK_LIB_FLT_II.jsfx-inc:32."""
    k = 1.0 - np.exp(-2.0 * np.pi * f / sr)
    s1 = 0.0
    out = np.zeros_like(x_buf, dtype=np.float32)
    for i, x in enumerate(x_buf):
        s1 = (float(x) - s1) * k + s1
        out[i] = s1
    return out


def flt_ii1_hp_block(f, sr, x_buf):
    """1st-order impulse-invariant HP — input minus the matching LP."""
    return (x_buf.astype(np.float32) - flt_ii1_lp_block(f, sr, x_buf)).astype(np.float32)


def flt_sv2_tst_block(b, m, t, f, Q, pwf, pwQ, sr, x_buf):
    """2nd-order TPT SVF used as a tonestack.

    See HK_LIB_FLT_SV.jsfx-inc:127 (flt_sv2_set_tst) and :200 (process).
    Output mix is t*hp + (kq*m)*bp + b*lp.

    pwf, pwQ: 0 = no prewarp, 1 = tan-prewarp.  We ignore the JSFX
    smoothing layer (sm=0 equivalent): coefficients are computed once.
    """
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
    kf = kq + k
    kdiv = 1.0 / (1.0 + k * (k + kq))
    b0 = t
    kb1 = kq * m
    b2 = b

    s1 = 0.0
    s2 = 0.0
    out = np.zeros_like(x_buf, dtype=np.float32)
    for i, x in enumerate(x_buf):
        hp = (float(x) - kf * s1 - s2) * kdiv
        aux = k * hp
        bp = aux + s1
        s1 = aux + bp
        aux = k * bp
        lp = aux + s2
        s2 = aux + lp
        out[i] = b0 * hp + kb1 * bp + b2 * lp
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
        """Impulse-invariant 1st-order LP (same as Faust flt_ii1_lp)."""
        k = 1.0 - np.exp(-2.0 * np.pi * self.avg_f / self.sr)
        self.avg_s = (x - self.avg_s) * k + self.avg_s
        return self.avg_s

    def _eq(self, x):
        """Direct Form 2 biquad (same as Faust fi.tf2)."""
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
        v5 = self.adnl.process_block(np.array([v4]))[0]
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
        v5 = self.adnl.process_block(np.array([v4]))[0]
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

    print(f"[oracle self-test] ADNL rms={rms_adnl:.4f}  PKD rms={rms_pk:.4f}  OK")


if __name__ == "__main__":
    _self_test()
