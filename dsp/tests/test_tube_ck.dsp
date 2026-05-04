// Test harness: full tube_ck pipeline with T2 (12AX7 v2) parameters.
//
// T2 is chosen because it has kpk=0.05 (active PKD bias modulation),
// kfb>0 (active local-feedback loop with avg_f LP), and a non-zero
// kspre/kspost.  T1 is mostly redundant (kpk=0).  Constants come
// from 5e3_constants.lib so any change to gen_5e3_tables.py is
// visible to this harness automatically.
//
// dvs is hard-coded to 0 so this stage runs open-loop (no PSS sag
// feedback).  The closed-loop case is covered separately by the
// nilamp.dsp end-to-end test (gated behind NILAMP_BUILD_TOPLEVEL).
//
// Output 1: v_out (plate voltage relative to bias).
// Output 2: dia   (anode-current delta, used downstream for PSS sag).
// The Python oracle (tools.keller_oracle.TubeCk) emits both so we
// pin both signals.
import("stdfaust.lib");
tube = library("hk_tube.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

process(v) = tube.tube_ck_simple(
    13503, tables.t2_12ax7_table, 15.0, 0.02,
    c.t2_kpre, c.t2_isat, c.t2_rl, c.t2_kpk,
    c.t2_kspre, c.t2_kspost, c.t2_ksva, c.t2_ksib, c.t2_kfb,
    c.t2_pk_xth, c.t2_pk_xdiode, c.t2_pk_k1, c.t2_pk_k2,
    c.t2_avg_f,
    v, 0);
