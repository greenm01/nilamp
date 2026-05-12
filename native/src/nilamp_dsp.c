// SPDX-License-Identifier: MIT

#include "nilamp_dsp.h"

#include "nilamp_dsp_internal.h"
#include "nilamp_tables.h"
#include "nilamp_topologies.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TBL_SIZE 13503
#define XMAX 15.0f
#define DX 0.02f

struct NilampEngine {
    double sr;
    NilampParams params;
    const NilampModelSpec *model;
    const NilampTopologyOps *topology_ops;
    void *topology_state;
    void *topology_coeffs;
    bool has_processed;
};

static int nilamp_tap_frame_is_finite(NilampTapFrame taps)
{
    return isfinite(taps.v_out) && isfinite(taps.res1_v) && isfinite(taps.res3_v) &&
           isfinite(taps.res4_v) && isfinite(taps.drive_t4) && isfinite(taps.res5_v) &&
           isfinite(taps.res_t5_v) && isfinite(taps.dvs2) && isfinite(taps.dvs3) &&
           isfinite(taps.p2_s) && isfinite(taps.p3_s) && isfinite(taps.drive_t5) &&
           isfinite(taps.post_pp) && isfinite(taps.post_peq3) && isfinite(taps.post_hs3) &&
           isfinite(taps.post_hp5) && isfinite(taps.t4_advk_in) &&
           isfinite(taps.t5_advk_in) && isfinite(taps.t4_dia) &&
           isfinite(taps.t5_dia) && isfinite(taps.t4_advk_out) &&
           isfinite(taps.t5_advk_out) && isfinite(taps.dia1_next);
}

#include "nilamp_models.inc"

const NilampModelSpec *nilamp_find_model(NilampModelId model_id)
{
    for (size_t i = 0; i < sizeof(NILAMP_MODELS) / sizeof(NILAMP_MODELS[0]); i++) {
        if (NILAMP_MODELS[i].id == model_id) {
            return &NILAMP_MODELS[i];
        }
    }
    return NULL;
}

float db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);
}

float iso266(float dbhz)
{
    const float f = db_to_linear(dbhz);
    const float decade = floorf(0.05f * dbhz);
    const float fract = 0.05f * dbhz - decade;
    float resolution = 0.02f * powf(10.0f, decade);
    if (fract >= 0.899f) {
        resolution = 0.2f * powf(10.0f, decade);
    } else if (fract >= 0.649f) {
        resolution = 0.1f * powf(10.0f, decade);
    } else if (fract >= 0.089f) {
        resolution = 0.05f * powf(10.0f, decade);
    }
    return resolution * floorf(f / resolution + 0.5f);
}

void smooth_snap(Smooth *s, float v)
{
    if (!isfinite(v)) {
        v = 0.0f;
    }
    s->act = v;
    s->tgt = v;
    s->slope = 0.0f;
}

void smooth_set(Smooth *s, float v, float smooth_k)
{
    if (!isfinite(v)) {
        v = 0.0f;
    }
    s->tgt = v;
    s->slope = smooth_k * fabsf(v - s->act);
}

static void smooth_tick(Smooth *s)
{
    if (s->slope == 0.0f) {
        s->act = s->tgt;
        return;
    }
    if (s->act < s->tgt) {
        s->act += s->slope;
        if (s->act > s->tgt) {
            s->act = s->tgt;
        }
    } else if (s->act > s->tgt) {
        s->act -= s->slope;
        if (s->act < s->tgt) {
            s->act = s->tgt;
        }
    }
}

float gain_sm_process(GainSmCoeffs *c, float x)
{
    smooth_tick(&c->g);
    return x * c->g.act;
}

Ii1Coeffs ii1_lp_coeffs(float f, double sr)
{
    const float k = 1.0f - expf((float)(-2.0 * M_PI * (double)f / sr));
    return (Ii1Coeffs) { k };
}

float ii1_lp_process(Ii1 *st, Ii1Coeffs c, float x)
{
    const float k = c.k;
    st->s = (x - st->s) * k + st->s;
    return st->s;
}

float ii1_hp_process(Ii1 *st, Ii1Coeffs c, float x)
{
    return x - ii1_lp_process(st, c, x);
}

static void svf2_process(Svf2 *st, float k, float kf, float kdiv, float x, float *hp, float *bp, float *lp)
{
    *hp = (x - kf * st->s1 - st->s2) * kdiv;
    const float v = k * *hp;
    *bp = v + st->s1;
    st->s1 = v + *bp;
    const float v2 = k * *bp;
    *lp = v2 + st->s2;
    st->s2 = v2 + *lp;
}

static float sv2_k(float f, double sr, int pwf)
{
    const float x = (float)(M_PI * (double)f / sr);
    return pwf ? tanf(x) : x;
}

