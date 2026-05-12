// SPDX-License-Identifier: MIT

#ifndef NILAMP_TOPOLOGIES_H
#define NILAMP_TOPOLOGIES_H

#include "nilamp_dsp_internal.h"

typedef struct {
    TubeCk t1;
    TubeCk t2;
    TubeCd t3;
    TubeLtp t6;
    TubeCk t4;
    TubeCk t5;

    Ii1 hp1;
    Svf2 tone;
    Ii1 lp1;
    Ii1 hp2;

    Ii1 hp3;
    Svf2 peq1_t4;
    Svf1 hs1_t4;

    Ii1 hp4;
    Svf2 peq1_t5;
    Svf1 hs1_t5;

    Svf2 peq3;
    Svf1 hs3;
    Ii1 hp5;
    Df2 lp2;

    TubePss p1;
    TubePss p2;
    TubePss p3;
    float prev_dia1;
    float prev_dig;
    float prev_dia3;
    int active_tube1;
    int active_splitter;
} NilampTweed5e3PpState;

typedef struct {
    float smooth_k;

    GainSmCoeffs gain_stage;
    GainSmCoeffs volume_stage;
    GainSmCoeffs output_stage;

    Ii1Coeffs hp1;
    Ii1Coeffs lp1;
    Ii1Coeffs hp2;
    Ii1Coeffs hp3;
    Ii1Coeffs hp4;
    Ii1Coeffs hp5;
    Ii1Coeffs pss1_f;
    Ii1Coeffs pss2_f;
    Ii1Coeffs pss3_f;
    Ii1Coeffs advk_t1;
    Ii1Coeffs advk_t2;
    Ii1Coeffs advk_t4;
    Ii1Coeffs advk_t5;

    Svf2SmCoeffs tone;
    Svf2SmCoeffs peq1_t4;
    Svf2SmCoeffs peq1_t5;
    Svf2SmCoeffs peq3;
    Svf1HsSmCoeffs hs1_t4;
    Svf1HsSmCoeffs hs1_t5;
    Svf1HsSmCoeffs hs3;
    Df2Coeffs lp2;
    Df2Coeffs adnl_eq;

    PkdCoeffs pk_t1;
    PkdCoeffs pk_t2;
    PkdCoeffs pk_t3;
    PkdCoeffs pk_t4;
    PkdCoeffs pk_t5;
    PkdCoeffs pk_ltp1;
    PkdCoeffs pk_ltp2;

    float pss3_r;
    float post_scale;
    int tube1_mode;
    int splitter_mode;
} NilampTweed5e3PpCoeffs;

typedef struct {
    NilampTopologyId topology;
    NilampDspMethod supply_method;
    const StageCfg *t1;
    const StageCfg *t2;
    const StageCfg *t3;
    const StageCfg *t4;
    const StageCfg *t5;
    float input_feed_gain;
    float input_gain_offset_db;
    float input_keller_gain_sq;
    float gain_comp_12ax7;
    float gain_comp_ltp_base;
    float pss1_r;
    float pss1_tau;
    float pss2_r;
    float pss2_tau;
    float pss3_r_at_full_sag;
    float pss3_tau;
    float phase_t4_gain;
    float phase_t5_gain;
    float screen_current_feedback;
} NilampTweed5e3PpData;

extern const NilampTopologyOps NILAMP_TWEED_5E3_PP_OPS;

const NilampTopologyOps *nilamp_topology_ops(NilampTopologyId topology);

#endif
