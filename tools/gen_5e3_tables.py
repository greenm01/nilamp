"""Generate ADNL waveform tables for the 5E3 model.

This module ports Keller's ``tube_ck_set`` / ``tube_cd_set`` configuration math
(HK_LIB_TUBE.jsfx-inc) into Python so the Faust runtime constants and the ADNL
table coefficients stay in sync.  Each ``CkConfig`` / ``CdConfig`` mirrors the
JSFX call signature one-to-one, e.g.

    t1.tube_ck_set(mu=100, ra=62500, isat=0.00165, ibias=0.00076,
                   b=0, type_b=0.5, vs=238, rl=100000, rk=1500, ...)

The script currently writes the ADNL polynomial waveform tables to
``dsp/5e3_tables.lib``.  The runtime gain coefficients (``kpre``, ``ksva`` …)
are also computed here and printed for reference; once Step 7 wires the new
``hk_tube.lib`` API end to end, they can be exported to a generated
``5e3_constants.lib`` rather than hand-tuned in ``nilamp.dsp``.
"""

from dataclasses import dataclass

from gen_tables import gen_adnl_table, export_faust_table


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


# 5E3 stage definitions, matching the canonical 12AX7 / 6V6 values from
# Keller's "TWD DLX  II.jsfx" (lines 180-181, 295).  The current Faust port
# uses one 12AX7 stage + cathodyne + a single 6V6 (push-pull is deferred to
# 5e3-v2), so we generate three tables here.
T1_12AX7 = CkConfig(
    name="t1_12ax7_table",
    mu=100, ra=62500, isat=0.00165, ibias=0.00076,
    b=0, type_b=0.5, vs=238, rl=100000, rk=1500, kcomp=0.0,
)

T2_12AX7 = CkConfig(
    name="t2_12ax7_table",
    mu=100, ra=62500, isat=0.00155, ibias=0.00076,
    b=0, type_b=0.5, vs=238, rl=100000, rk=1500, kcomp=0.0,
)

T3_CD = CdConfig(
    name="t3_cd_table",
    mu=100, ra=62500, isat=0.00160, ibias=0.00073,
    b=0, type_b=0.5, vs=238, rl=56000, rk=1500, kcomp=0.0,
)

T4_6V6 = CkConfig(
    name="t4_6v6_table",
    mu=125, ra=40000, isat=0.11, ibias=0.042,
    b=2, type_b=0.5, vs=346, rl=3000, rk=540, kcomp=1.0,
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
    stages = [T1_12AX7, T2_12AX7, T3_CD, T4_6V6]

    with open("dsp/5e3_tables.lib", "w") as f:
        f.write("// 5E3 ADNL Tables (auto-generated by tools/gen_5e3_tables.py)\n\n")
        for cfg in stages:
            spec = gen_adnl_table(cfg.kbias, cfg.b, cfg.type_b, cfg.kloop)
            f.write(export_faust_table(cfg.name, spec))
            _print_constants(cfg)
            print()


if __name__ == "__main__":
    main()