static float sv2_kq(float f, float q, double sr, int pwq)
{
    if (!pwq) {
        return 1.0f / q;
    }
    const float k = (float)(M_PI * (double)f / sr);
    const float aux1 = sqrtf(1.0f + 4.0f * q * q);
    const float aux2 = (k / sinf(2.0f * k + 1e-20f)) * logf((aux1 + 1.0f) / (aux1 - 1.0f));
    return expf(aux2) - expf(-aux2);
}

Svf2Coeffs svf2_tst_coeffs(float b, float m, float t, float f, float q, double sr)
{
    const float kq = sv2_kq(f, q, sr, 1);
    const float k = sv2_k(f, sr, 1);
    const float kf = kq + k;
    const float kdiv = 1.0f / (1.0f + k * (k + kq));
    return (Svf2Coeffs) { k, kf, kdiv, t, kq * m, b };
}

Svf2Coeffs svf2_peq_coeffs(float kgain, float f, float qc, double sr)
{
    const float sqrt_gain = sqrtf(kgain);
    const float kq = sv2_kq(f, qc * sqrt_gain, sr, 1);
    const float k = sv2_k(f, sr, 1);
    const float kf = kq + k;
    const float kdiv = 1.0f / (1.0f + k * (k + kq));
    return (Svf2Coeffs) { k, kf, kdiv, 1.0f, kq * kgain, 1.0f };
}

#ifdef NILAMP_ENABLE_TEST_API
float svf2_coeffs_process(Svf2 *st, Svf2Coeffs c, float x)
{
    float hp, bp, lp;
    svf2_process(st, c.k, c.kf, c.kdiv, x, &hp, &bp, &lp);
    return hp * c.b0 + bp * c.kb1 + lp * c.b2;
}
#endif

void svf2_sm_snap(Svf2SmCoeffs *dst, Svf2Coeffs src)
{
    smooth_snap(&dst->k, src.k);
    smooth_snap(&dst->kf, src.kf);
    smooth_snap(&dst->kdiv, src.kdiv);
    smooth_snap(&dst->b0, src.b0);
    smooth_snap(&dst->kb1, src.kb1);
    smooth_snap(&dst->b2, src.b2);
}

void svf2_sm_set(Svf2SmCoeffs *dst, Svf2Coeffs src, float smooth_k)
{
    smooth_set(&dst->k, src.k, smooth_k);
    smooth_set(&dst->kf, src.kf, smooth_k);
    smooth_set(&dst->kdiv, src.kdiv, smooth_k);
    smooth_set(&dst->b0, src.b0, smooth_k);
    smooth_set(&dst->kb1, src.kb1, smooth_k);
    smooth_set(&dst->b2, src.b2, smooth_k);
}

float svf2_sm_process(Svf2 *st, Svf2SmCoeffs *c, float x)
{
    smooth_tick(&c->k);
    smooth_tick(&c->kf);
    smooth_tick(&c->kdiv);
    smooth_tick(&c->b0);
    smooth_tick(&c->kb1);
    smooth_tick(&c->b2);
    float hp, bp, lp;
    svf2_process(st, c->k.act, c->kf.act, c->kdiv.act, x, &hp, &bp, &lp);
    return hp * c->b0.act + bp * c->kb1.act + lp * c->b2.act;
}

Svf1HsCoeffs svf1_hs_coeffs(float kgain, float fs, double sr)
{
    const float k_raw = tanf((float)(M_PI * (double)fs / sr));
    const float k = sqrtf(kgain) * k_raw;
    const float kdiv = 1.0f / (1.0f + k);
    return (Svf1HsCoeffs) { k, kdiv, kgain };
}

#ifdef NILAMP_ENABLE_TEST_API
float svf1_hs_coeffs_process(Svf1 *st, Svf1HsCoeffs c, float x)
{
    const float hp = (x - st->s1) * c.kdiv;
    const float v = c.k * hp;
    const float lp = v + st->s1;
    st->s1 = v + lp;
    return hp * c.b0 + lp;
}
#endif

void svf1_hs_sm_snap(Svf1HsSmCoeffs *dst, Svf1HsCoeffs src)
{
    smooth_snap(&dst->k, src.k);
    smooth_snap(&dst->kdiv, src.kdiv);
    smooth_snap(&dst->b0, src.b0);
}

void svf1_hs_sm_set(Svf1HsSmCoeffs *dst, Svf1HsCoeffs src, float smooth_k)
{
    smooth_set(&dst->k, src.k, smooth_k);
    smooth_set(&dst->kdiv, src.kdiv, smooth_k);
    smooth_set(&dst->b0, src.b0, smooth_k);
}

float svf1_hs_sm_process(Svf1 *st, Svf1HsSmCoeffs *c, float x)
{
    smooth_tick(&c->k);
    smooth_tick(&c->kdiv);
    smooth_tick(&c->b0);
    const float hp = (x - st->s1) * c->kdiv.act;
    const float v = c->k.act * hp;
    const float lp = v + st->s1;
    st->s1 = v + lp;
    return hp * c->b0.act + lp;
}

