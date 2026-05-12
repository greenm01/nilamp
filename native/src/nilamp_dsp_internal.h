// SPDX-License-Identifier: MIT

#ifndef NILAMP_DSP_INTERNAL_H
#define NILAMP_DSP_INTERNAL_H

#include "nilamp_dsp.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    float s;
} Ii1;

typedef struct {
    float s1;
    float s2;
} Svf2;

typedef struct {
    float s1;
} Svf1;

typedef struct {
    float z1;
    float z2;
} Df2;

typedef struct {
    float s1;
    float s2;
} Pkd;

typedef struct {
    float act;
    float tgt;
    float slope;
} Smooth;

typedef struct {
    float k;
} Ii1Coeffs;

typedef struct {
    float k;
    float kf;
    float kdiv;
    float b0;
    float kb1;
    float b2;
} Svf2Coeffs;

typedef struct {
    Smooth k;
    Smooth kf;
    Smooth kdiv;
    Smooth b0;
    Smooth kb1;
    Smooth b2;
} Svf2SmCoeffs;

typedef struct {
    float k;
    float kdiv;
    float b0;
} Svf1HsCoeffs;

typedef struct {
    Smooth k;
    Smooth kdiv;
    Smooth b0;
} Svf1HsSmCoeffs;

typedef struct {
    float b0;
    float b1;
    float b2;
    float a1;
    float a2;
} Df2Coeffs;

typedef struct {
    float k1;
    float k2;
} PkdCoeffs;

typedef struct {
    Smooth g;
} GainSmCoeffs;

typedef struct {
    double prev_x;
    double prev_y;
    double prev_z;
} Adnl;

typedef struct {
    Pkd pkd;
    Adnl adnl;
    Df2 neq;
    Ii1 advk_lp;
    float advk;
} TubeCk;

typedef struct {
    Pkd pkd;
    Adnl adnl;
    Df2 neq;
} TubeCd;

typedef struct {
    Pkd pkd1;
    Pkd pkd2;
    Adnl adnl1;
    Adnl adnl2;
    Df2 neq1;
    Df2 neq2;
    float dia;
    float vout2;
} TubeLtp;

typedef struct {
    Ii1 s_lp;
    Ii1 dvs_lp;
    float s;
} TubePss;

typedef enum {
    NILAMP_TOPOLOGY_TWEED_5E3_CATHODYNE_PP = 0,
} NilampTopologyId;

typedef enum {
    NILAMP_STAGE_KIND_TRIODE_CK = 0,
    NILAMP_STAGE_KIND_CATHODYNE = 1,
    NILAMP_STAGE_KIND_POWER_CK = 2,
} NilampStageKind;

typedef enum {
    NILAMP_DSP_METHOD_KELLER_GLF_ADAA = 0,
    NILAMP_DSP_METHOD_KELLER_CD_ADAA = 1,
    NILAMP_DSP_METHOD_DEMPWOLF_ZOLZER_ADAA = 2,
    NILAMP_DSP_METHOD_HEGGLUN_BLOCKING_W = 3,
    NILAMP_DSP_METHOD_KELLER_PSS = 4,
} NilampDspMethod;

typedef struct {
    const float *table;
    size_t len;
    float kpre;
    float isat;
    float rl;
    float rkl;
    float kspre;
    float kspost;
    float ksva;
    float ksvk;
    float ksib;
    float kfb;
    float kpk;
    float pk_xth;
    float pk_xdiode;
    float pk_attack;
    float pk_release;
    float avg_tau;
    NilampStageKind kind;
    NilampDspMethod method;
} StageCfg;

typedef struct {
    const float *table1;
    size_t len1;
    const float *table2;
    size_t len2;
    float kpre11;
    float kpre12;
    float kpre22;
    float kpre21;
    float kprek1;
    float kprek2;
    float isat;
    float rl1;
    float rl2;
    float kspre;
    float kspost;
    float ksv1;
    float ksv2;
    float ksib;
    float kpk;
    float pk_xth;
    float pk_xdiode;
    float pk_attack;
    float pk_release;
} LtpCfg;

typedef struct {
    bool is_ltp;
    const StageCfg *t4;
    const StageCfg *t5;
    float k1;
    float k2;
    float hp2;
    float hp3;
    float hp4;
    float kmst;
    float k4;
} SplitterModeCfg;

typedef struct {
    float v_out;
    float res1_v;
    float res3_v;
    float res4_v;
    float drive_t4;
    float res5_v;
    float res_t5_v;
    float dvs2;
    float dvs3;
    float p2_s;
    float p3_s;
    float drive_t5;
    float post_pp;
    float post_peq3;
    float post_hs3;
    float post_hp5;
    float t4_advk_in;
    float t5_advk_in;
    float t4_dia;
    float t5_dia;
    float t4_advk_out;
    float t5_advk_out;
    float dia1_next;
} NilampTapFrame;

