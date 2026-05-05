# SPDX-License-Identifier: MIT
"""Generate native C ADNL tables for the 5E3 model.

This module ports Keller's ``tube_ck_set`` / ``tube_cd_set`` configuration math
(HK_LIB_TUBE.jsfx-inc) into Python so the native runtime constants and the ADNL
table coefficients stay in sync. Each ``CkConfig`` / ``CdConfig`` mirrors the
JSFX call signature one-to-one, e.g.

    t1.tube_ck_set(mu=100, ra=62500, isat=0.00165, ibias=0.00076,
                   b=0, type_b=0.5, vs=238, rl=100000, rk=1500, ...)

The script writes the ADNL polynomial tables to ``native/generated/``. The
runtime gain coefficients (``kpre``, ``ksva`` and related values) are computed
here and printed for inspection; keep the C engine constants in sync with this
output when changing stage parameters.
"""

from dataclasses import dataclass

from pathlib import Path

from gen_tables import (
    export_c_table_decl,
    export_c_table_def,
    gen_adnl_table,
    gen_adnl_table_dz_ck,
    gen_adnl_table_dz_cd,
)


@dataclass
class CkConfig:
    """Common-cathode (CK) tube stage parameters with global advk feedback.

    Mirrors ``tube_ck_set`` in HK_LIB_TUBE.jsfx-inc lines 33-79 (the
    *with-feedback* CK variant used by Keller's TWD-DLX 5E3 patch for
    every 12AX7 / 6V6 stage; t1/t2/t4/t5 in 'TWD DLX  II.jsfx').
    Distinct from ``tube_cc_set`` (line 115) which is the no-feedback
    common-cathode topology and bakes the local feedback into the GLF
    table itself (kloop > 0).

    In ``tube_ck`` the local feedback is realised at runtime via the
    ``advk = kfb * lp(v - dvs)`` averaging path (see hk_tube.lib's
    tick) so the GLF table is generated with kloop = 0 and the ``kfb``
    coefficient is exposed for the runtime.
    """

    name: str
    mu: float            # voltage gain of the tube
    ra: float            # output (plate) resistance
    isat: float          # saturation current
    ibias: float         # bias current
    b: float             # GLF curve parameter
    type_b: float        # GLF type blend (0 = A, 1 = B)
    vs: float            # supply voltage at bias
    rl: float            # plate load resistor
    rk: float            # cathode resistor
    kcomp: float = 0.0   # gain change / relative vs change
    # ----- runtime detector / EQ params (the trailing tube_ck_set args) -----
    kpk: float = 0.0     # peak-detector sensitivity to grid current peaks
    pk_xth: float = 0.0  # peak detector normalized threshold voltage
    pk_xdrop: float = 0.0  # peak detector normalized voltage drop
    tattack: float = 0.01  # input peak detector attack time const (s)
    trelease: float = 0.05  # input peak detector release time const (s)
    neq: float = 0.0     # antiderivative-EQ cutoff index (fc = neq*fs/12, 0 = bypass)
    tck: float = 0.0     # cathode bias time constant (s) \u2014 drives flt_ii1_set_tau

    @property
    def kbias(self) -> float:
        return self.ibias / self.isat

    @property
    def kloop(self) -> float:
        # tube_ck_set: kloop = 0 (feedback handled at runtime via kfb,
        # not baked into the GLF table).
        return 0.0

    @property
    def kpre(self) -> float:
        return self.mu / self.isat / (self.ra + self.rl)

    @property
    def ksva(self) -> float:
        return self.ra / (self.ra + self.rl)

    @property
    def kfb(self) -> float:
        """Outer-loop feedback factor: advk = kfb * lp(v_out - dvs).

        Only the CK variant has this; tube_cc_set bakes feedback into the
        GLF table (kloop > 0) and has no kfb.
        """
        return (1 + self.mu) / self.mu * self.rk / self.rl

    @property
    def kspre(self) -> float:
        return (1 - self.kcomp) / self.vs

    @property
    def kspost(self) -> float:
        return 1.0 / self.vs

    @property
    def ksib(self) -> float:
        return self.ibias / self.vs