static float df2_process(Df2 *st, float b0, float b1, float b2, float a1, float a2, float x)
{
    const float z0 = x - st->z1 * a1 - st->z2 * a2;
    const float y = z0 * b0 + st->z1 * b1 + st->z2 * b2;
    st->z2 = st->z1;
    st->z1 = z0;
    return y;
}

static int adnl_eq_index(double sr)
{
    if (sr >= 176400.0) {
        return 0;
    }
    if (sr >= 88200.0) {
        return 1;
    }
    return 2;
}

static void adnl_eq_coeffs(int n, float *b0, float *b1, float *b2, float *a1, float *a2)
{
    switch (n) {
    case 1:
        *b0 = 1.056878f;
        *b1 = -1.271531f;
        *b2 = 0.418433f;
        *a1 = -1.179530f;
        *a2 = 0.383309f;
        break;
    case 2:
        *b0 = 1.200445f;
        *b1 = -0.732882f;
        *b2 = 0.178744f;
        *a1 = -0.468873f;
        *a2 = 0.115181f;
        break;
    case 3:
        *b0 = 1.408580f;
        *b1 = -0.221734f;
        *b2 = 0.069520f;
        *a1 = 0.203224f;
        *a2 = 0.053142f;
        break;
    case 4:
        *b0 = 1.656505f;
        *b1 = 0.357005f;
        *b2 = 0.020442f;
        *a1 = 0.851983f;
        *a2 = 0.181969f;
        break;
    default:
        *b0 = 1.0f;
        *b1 = 0.0f;
        *b2 = 0.0f;
        *a1 = 0.0f;
        *a2 = 0.0f;
        break;
    }
}

Df2Coeffs adnl_eq_make_coeffs(double sr)
{
    float b0, b1, b2, a1, a2;
    adnl_eq_coeffs(adnl_eq_index(sr), &b0, &b1, &b2, &a1, &a2);
    return (Df2Coeffs) { b0, b1, b2, a1, a2 };
}

float df2_coeffs_process(Df2 *st, Df2Coeffs c, float x)
{
    return df2_process(st, c.b0, c.b1, c.b2, c.a1, c.a2, x);
}

Df2Coeffs df2_lp_coeffs(double sr, float f, float q, bool prewarp_q)
{
    const float pi_t = (float)(M_PI / sr);
    const float k0 = f * pi_t;
    float kq0 = 1.0f / q;
    if (prewarp_q) {
        const float aux1 = sqrtf(1.0f + 4.0f * q * q);
        const float aux2 = (k0 / sinf(2.0f * k0 + 1e-20f)) * logf((aux1 + 1.0f) / (aux1 - 1.0f));
        kq0 = expf(aux2) - expf(-aux2);
    }
    const float k = tanf(f * pi_t);
    const float kq = k * kq0;
    const float ksqr = k * k;
    const float kdiv = 1.0f / (1.0f + kq + ksqr);
    const float a1 = (-2.0f + 2.0f * ksqr) * kdiv;
    const float a2 = (1.0f - kq + ksqr) * kdiv;
    const float b0 = ksqr * kdiv;
    const float b1 = 2.0f * ksqr * kdiv;
    const float b2 = b0;
    return (Df2Coeffs) { b0, b1, b2, a1, a2 };
}

static float pkd_process(Pkd *st, float xth, float xdiode, float k1, float k2, float x)
{
    const float xd = fmaxf(1e-10f, xdiode);
    float x_val;
    if (x <= xth) {
        x_val = 0.0f;
    } else if (x >= xth + 2.0f * xd) {
        x_val = (x - xth) - xd;
    } else {
        const float d = x - xth;
        x_val = 0.25f * d * d / xd;
    }
    st->s1 = (x_val - st->s1) * k1 + st->s1;
    st->s2 = fmaxf(st->s1, k2 * st->s2);
    return st->s2;
}

static float pkd_coeffs_process(Pkd *st, float xth, float xdiode, PkdCoeffs c, float x)
{
    return pkd_process(st, xth, xdiode, c.k1, c.k2, x);
}