typedef struct {
    NilampModelId id;
    NilampTopologyId topology;
    const char *name;
    const char *family;
    const char *clap_name;
    const char *clap_filename;
    const char *vst3_name;
    const char *vst3_filename;
    const char *vst3_executable;
    const char *vst3_bundle_id;
    float speaker_source_ohms;
    float speaker_nominal_ohms;
    const void *topology_data;
    const NilampControlSpec *controls;
    uint32_t control_count;
    const NilampGuiLayoutSpec *gui_layout;
} NilampModelSpec;

typedef struct {
    NilampTopologyId topology;
    size_t state_size;
    size_t coeffs_size;
    bool (*accepts_model_data)(const void *topology_data);
    void (*reset)(void *state, void *coeffs, const NilampParams *params,
                  const void *topology_data, double sr);
    void (*set_params)(void *state, void *coeffs, const NilampParams *params,
                       const void *topology_data, double sr, bool has_processed);
    NilampTapFrame (*process_sample)(void *state, void *coeffs,
                                     const NilampParams *params,
                                     const void *topology_data, float input);
} NilampTopologyOps;

const NilampModelSpec *nilamp_find_model(NilampModelId model_id);

float db_to_linear(float db);
float iso266(float dbhz);
void smooth_snap(Smooth *s, float v);
void smooth_set(Smooth *s, float v, float smooth_k);
float gain_sm_process(GainSmCoeffs *c, float x);
Ii1Coeffs ii1_lp_coeffs(float f, double sr);
float ii1_lp_process(Ii1 *st, Ii1Coeffs c, float x);
float ii1_hp_process(Ii1 *st, Ii1Coeffs c, float x);
Svf2Coeffs svf2_tst_coeffs(float b, float m, float t, float f, float q, double sr);
Svf2Coeffs svf2_peq_coeffs(float kgain, float f, float qc, double sr);
void svf2_sm_snap(Svf2SmCoeffs *dst, Svf2Coeffs src);
void svf2_sm_set(Svf2SmCoeffs *dst, Svf2Coeffs src, float smooth_k);
float svf2_sm_process(Svf2 *st, Svf2SmCoeffs *c, float x);
Svf1HsCoeffs svf1_hs_coeffs(float kgain, float fs, double sr);
void svf1_hs_sm_snap(Svf1HsSmCoeffs *dst, Svf1HsCoeffs src);
void svf1_hs_sm_set(Svf1HsSmCoeffs *dst, Svf1HsCoeffs src, float smooth_k);
float svf1_hs_sm_process(Svf1 *st, Svf1HsSmCoeffs *c, float x);
Df2Coeffs df2_lp_coeffs(double sr, float f, float q, bool prewarp_q);
Df2Coeffs adnl_eq_make_coeffs(double sr);
float df2_coeffs_process(Df2 *st, Df2Coeffs c, float x);
PkdCoeffs pkd_coeffs(float attack, float release, double sr);
Ii1Coeffs tube_advk_coeffs(const StageCfg *cfg, double sr);
void tube_ck_process(TubeCk *st, const StageCfg *cfg, PkdCoeffs pk_coeffs,
                     Df2Coeffs adnl_eq, Ii1Coeffs advk_coeffs,
                     float v, float dvs, float *v_out, float *dia);
void tube_cd_process(TubeCd *st, const StageCfg *cfg, PkdCoeffs pk_coeffs,
                     Df2Coeffs adnl_eq, float v, float dvs,
                     float *v_out, float *vk_out, float *dia);
void tube_ltp_process(TubeLtp *st, const LtpCfg *cfg,
                      PkdCoeffs pk1_coeffs, PkdCoeffs pk2_coeffs,
                      Df2Coeffs adnl_eq,
                      float vin1, float vink, float vin2, float dvs,
                      float *v_out, float *vout2, float *dia);
float tube_pss_process(TubePss *st, float r, Ii1Coeffs coeffs,
                       float snext, float dia, float dvs_in);
void adnl_seed_at_zero(Adnl *st, const float *table);
int nilamp_enum_param(float value, int count, int fallback);

#ifdef NILAMP_ENABLE_TEST_API
float svf2_coeffs_process(Svf2 *st, Svf2Coeffs c, float x);
float svf1_hs_coeffs_process(Svf1 *st, Svf1HsCoeffs c, float x);
#endif

#endif
