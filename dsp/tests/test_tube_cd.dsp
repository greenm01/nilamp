// Test harness: full tube_cd pipeline with T3 (cathodyne) parameters.
//
// T3 is the only cathodyne stage in 5E3.  Has kpk=0.125 (active PKD)
// and produces three outputs: plate (v_out), cathode (vk_out), and
// dia (anode-current delta).  Cathodyne has no advk feedback (no kfb,
// no avg_f) since the split-load is its own feedback loop.
//
// dvs hard-coded to 0; see test_tube_ck.dsp for rationale.
//
// neq coefficients pinned to identity (b0=1, others=0): the runtime
// EQ is currently bypassed in nilamp.dsp because c.t3_neq=0.
// TODO(5e3-v2): once flt_df2_set_adnl_eq is ported, this harness
// will need to compute and pass the proper coefficients.
import("stdfaust.lib");
tube = library("hk_tube.lib");
tables = library("5e3_tables.lib");
c = library("5e3_constants.lib");

process(v) = tube.tube_cd(
    13503, tables.t3_cd_table, 15.0, 0.02,
    c.t3_kpre, c.t3_isat, c.t3_rl, c.t3_rkl, c.t3_kpk,
    c.t3_kspre, c.t3_kspost, c.t3_ksva, c.t3_ksvk, c.t3_ksib,
    c.t3_pk_xth, c.t3_pk_xdiode, c.t3_pk_k1, c.t3_pk_k2,
    1, 0, 0, 0, 0,
    v, 0);