static float adnl_process(Adnl *st, const float *table, size_t table_len, float x)
{
    const int num_segments = (int)(2.0f * XMAX / DX);
    int index = (int)((x + XMAX) / DX);
    if (index < 0) {
        index = 0;
    } else if (index > num_segments - 1) {
        index = num_segments - 1;
    }

    const double xd = (double)x;
    const double xmax = (double)XMAX;
    const double dx = (double)DX;
    const double w = xd + xmax - (double)index * dx;
    const size_t base = (size_t)index * 9u;
    const double a3 = (double)table[base + 0u];
    const double a2 = (double)table[base + 1u];
    const double a1 = (double)table[base + 2u];
    const double a0 = (double)table[base + 3u];
    const double b4 = (double)table[base + 4u];
    const double b3 = (double)table[base + 5u];
    const double b2 = (double)table[base + 6u];
    const double b1 = (double)table[base + 7u];
    const double b0 = (double)table[base + 8u];

    const double ymin = (double)table[table_len - 3u];
    const double ymax = (double)table[table_len - 2u];
    const double z_at_xmax = (double)table[table_len - 1u];

    double y1 = ((a3 * w + a2) * w + a1) * w + a0;
    double z1 = (((b4 * w + b3) * w + b2) * w + b1) * w + b0;

    if (x <= -XMAX) {
        y1 = ymin;
        z1 = ymin * (xd + xmax);
    } else if (x >= XMAX) {
        y1 = ymax;
        z1 = z_at_xmax + ymax * (xd - xmax);
    } else if (xd == 0.0) {
        y1 = 0.0;
    }

    const double dx0 = xd - st->prev_x;
    const double reldx0 = fabs(dx0) / (fabs(xd + st->prev_x) + 1e-7);
    /* The generated C tables store float coefficients; near zero their
       antiderivative difference is less reliable than the direct-value limit. */
    const int use_average = reldx0 < 0.0001 || fabs(dx0) < 0.001;
    const double out = use_average ? 0.5 * (y1 + st->prev_y) : (z1 - st->prev_z) / dx0;

    st->prev_x = xd;
    st->prev_y = y1;
    st->prev_z = z1;
    return (float)out;
}

static float pk_k1(float tau, double sr)
{
    return tau > 0.0f ? 1.0f - expf(-1.0f / (tau * (float)sr)) : 1.0f;
}

static float pk_k2(float tau, double sr)
{
    return tau > 0.0f ? expf(-1.0f / (tau * (float)sr)) : 0.0f;
}

PkdCoeffs pkd_coeffs(float attack, float release, double sr)
{
    return (PkdCoeffs) { pk_k1(attack, sr), pk_k2(release, sr) };
}

Ii1Coeffs tube_advk_coeffs(const StageCfg *cfg, double sr)
{
    const float avg_tau = fmaxf(1e-10f, cfg->avg_tau);
    const float avg_f = 1.0f / (float)(2.0 * M_PI * (double)avg_tau);
    return ii1_lp_coeffs(avg_f, sr);
}

void tube_ck_process(TubeCk *st, const StageCfg *cfg, PkdCoeffs pk_coeffs,
                            Df2Coeffs adnl_eq, Ii1Coeffs advk_coeffs,
                            float v, float dvs, float *v_out, float *dia)
{
    const float v1 = v + st->advk;
    const float v2 = v1 * cfg->kpre;
    const float v3 = v2 / (1.0f + cfg->kspre * dvs);
    const float pk = pkd_coeffs_process(&st->pkd, cfg->pk_xth, cfg->pk_xdiode, pk_coeffs, v3);
    const float v4 = v3 - cfg->kpk * pk;
    const float v5 = adnl_process(&st->adnl, cfg->table, cfg->len, v4);
    const float v6 = df2_coeffs_process(&st->neq, adnl_eq, v5);
    const float v7 = v6 * (1.0f + cfg->kspost * dvs);
    const float v8 = v7 * cfg->isat;
    *dia = v8 + cfg->ksib * dvs;
    *v_out = *dia * (-cfg->rl) + cfg->ksva * dvs;
    st->advk = ii1_lp_process(&st->advk_lp, advk_coeffs, *v_out - dvs) * cfg->kfb;
}

void tube_cd_process(TubeCd *st, const StageCfg *cfg, PkdCoeffs pk_coeffs,
                            Df2Coeffs adnl_eq, float v, float dvs,
                            float *v_out, float *vk_out, float *dia)
{
    const float v2 = v * cfg->kpre;
    const float v3 = v2 / (1.0f + cfg->kspre * dvs);
    const float pk = pkd_coeffs_process(&st->pkd, cfg->pk_xth, cfg->pk_xdiode, pk_coeffs, v3);
    const float v4 = v3 - cfg->kpk * pk;
    const float v5 = adnl_process(&st->adnl, cfg->table, cfg->len, v4);
    const float v6 = df2_coeffs_process(&st->neq, adnl_eq, v5);
    const float v7 = v6 * (1.0f + cfg->kspost * dvs);
    const float v8 = v7 * cfg->isat;
    *dia = v8 + cfg->ksib * dvs;
    *vk_out = *dia * cfg->rkl + cfg->ksvk * dvs;
    *v_out = *dia * (-cfg->rl) + cfg->ksva * dvs;
}