@dataclass
class CdConfig:
    """Cathodyne (CD) tube stage parameters.

    Mirrors ``tube_cd_set`` in HK_LIB_TUBE.jsfx-inc lines 186-...
    """

    name: str
    mu: float
    ra: float
    isat: float
    ibias: float
    b: float
    type_b: float
    vs: float
    rl: float
    rk: float
    kcomp: float = 0.0
    # ----- runtime detector / EQ params (trailing tube_cd_set args) -----
    # No tck for CD: cathodyne has no advk averaging path (no global
    # outer feedback), so flt_ii1_set_tau isn't called in tube_cd_set.
    kpk: float = 0.0
    pk_xth: float = 0.0
    pk_xdrop: float = 0.0
    tattack: float = 0.01
    trelease: float = 0.05
    neq: float = 0.0

    @property
    def kbias(self) -> float:
        return self.ibias / self.isat

    @property
    def kloop(self) -> float:
        return (self.rk + self.rl) * (1 + self.mu) / (self.ra + self.rl)

    @property
    def kpre(self) -> float:
        return (self.mu / self.isat
                / (self.ra + self.rl + (1 + self.mu) * (self.rk + self.rl)))

    @property
    def ksva(self) -> float:
        return ((self.ra + self.rl + (1 + self.mu) * self.rk)
                / (self.ra + 2 * self.rl + (1 + self.mu) * self.rk))

    @property
    def ksvk(self) -> float:
        return (self.rk + self.rl) / (self.ra + 2 * self.rl + (1 + self.mu) * self.rk)

    @property
    def kspre(self) -> float:
        return (1 - self.kcomp) / self.vs

    @property
    def kspost(self) -> float:
        return 1.0 / self.vs

    @property
    def ksib(self) -> float:
        return self.ibias / self.vs


# 5E3 stage definitions.  Each stage's set of *physical* tube parameters
# (mu/ra/isat/ibias/b/type/vs/rl/rk/kcomp) and runtime detector params
# (kpk/xth/xdrop/tattack/trelease/neq/tck) is taken verbatim from
# Keller's 'TWD DLX  II.jsfx', so any change here should be cross-checked
# against that file.  References below cite line numbers in that patch.
T1_12AX7 = CkConfig(
    # TWD-DLX-II line 278 (default 5E3 voicing).  kpk=0 disables peak-detector
    # influence on the grid bias for this clean preamp stage.
    name="t1_12ax7_table",
    mu=100, ra=62500, isat=0.00165, ibias=0.00076,
    b=0, type_b=0.5, vs=238, rl=100000, rk=1500, kcomp=0.0,
    kpk=0.0, pk_xth=0.25, pk_xdrop=0.250,
    tattack=0.01, trelease=0.05, neq=2.0, tck=0.0375,
)

T2_12AX7 = CkConfig(
    # TWD-DLX-II line 180.  kpk=0.05 lets the PKD nudge the bias here.
    name="t2_12ax7_table",
    mu=100, ra=62500, isat=0.00155, ibias=0.00076,
    b=0, type_b=0.5, vs=238, rl=100000, rk=1500, kcomp=0.0,
    kpk=0.05, pk_xth=0.255, pk_xdrop=0.570,
    tattack=0.015, trelease=0.05, neq=2.0, tck=0.0375,
)

T3_CD = CdConfig(
    # TWD-DLX-II line 181.  Cathodyne; rk == rl/k for split-load symmetry.
    name="t3_cd_table",
    mu=100, ra=62500, isat=0.00160, ibias=0.00073,
    b=0, type_b=0.5, vs=238, rl=56000, rk=1500, kcomp=0.0,
    kpk=0.125, pk_xth=0.272, pk_xdrop=0.394,
    tattack=0.00085, trelease=0.3872, neq=2.0,
)

T4_6V6 = CkConfig(
    # TWD-DLX-II line 295.  6V6 wired triode-mode (b=2, type=0.5).
    # Note kcomp=1: the supply-voltage modulation term scales the input
    # by (1 - kcomp) / vs = 0, i.e. the kspre coefficient evaluates to 0
    # for the power tube (sag is handled via PSS, not pre-gain modulation).
    name="t4_6v6_table",
    mu=125, ra=40000, isat=0.11, ibias=0.042,
    b=2, type_b=0.5, vs=346, rl=3000, rk=540, kcomp=1.0,
    kpk=0.125, pk_xth=0.309, pk_xdrop=0.437,
    tattack=0.00575, trelease=0.0276, neq=2.0, tck=0.00675,
)

T5_6V6 = CkConfig(
    # TWD-DLX-II line 296.  Second 6V6 for mode 0 push-pull aux branch.
    name="t5_6v6_table",
    mu=125, ra=40000, isat=0.12, ibias=0.042,
    b=2.5, type_b=0.5, vs=346, rl=3000, rk=540, kcomp=1.0,
    kpk=0.18, pk_xth=0.325, pk_xdrop=0.388,
    tattack=0.00155, trelease=0.0234, neq=2.0, tck=0.00675,
)