void tube_ltp_process(TubeLtp *st, const LtpCfg *cfg,
                             PkdCoeffs pk1_coeffs, PkdCoeffs pk2_coeffs,
                             Df2Coeffs adnl_eq,
                             float vin1, float vink, float vin2, float dvs,
                             float *v_out, float *vout2, float *dia)
{
    float v = vin1 * cfg->kpre12 + vink * cfg->kprek2 + vin2 * cfg->kpre22;
    v /= 1.0f + cfg->kspre * dvs;
    if (cfg->kpk > 0.0f) {
        const float pk = pkd_process(&st->pkd2, cfg->pk_xth, cfg->pk_xdiode,
                                     pk2_coeffs.k1, pk2_coeffs.k2, v);
        v -= cfg->kpk * pk;
    }
    v = adnl_process(&st->adnl2, cfg->table2, cfg->len2, v);
    v = df2_coeffs_process(&st->neq2, adnl_eq, v);
    v *= 1.0f + cfg->kspost * dvs;
    v *= cfg->isat;
    v += cfg->ksib * dvs;
    *dia = v;
    *vout2 = v * (-cfg->rl2) + cfg->ksv2 * dvs;

    v = vin1 * cfg->kpre11 + vink * cfg->kprek1 + vin2 * cfg->kpre21;
    v /= 1.0f + cfg->kspre * dvs;
    if (cfg->kpk > 0.0f) {
        const float pk = pkd_process(&st->pkd1, cfg->pk_xth, cfg->pk_xdiode,
                                     pk1_coeffs.k1, pk1_coeffs.k2, v);
        v -= cfg->kpk * pk;
    }
    v = adnl_process(&st->adnl1, cfg->table1, cfg->len1, v);
    v = df2_coeffs_process(&st->neq1, adnl_eq, v);
    v *= 1.0f + cfg->kspost * dvs;
    v *= cfg->isat;
    v += cfg->ksib * dvs;
    *dia += v;
    *v_out = v * (-cfg->rl1) + cfg->ksv1 * dvs;

    st->dia = *dia;
    st->vout2 = *vout2;
}

float tube_pss_process(TubePss *st, float r, Ii1Coeffs coeffs,
                              float snext, float dia, float dvs_in)
{
    st->s = ii1_lp_process(&st->s_lp, coeffs, dia + snext);
    const float dvs_filtered = ii1_lp_process(&st->dvs_lp, coeffs, dvs_in);
    return dvs_filtered - r * st->s;
}

static void nilamp_params_set_raw(NilampParams *params, uint32_t id, float value)
{
    if (!params) {
        return;
    }
    switch ((NilampParamId)id) {
    case NILAMP_PARAM_GAIN_DB:
        params->gain_db = value;
        break;
    case NILAMP_PARAM_VOLUME_PCT:
        params->volume_pct = value;
        break;
    case NILAMP_PARAM_BASS_PCT:
        params->bass_pct = value;
        break;
    case NILAMP_PARAM_MID_PCT:
        params->mid_pct = value;
        break;
    case NILAMP_PARAM_TREBLE_PCT:
        params->treble_pct = value;
        break;
    case NILAMP_PARAM_SAG_PCT:
        params->sag_pct = value;
        break;
    case NILAMP_PARAM_OUTPUT_GAIN_DB:
        params->output_gain_db = value;
        break;
    case NILAMP_PARAM_TONE_FMID_DBHZ:
        params->tone_fmid_dbhz = value;
        break;
    case NILAMP_PARAM_TONE_QMID_DB:
        params->tone_qmid_db = value;
        break;
    case NILAMP_PARAM_SPK_RES_GAIN1_DB:
        params->spk_res_gain1_db = value;
        break;
    case NILAMP_PARAM_SPK_RES_GAIN2_DB:
        params->spk_res_gain2_db = value;
        break;
    case NILAMP_PARAM_SPK_RES_FRES_DBHZ:
        params->spk_res_fres_dbhz = value;
        break;
    case NILAMP_PARAM_SPK_RES_QTS_DB:
        params->spk_res_qts_db = value;
        break;
    case NILAMP_PARAM_SPK_IND_GAIN1_DB:
        params->spk_ind_gain1_db = value;
        break;
    case NILAMP_PARAM_SPK_IND_GAIN2_DB:
        params->spk_ind_gain2_db = value;
        break;
    case NILAMP_PARAM_SPK_IND_FIND_DBHZ:
        params->spk_ind_find_dbhz = value;
        break;
    case NILAMP_PARAM_GAIN_COMP:
        params->gain_comp = value;
        break;
    case NILAMP_PARAM_TUBE1:
        params->tube1 = value;
        break;
    case NILAMP_PARAM_PHASE_SPLITTER:
        params->phase_splitter = value;
        break;
    case NILAMP_PARAM_BYPASS:
        params->bypass = value;
        break;
    case NILAMP_PARAM_COUNT:
    default:
        break;
    }
}

NilampParams nilamp_default_params(void)
{
    return nilamp_model_default_params(NILAMP_MODEL_DEFAULT);
}

NilampParams nilamp_model_default_params(NilampModelId model_id)
{
    NilampParams params = {0};
    uint32_t count = 0;
    const NilampControlSpec *controls = nilamp_model_control_specs(model_id, &count);
    for (uint32_t i = 0; i < count; i++) {
        nilamp_params_set_raw(&params, controls[i].id, controls[i].default_value);
    }
    return params;
}

NilampEngine *nilamp_engine_create(double sample_rate)
{
    return nilamp_engine_create_model(sample_rate, NILAMP_MODEL_DEFAULT);
}

NilampEngine *nilamp_engine_create_model(double sample_rate, NilampModelId model_id)
{
    NilampEngine *engine = calloc(1, sizeof(*engine));
    if (engine == NULL) {
        return NULL;
    }
    const NilampModelSpec *model = nilamp_find_model(model_id);
    if (model == NULL) {
        free(engine);
        return NULL;
    }
    const NilampTopologyOps *topology_ops = nilamp_topology_ops(model->topology);
    if (topology_ops == NULL ||
        topology_ops->accepts_model_data == NULL ||
        !topology_ops->accepts_model_data(model->topology_data)) {
        free(engine);
        return NULL;
    }
    engine->topology_state = calloc(1, topology_ops->state_size);
    engine->topology_coeffs = calloc(1, topology_ops->coeffs_size);
    if (engine->topology_state == NULL || engine->topology_coeffs == NULL) {
        free(engine->topology_state);
        free(engine->topology_coeffs);
        free(engine);
        return NULL;
    }
    engine->sr = sample_rate;
    engine->params = nilamp_model_default_params(model_id);
    engine->model = model;
    engine->topology_ops = topology_ops;
    nilamp_engine_reset(engine);
    return engine;
}

void nilamp_engine_destroy(NilampEngine *engine)
{
    if (engine == NULL) {
        return;
    }
    free(engine->topology_state);
    free(engine->topology_coeffs);
    free(engine);
}

/* Seed an Adnl ADAA history to the table-consistent state at x=0.
 *
 * The ADAA quotient (z(x) - z(prev_x)) / (x - prev_x) requires prev_z to
 * equal z(prev_x). The table's antiderivative carries a non-zero constant of
 * integration (e.g. T1 12AX7 has z(0) ~= -6.76), so a zero-init produces an
 * enormous spurious quotient on the first non-zero sample. JSFX seeds
 * (x0, y0, z0) = (0, y(0), z(0)) at table-build time
 * (vendor/keller-jsfx/Libs/HK_LIB_ADNL.jsfx-inc:181-184); we replicate that
 * after the engine zero-init.
 */
void adnl_seed_at_zero(Adnl *st, const float *table)
{
    const int idx_zero = (int)(XMAX / DX);
    const size_t base = (size_t)idx_zero * 9u;
    st->prev_x = 0.0f;
    /* ideal y(0); the generated table row has tiny numerical residue */
    st->prev_y = 0.0;
    st->prev_z = table[base + 8u]; /* b0 at x=0 = z(0) */
}

int nilamp_enum_param(float value, int count, int fallback)
{
    if (!isfinite(value)) {
        return fallback;
    }
    const int index = (int)lroundf(value);
    return index >= 0 && index < count ? index : fallback;
}

void nilamp_engine_reset(NilampEngine *engine)
{
    if (engine == NULL) {
        return;
    }
    engine->has_processed = false;
    if (engine->topology_ops != NULL && engine->topology_ops->reset != NULL) {
        engine->topology_ops->reset(engine->topology_state, engine->topology_coeffs,
                                    &engine->params, engine->model->topology_data,
                                    engine->sr);
    }
}

void nilamp_engine_set_params(NilampEngine *engine, const NilampParams *params)
{
    if (engine == NULL || params == NULL) {
        return;
    }
    engine->params = *params;
    if (engine->topology_ops != NULL && engine->topology_ops->set_params != NULL) {
        engine->topology_ops->set_params(engine->topology_state, engine->topology_coeffs,
                                         &engine->params, engine->model->topology_data,
                                         engine->sr, engine->has_processed);
    }
}

NilampModelId nilamp_engine_model_id(const NilampEngine *engine)
{
    return engine != NULL && engine->model != NULL ? engine->model->id : NILAMP_MODEL_DEFAULT;
}

const char *nilamp_model_name(NilampModelId model_id)
{
    const NilampModelSpec *model = nilamp_find_model(model_id);
    return model != NULL ? model->name : "";
}

const NilampGuiLayoutSpec *nilamp_model_gui_layout(NilampModelId model_id)
{
    const NilampModelSpec *model = nilamp_find_model(model_id);
    return model != NULL ? model->gui_layout : NULL;
}

const NilampControlSpec *nilamp_control_specs(uint32_t *count)
{
    return nilamp_model_control_specs(NILAMP_MODEL_DEFAULT, count);
}