def _print_constants(cfg):
    """Echo derived runtime constants for human inspection."""
    print(f"--- {cfg.name} ---")
    print(f"  kbias  = {cfg.kbias:.6f}")
    print(f"  kloop  = {cfg.kloop:.6f}")
    print(f"  kpre   = {cfg.kpre:.6f}")
    print(f"  ksva   = {cfg.ksva:.6f}")
    if hasattr(cfg, "ksvk"):
        print(f"  ksvk   = {cfg.ksvk:.6f}")
    if hasattr(cfg, "kfb"):
        print(f"  kfb    = {cfg.kfb:.6f}")
    print(f"  kspre  = {cfg.kspre:.6f}")
    print(f"  kspost = {cfg.kspost:.6f}")
    print(f"  ksib   = {cfg.ksib:.6f}")


def main():
    stages = [T1_12AX7, T2_12AX7, T3_CD, T4_6V6, T5_6V6]

    # ECC83/12AX7 stages eligible for DZ load-line replacement.  6V6 (T4)
    # stages stay GLF: they are pentodes, not modeled by Dempwolf-Zölzer's ECC83
    # equations.  T1 here is the default 12AY7 voicing; we still emit a
    # DZ variant for it because TWD-DLX-II also has a 12AX7-mod toggle
    # (line 281) that uses identical Keller params except mu/ra/rk —
    # the DZ table here is keyed to the *12AX7* voicing's load line, so
    # callers should only switch to the DZ T1 table together with the
    # 12AX7 input-mod runtime constants.
    dz_stages = [
        ("t1_12ax7_table_dz", T1_12AX7, "ck"),
        ("t2_12ax7_table_dz", T2_12AX7, "ck"),
        ("t3_cd_table_dz",    T3_CD,    "cd"),
    ]

    table_specs = []

    # GLF tables (v1, behavioural — what the canonical Keller patch ships).
    for cfg in stages:
        spec = gen_adnl_table(cfg.kbias, cfg.b, cfg.type_b, cfg.kloop)
        table_specs.append((cfg.name, spec))
        _print_constants(cfg)
        print()

    # DZ tables (v2, physical — replace symmetric tanh with real ECC83 curves).
    for tbl_name, cfg, topology in dz_stages:
        if topology == "ck":
            dz_spec = gen_adnl_table_dz_ck(
                vs=cfg.vs, ra=cfg.ra, rl=cfg.rl, rk=cfg.rk,
                isat=cfg.isat, ibias=cfg.ibias, kpre=cfg.kpre,
            )
        else:  # cd
            dz_spec = gen_adnl_table_dz_cd(
                vs=cfg.vs, ra=cfg.ra, rl=cfg.rl, rk=cfg.rk,
                isat=cfg.isat, ibias=cfg.ibias, kpre=cfg.kpre,
            )
        table_specs.append((tbl_name, dz_spec))
        print(f"--- {tbl_name} (DZ) ---")
        print(f"  ymin   = {dz_spec['ymin']:.6f}")
        print(f"  ymax   = {dz_spec['ymax']:.6f}")
        print(f"  z_xmax = {dz_spec['z_at_xmax']:.6f}")
        print()

    out_dir = Path("native/generated")
    out_dir.mkdir(parents=True, exist_ok=True)
    h_path = out_dir / "nilamp_tables.h"
    c_path = out_dir / "nilamp_tables.c"
    with open(h_path, "w") as h:
        h.write("// SPDX-License-Identifier: MIT\n")
        h.write("// 5E3 ADNL tables (auto-generated by tools/gen_5e3_tables.py).\n")
        h.write("// Do not edit by hand.\n\n")
        h.write("#ifndef NILAMP_TABLES_H\n#define NILAMP_TABLES_H\n\n")
        h.write("#include <stddef.h>\n\n")
        for name, spec in table_specs:
            h.write(export_c_table_decl(name, spec))
        h.write("\n#endif\n")
    with open(c_path, "w") as c:
        c.write("// SPDX-License-Identifier: MIT\n")
        c.write("// 5E3 ADNL tables (auto-generated by tools/gen_5e3_tables.py).\n")
        c.write("// Do not edit by hand.\n\n")
        c.write("#include \"nilamp_tables.h\"\n\n")
        for name, spec in table_specs:
            c.write(export_c_table_def(name, spec))
    print(f"  wrote {h_path}")
    print(f"  wrote {c_path}")


if __name__ == "__main__":
    main()