const NilampControlSpec *nilamp_model_control_specs(NilampModelId model_id, uint32_t *count)
{
    if (count) {
        *count = 0u;
    }
    const NilampModelSpec *model = nilamp_find_model(model_id);
    if (model == NULL) {
        return NULL;
    }
    if (count) {
        *count = model->control_count;
    }
    return model->controls;
}

const NilampControlSpec *nilamp_control_spec(uint32_t id)
{
    uint32_t count = 0;
    const NilampControlSpec *specs = nilamp_control_specs(&count);
    for (uint32_t i = 0; i < count; i++) {
        if (specs[i].id == id) {
            return &specs[i];
        }
    }
    return NULL;
}

float nilamp_control_display_value(const NilampControlSpec *spec, float raw_value)
{
    if (!spec) {
        return raw_value;
    }
    return spec->display == NILAMP_CONTROL_DISPLAY_ISO266 ? iso266(raw_value) : raw_value;
}

static NilampTapFrame nilamp_engine_process_sample(NilampEngine *engine, float input)
{
    if (engine->topology_ops != NULL && engine->topology_ops->process_sample != NULL) {
        return engine->topology_ops->process_sample(engine->topology_state,
                                                    engine->topology_coeffs,
                                                    &engine->params,
                                                    engine->model->topology_data,
                                                    input);
    }
    return (NilampTapFrame) { 0 };
}

void nilamp_engine_process(NilampEngine *engine, const float *input, float *output, uint32_t nframes)
{
    if (engine == NULL || input == NULL || output == NULL) {
        return;
    }
    for (uint32_t i = 0; i < nframes; i++) {
        const float x = isfinite(input[i]) ? input[i] : 0.0f;
        const NilampTapFrame taps = nilamp_engine_process_sample(engine, x);
        if (!nilamp_tap_frame_is_finite(taps)) {
            nilamp_engine_reset(engine);
            output[i] = 0.0f;
        } else {
            output[i] = taps.v_out;
        }
    }
    if (nframes > 0u) {
        engine->has_processed = true;
    }
}

void nilamp_engine_process_taps(NilampEngine *engine, const float *input, float *outputs[NILAMP_NUM_TAPS], uint32_t nframes)
{
    if (engine == NULL || input == NULL || outputs == NULL) {
        return;
    }
    for (uint32_t i = 0; i < nframes; i++) {
        const float x = isfinite(input[i]) ? input[i] : 0.0f;
        NilampTapFrame taps = nilamp_engine_process_sample(engine, x);
        if (!nilamp_tap_frame_is_finite(taps)) {
            nilamp_engine_reset(engine);
            taps = (NilampTapFrame) { 0 };
        }
        outputs[0][i] = taps.v_out;
        outputs[1][i] = taps.res1_v;
        outputs[2][i] = taps.res3_v;
        outputs[3][i] = taps.res4_v;
        outputs[4][i] = taps.drive_t4;
        outputs[5][i] = taps.res5_v;
        outputs[6][i] = taps.res_t5_v;
        outputs[7][i] = taps.dvs2;
        outputs[8][i] = taps.dvs3;
        outputs[9][i] = taps.p2_s;
        outputs[10][i] = taps.p3_s;
        outputs[11][i] = taps.drive_t5;
        outputs[12][i] = taps.post_pp;
        outputs[13][i] = taps.post_peq3;
        outputs[14][i] = taps.post_hs3;
        outputs[15][i] = taps.post_hp5;
        outputs[16][i] = taps.t4_advk_in;
        outputs[17][i] = taps.t5_advk_in;
        outputs[18][i] = taps.t4_dia;
        outputs[19][i] = taps.t5_dia;
        outputs[20][i] = taps.t4_advk_out;
        outputs[21][i] = taps.t5_advk_out;
        outputs[22][i] = taps.dia1_next;
    }
    if (nframes > 0u) {
        engine->has_processed = true;
    }
}

#ifdef NILAMP_ENABLE_TEST_API
void nilamp_test_flt_ii1_lp(float f, double sample_rate, const float *input, float *output, size_t n)
{
    Ii1 st = { 0 };
    const Ii1Coeffs coeffs = ii1_lp_coeffs(f, sample_rate);
    for (size_t i = 0; i < n; i++) {
        output[i] = ii1_lp_process(&st, coeffs, input[i]);
    }
}

void nilamp_test_flt_ii1_hp(float f, double sample_rate, const float *input, float *output, size_t n)
{
    Ii1 st = { 0 };
    const Ii1Coeffs coeffs = ii1_lp_coeffs(f, sample_rate);
    for (size_t i = 0; i < n; i++) {
        output[i] = ii1_hp_process(&st, coeffs, input[i]);
    }
}

void nilamp_test_flt_df2_lp_keller(double sample_rate, const float *input, float *output, size_t n)
{
    Df2 st = { 0 };
    const Df2Coeffs coeffs = df2_lp_coeffs(sample_rate, 10000.0f, sqrtf(0.5f), false);
    for (size_t i = 0; i < n; i++) {
        output[i] = df2_coeffs_process(&st, coeffs, input[i]);
    }
}

void nilamp_test_flt_sv2_tst(double sample_rate, const float *input, float *output, size_t n)
{
    Svf2 st = { 0 };
    const Svf2Coeffs coeffs =
        svf2_tst_coeffs(0.25f, 0.25f, 0.25f, 500.0f, 0.5f, sample_rate);
    for (size_t i = 0; i < n; i++) {
        output[i] = svf2_coeffs_process(&st, coeffs, input[i]);
    }
}

void nilamp_test_pkd(float xth, float xdiode, float k1, float k2, const float *input, float *output, size_t n)
{
    Pkd st = { 0 };
    for (size_t i = 0; i < n; i++) {
        output[i] = pkd_process(&st, xth, xdiode, k1, k2, input[i]);
    }
}

void nilamp_test_adnl(NilampTestAdnlTable table, const float *input, float *output, size_t n)
{
    const float *coeffs = nilamp_t1_12ax7_table;
    size_t len = nilamp_t1_12ax7_table_len;
    switch (table) {
    case NILAMP_TEST_ADNL_T1_12AX7:
        coeffs = nilamp_t1_12ax7_table;
        len = nilamp_t1_12ax7_table_len;
        break;
    case NILAMP_TEST_ADNL_T2_12AX7:
        coeffs = nilamp_t2_12ax7_table;
        len = nilamp_t2_12ax7_table_len;
        break;
    case NILAMP_TEST_ADNL_T3_CD:
        coeffs = nilamp_t3_cd_table;
        len = nilamp_t3_cd_table_len;
        break;
    case NILAMP_TEST_ADNL_T4_6V6:
        coeffs = nilamp_t4_6v6_table;
        len = nilamp_t4_6v6_table_len;
        break;
    case NILAMP_TEST_ADNL_T5_6V6:
        coeffs = nilamp_t5_6v6_table;
        len = nilamp_t5_6v6_table_len;
        break;
    }

    Adnl st = { 0 };
    adnl_seed_at_zero(&st, coeffs);
    for (size_t i = 0; i < n; i++) {
        output[i] = adnl_process(&st, coeffs, len, input[i]);
    }
}

void nilamp_test_filter_backend(double sample_rate, const float *input, float *outputs[NILAMP_TEST_NUM_BACKEND_FILTERS], size_t n)
{
    Ii1 hp3 = { 0 };
    Ii1 hp4 = { 0 };
    Svf2 peq1 = { 0 };
    Svf1 hs1 = { 0 };
    Svf2 peq1_hs1_peq = { 0 };
    Svf1 peq1_hs1_hs = { 0 };
    Ii1 t4_hp = { 0 };
    Svf2 t4_peq = { 0 };
    Svf1 t4_hs = { 0 };
    Ii1 t5_hp = { 0 };
    Svf2 t5_peq = { 0 };
    Svf1 t5_hs = { 0 };

    const Ii1Coeffs hp3_coeffs = ii1_lp_coeffs(5.8f, sample_rate);
    const Ii1Coeffs hp4_coeffs = ii1_lp_coeffs(6.4f, sample_rate);
    const Svf2Coeffs peq_coeffs =
        svf2_peq_coeffs(1.1220184543f, 80.0f, 2.6685237666f, sample_rate);
    const Svf1HsCoeffs hs_coeffs =
        svf1_hs_coeffs(1.4125375446f, 2098.1359672f, sample_rate);

    for (size_t i = 0; i < n; i++) {
        outputs[0][i] = ii1_hp_process(&hp3, hp3_coeffs, input[i]);
        outputs[1][i] = ii1_hp_process(&hp4, hp4_coeffs, input[i]);
        outputs[2][i] = svf2_coeffs_process(&peq1, peq_coeffs, input[i]);
        outputs[3][i] = svf1_hs_coeffs_process(&hs1, hs_coeffs, input[i]);
        const float peq_hs = svf2_coeffs_process(&peq1_hs1_peq, peq_coeffs, input[i]);
        outputs[4][i] = svf1_hs_coeffs_process(&peq1_hs1_hs, hs_coeffs, peq_hs);
        float t4 = ii1_hp_process(&t4_hp, hp3_coeffs, input[i] * 0.797f);
        t4 = svf2_coeffs_process(&t4_peq, peq_coeffs, t4);
        outputs[5][i] = svf1_hs_coeffs_process(&t4_hs, hs_coeffs, t4);
        float t5 = ii1_hp_process(&t5_hp, hp4_coeffs, input[i] * 0.940f);
        t5 = svf2_coeffs_process(&t5_peq, peq_coeffs, t5);
        outputs[6][i] = svf1_hs_coeffs_process(&t5_hs, hs_coeffs, t5);
    }
}

#endif
