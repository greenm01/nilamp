// SPDX-License-Identifier: MIT

#include "nilamp_dsp.h"

#include "nilamp_tables.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TBL_SIZE 13503
#define XMAX 15.0f
#define DX 0.02f

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
    float prev_x;
    float prev_y;
    float prev_z;
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
    Ii1 s_lp;
    Ii1 dvs_lp;
    float s;
} TubePss;

struct NilampEngine {
    double sr;
    NilampParams params;

    TubeCk t1;
    TubeCk t2;
    TubeCd t3;
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
};

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
} StageCfg;

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

static const StageCfg T1 = {
    nilamp_t1_12ax7_table, 13503, 0.372960373f, 0.00165f, 100000.0f, 0.0f,
    0.004201680672f, 0.004201680672f, 0.3846153846f, 0.0f, 3.193277311e-06f,
    0.01515f, 0.0f, 0.25f, 0.25f, 0.01f, 0.05f, 0.0375f,
};
static const StageCfg T2 = {
    nilamp_t2_12ax7_table, 13503, 0.3970223325f, 0.00155f, 100000.0f, 0.0f,
    0.004201680672f, 0.004201680672f, 0.3846153846f, 0.0f, 3.193277311e-06f,
    0.01515f, 0.05f, 0.255f, 0.57f, 0.015f, 0.05f, 0.0375f,
};
static const StageCfg T3 = {
    nilamp_t3_cd_table, 13503, 0.01054674317f, 0.0016f, 56000.0f, 57500.0f,
    0.004201680672f, 0.004201680672f, 0.8282208589f, 0.1763803681f,
    3.067226891e-06f, 0.0f, 0.125f, 0.272f, 0.394f, 0.00085f, 0.3872f, 0.0f,
};
static const StageCfg T4 = {
    nilamp_t4_6v6_table, 13503, 0.02642706131f, 0.11f, 3000.0f, 0.0f,
    0.0f, 0.00289017341f, 0.9302325581f, 0.0f, 0.0001213872832f,
    0.18144f, 0.125f, 0.309f, 0.437f, 0.00575f, 0.0276f, 0.00675f,
};
static const StageCfg T5 = {
    nilamp_t5_6v6_table, 13503, 0.0242248062f, 0.12f, 3000.0f, 0.0f,
    0.0f, 0.00289017341f, 0.9302325581f, 0.0f, 0.0001213872832f,
    0.18144f, 0.18f, 0.325f, 0.388f, 0.00155f, 0.0234f, 0.00675f,
};
#ifdef NILAMP_ENABLE_TEST_API
static const StageCfg T2_DZ = {
    nilamp_t2_12ax7_table_dz, 13503, 0.3970223325f, 0.00155f, 100000.0f, 0.0f,
    0.004201680672f, 0.004201680672f, 0.3846153846f, 0.0f, 3.193277311e-06f,
    0.01515f, 0.05f, 0.255f, 0.57f, 0.015f, 0.05f, 0.0375f,
};
static const StageCfg T3_DZ = {
    nilamp_t3_cd_table_dz, 13503, 0.01054674317f, 0.0016f, 56000.0f, 57500.0f,
    0.004201680672f, 0.004201680672f, 0.8282208589f, 0.1763803681f,
    3.067226891e-06f, 0.0f, 0.125f, 0.272f, 0.394f, 0.00085f, 0.3872f, 0.0f,
};
#endif

static float db_to_linear(float db)
{
    return powf(10.0f, db / 20.0f);
}

static float ii1_lp_process(Ii1 *st, float f, double sr, float x)
{
    const float k = 1.0f - expf((float)(-2.0 * M_PI * (double)f / sr));
    st->s = (x - st->s) * k + st->s;
    return st->s;
}

static float ii1_hp_process(Ii1 *st, float f, double sr, float x)
{
    return x - ii1_lp_process(st, f, sr, x);
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

static float svf2_tst(Svf2 *st, double sr, float b, float m, float t, float f, float q, float x)
{
    const float kq = sv2_kq(f, q, sr, 1);
    const float k = sv2_k(f, sr, 1);
    const float kf = kq + k;
    const float kdiv = 1.0f / (1.0f + k * (k + kq));
    float hp, bp, lp;
    svf2_process(st, k, kf, kdiv, x, &hp, &bp, &lp);
    return hp * t + bp * (kq * m) + lp * b;
}

static float svf2_peq(Svf2 *st, double sr, float kgain, float f, float qc, float x)
{
    const float sqrt_gain = sqrtf(kgain);
    const float kq = sv2_kq(f, qc * sqrt_gain, sr, 1);
    const float kq_val = kq;
    const float k = sv2_k(f, sr, 1);
    const float kf = kq_val + k;
    const float kdiv = 1.0f / (1.0f + k * (k + kq_val));
    float hp, bp, lp;
    svf2_process(st, k, kf, kdiv, x, &hp, &bp, &lp);
    return hp + bp * (kq_val * kgain) + lp;
}

static float svf1_hs(Svf1 *st, double sr, float kgain, float fs, float x)
{
    const float k_raw = tanf((float)(M_PI * (double)fs / sr));
    const float k = sqrtf(kgain) * k_raw;
    const float kdiv = 1.0f / (1.0f + k);
    const float hp = (x - st->s1) * kdiv;
    const float v = k * hp;
    const float lp = v + st->s1;
    st->s1 = v + lp;
    return hp * kgain + lp;
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

static float adnl_eq_process(Df2 *st, double sr, float x)
{
    float b0, b1, b2, a1, a2;
    adnl_eq_coeffs(adnl_eq_index(sr), &b0, &b1, &b2, &a1, &a2);
    return df2_process(st, b0, b1, b2, a1, a2, x);
}

static float df2_lp(Df2 *st, double sr, float f, float q, float x)
{
    const float pi_t = (float)(M_PI / sr);
    const float k0 = f * pi_t;
    const float aux1 = sqrtf(1.0f + 4.0f * q * q);
    const float aux2 = (k0 / sinf(2.0f * k0 + 1e-20f)) * logf((aux1 + 1.0f) / (aux1 - 1.0f));
    const float kq0 = expf(aux2) - expf(-aux2);
    const float k = tanf(f * pi_t);
    const float kq = k * kq0;
    const float ksqr = k * k;
    const float kdiv = 1.0f / (1.0f + kq + ksqr);
    const float a1 = (-2.0f + 2.0f * ksqr) * kdiv;
    const float a2 = (1.0f - kq + ksqr) * kdiv;
    const float b0 = ksqr * kdiv;
    const float b1 = 2.0f * ksqr * kdiv;
    const float b2 = b0;
    return df2_process(st, b0, b1, b2, a1, a2, x);
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

static float adnl_process(Adnl *st, const float *table, size_t table_len, float x)
{
    const int num_segments = (int)(2.0f * XMAX / DX);
    int index = (int)((x + XMAX) / DX);
    if (index < 0) {
        index = 0;
    } else if (index > num_segments - 1) {
        index = num_segments - 1;
    }

    const float w = x + XMAX - (float)index * DX;
    const size_t base = (size_t)index * 9u;
    const float a3 = table[base + 0u];
    const float a2 = table[base + 1u];
    const float a1 = table[base + 2u];
    const float a0 = table[base + 3u];
    const float b4 = table[base + 4u];
    const float b3 = table[base + 5u];
    const float b2 = table[base + 6u];
    const float b1 = table[base + 7u];
    const float b0 = table[base + 8u];

    const float ymin = table[table_len - 3u];
    const float ymax = table[table_len - 2u];
    const float z_at_xmax = table[table_len - 1u];

    float y1 = ((a3 * w + a2) * w + a1) * w + a0;
    float z1 = (((b4 * w + b3) * w + b2) * w + b1) * w + b0;

    if (x <= -XMAX) {
        y1 = ymin;
        z1 = ymin * (x + XMAX);
    } else if (x >= XMAX) {
        y1 = ymax;
        z1 = z_at_xmax + ymax * (x - XMAX);
    }

    const float dx0 = x - st->prev_x;
    const float reldx0 = fabsf(dx0) / (fabsf(x + st->prev_x) + 1e-7f);
    const float out = (reldx0 < 0.0001f) ? 0.5f * (y1 + st->prev_y) : (z1 - st->prev_z) / dx0;

    st->prev_x = x;
    st->prev_y = y1;
    st->prev_z = z1;
    return out;
}

static float pk_k1(float tau, double sr)
{
    return tau > 0.0f ? 1.0f - expf(-1.0f / (tau * (float)sr)) : 1.0f;
}

static float pk_k2(float tau, double sr)
{
    return tau > 0.0f ? expf(-1.0f / (tau * (float)sr)) : 0.0f;
}

static void tube_ck_process(TubeCk *st, const StageCfg *cfg, double sr, float v, float dvs, float *v_out, float *dia)
{
    const float v1 = v + st->advk;
    const float v2 = v1 * cfg->kpre;
    const float v3 = v2 / (1.0f + cfg->kspre * dvs);
    const float pk = pkd_process(&st->pkd, cfg->pk_xth, cfg->pk_xdiode, pk_k1(cfg->pk_attack, sr), pk_k2(cfg->pk_release, sr), v3);
    const float v4 = v3 - cfg->kpk * pk;
    const float v5 = adnl_process(&st->adnl, cfg->table, cfg->len, v4);
    const float v6 = adnl_eq_process(&st->neq, sr, v5);
    const float v7 = v6 * (1.0f + cfg->kspost * dvs);
    const float v8 = v7 * cfg->isat;
    *dia = v8 + cfg->ksib * dvs;
    *v_out = *dia * (-cfg->rl) + cfg->ksva * dvs;
    const float avg_f = 1.0f / (float)(2.0 * M_PI * (double)cfg->avg_tau);
    st->advk = ii1_lp_process(&st->advk_lp, avg_f, sr, *v_out - dvs) * cfg->kfb;
}

static void tube_cd_process(TubeCd *st, const StageCfg *cfg, double sr, float v, float dvs, float *v_out, float *vk_out, float *dia)
{
    const float v2 = v * cfg->kpre;
    const float v3 = v2 / (1.0f + cfg->kspre * dvs);
    const float pk = pkd_process(&st->pkd, cfg->pk_xth, cfg->pk_xdiode, pk_k1(cfg->pk_attack, sr), pk_k2(cfg->pk_release, sr), v3);
    const float v4 = v3 - cfg->kpk * pk;
    const float v5 = adnl_process(&st->adnl, cfg->table, cfg->len, v4);
    const float v6 = adnl_eq_process(&st->neq, sr, v5);
    const float v7 = v6 * (1.0f + cfg->kspost * dvs);
    const float v8 = v7 * cfg->isat;
    *dia = v8 + cfg->ksib * dvs;
    *vk_out = *dia * cfg->rkl + cfg->ksvk * dvs;
    *v_out = *dia * (-cfg->rl) + cfg->ksva * dvs;
}

static float tube_pss_process(TubePss *st, float r, float tau, double sr, float snext, float dia, float dvs_in)
{
    const float f = 1.0f / (float)(2.0 * M_PI * fmax(1e-10, (double)tau));
    st->s = ii1_lp_process(&st->s_lp, f, sr, dia + snext);
    const float dvs_filtered = ii1_lp_process(&st->dvs_lp, f, sr, dvs_in);
    return dvs_filtered - r * st->s;
}

NilampParams nilamp_default_params(void)
{
    return (NilampParams) {
        .gain_db = 0.0f,
        .volume_pct = 50.0f,
        .bass_pct = 50.0f,
        .mid_pct = 50.0f,
        .treble_pct = 50.0f,
        .sag_pct = 50.0f,
    };
}

NilampEngine *nilamp_engine_create(double sample_rate)
{
    NilampEngine *engine = calloc(1, sizeof(*engine));
    if (engine == NULL) {
        return NULL;
    }
    engine->sr = sample_rate;
    engine->params = nilamp_default_params();
    return engine;
}

void nilamp_engine_destroy(NilampEngine *engine)
{
    free(engine);
}

void nilamp_engine_reset(NilampEngine *engine)
{
    if (engine == NULL) {
        return;
    }
    const double sr = engine->sr;
    const NilampParams params = engine->params;
    memset(engine, 0, sizeof(*engine));
    engine->sr = sr;
    engine->params = params;
}

void nilamp_engine_set_params(NilampEngine *engine, const NilampParams *params)
{
    if (engine != NULL && params != NULL) {
        engine->params = *params;
    }
}

static NilampTapFrame nilamp_engine_process_sample(NilampEngine *engine, float input)
{
    /* Keller g1 uses sqrt(1.2); REAPER's mono JSFX render path contributes
       the 0.5 feed factor that the parity harness measures at the first tap. */
    const float gain = db_to_linear(engine->params.gain_db) * 0.5f * sqrtf(1.2f);
    const float volume = engine->params.volume_pct * 0.01f;
    const float bass = engine->params.bass_pct * 0.01f;
    const float mid = engine->params.mid_pct * 0.01f;
    const float treble = engine->params.treble_pct * 0.01f;
    const float sag = engine->params.sag_pct * 0.01f;

    engine->t4.advk = 0.5f * (engine->t4.advk + engine->t5.advk);
    engine->t5.advk = engine->t4.advk;
    const float t4_advk_in = engine->t4.advk;
    const float t5_advk_in = engine->t5.advk;

    const float old_s2 = engine->p2.s;
    const float old_s3 = engine->p3.s;

    const float dvs1 = tube_pss_process(&engine->p1, 125.0f, 0.008f, engine->sr, old_s2, engine->prev_dia1, 0.0f);
    const float dvs2 = tube_pss_process(&engine->p2, 5100.0f, 0.0816f, engine->sr, old_s3, engine->prev_dig, dvs1);
    const float dvs3 = tube_pss_process(&engine->p3, sag * 22000.0f, 0.352f, engine->sr, 0.0f, engine->prev_dia3, dvs2);
    const float p2_s = engine->p2.s;
    const float p3_s = engine->p3.s;

    float res1_v, res1_dia;
    tube_ck_process(&engine->t1, &T1, engine->sr, input * gain, dvs3, &res1_v, &res1_dia);

    float v2 = ii1_hp_process(&engine->hp1, 10.0f, engine->sr, res1_v);
    v2 *= volume * volume;
    v2 = svf2_tst(&engine->tone, engine->sr, bass * bass, mid * mid, treble * treble, 630.0f, 0.5f, v2);
    v2 = ii1_lp_process(&engine->lp1, 8800.0f, engine->sr, v2);

    float res3_v, res3_dia;
    tube_ck_process(&engine->t2, &T2, engine->sr, v2, dvs3, &res3_v, &res3_dia);
    const float res3_hp = ii1_hp_process(&engine->hp2, 0.41f, engine->sr, res3_v);

    float res4_v, res4_vk, res4_dia;
    tube_cd_process(&engine->t3, &T3, engine->sr, res3_hp, dvs3, &res4_v, &res4_vk, &res4_dia);

    float drive_t4 = res4_v * 0.797f;
    drive_t4 = ii1_hp_process(&engine->hp3, 5.8f, engine->sr, drive_t4);
    drive_t4 = svf2_peq(&engine->peq1_t4, engine->sr, 1.1220184543f, 80.0f, 2.6685237666f, drive_t4);
    drive_t4 = svf1_hs(&engine->hs1_t4, engine->sr, 1.4125375446f, 2098.1359672f, drive_t4);

    float res5_v, res5_dia;
    tube_ck_process(&engine->t4, &T4, engine->sr, drive_t4, dvs2, &res5_v, &res5_dia);
    const float t4_advk_out = engine->t4.advk;

    float aux = res4_vk * 0.940f;
    aux = ii1_hp_process(&engine->hp4, 6.4f, engine->sr, aux);
    aux = svf2_peq(&engine->peq1_t5, engine->sr, 1.1220184543f, 80.0f, 2.6685237666f, aux);
    aux = svf1_hs(&engine->hs1_t5, engine->sr, 1.4125375446f, 2098.1359672f, aux);
    const float drive_t5 = aux;

    float res_t5_v, res_t5_dia;
    tube_ck_process(&engine->t5, &T5, engine->sr, aux, dvs2, &res_t5_v, &res_t5_dia);
    const float t5_advk_out = engine->t5.advk;

    const float post_pp = res5_v - res_t5_v;
    const float post_peq3 = svf2_peq(&engine->peq3, engine->sr, 1.2589254118f, 80.0f, 2.2440931043f, post_pp);
    const float post_hs3 = svf1_hs(&engine->hs3, engine->sr, 1.4125375446f, 1485.8089753f, post_peq3);
    const float post_hp5 = ii1_hp_process(&engine->hp5, 40.0f, engine->sr, post_hs3);
    float v_out = post_hp5;
    v_out = df2_lp(&engine->lp2, engine->sr, 10000.0f, sqrtf(0.5f), v_out);
    v_out *= 0.5f / (T4.rl * T4.isat + T5.rl * T5.isat);

    const float dia1_next = res5_dia + res_t5_dia;
    engine->prev_dia1 = dia1_next;
    engine->prev_dig = 0.025f * engine->prev_dia1;
    engine->prev_dia3 = res1_dia + res3_dia + res4_dia;

    return (NilampTapFrame) {
        .v_out = v_out,
        .res1_v = res1_v,
        .res3_v = res3_v,
        .res4_v = res4_v,
        .drive_t4 = drive_t4,
        .res5_v = res5_v,
        .res_t5_v = res_t5_v,
        .dvs2 = dvs2,
        .dvs3 = dvs3,
        .p2_s = p2_s,
        .p3_s = p3_s,
        .drive_t5 = drive_t5,
        .post_pp = post_pp,
        .post_peq3 = post_peq3,
        .post_hs3 = post_hs3,
        .post_hp5 = post_hp5,
        .t4_advk_in = t4_advk_in,
        .t5_advk_in = t5_advk_in,
        .t4_dia = res5_dia,
        .t5_dia = res_t5_dia,
        .t4_advk_out = t4_advk_out,
        .t5_advk_out = t5_advk_out,
        .dia1_next = dia1_next,
    };
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
}

#ifdef NILAMP_ENABLE_TEST_API
void nilamp_test_flt_ii1_lp(float f, double sample_rate, const float *input, float *output, size_t n)
{
    Ii1 st = { 0 };
    for (size_t i = 0; i < n; i++) {
        output[i] = ii1_lp_process(&st, f, sample_rate, input[i]);
    }
}

void nilamp_test_flt_ii1_hp(float f, double sample_rate, const float *input, float *output, size_t n)
{
    Ii1 st = { 0 };
    for (size_t i = 0; i < n; i++) {
        output[i] = ii1_hp_process(&st, f, sample_rate, input[i]);
    }
}

void nilamp_test_flt_sv2_tst(double sample_rate, const float *input, float *output, size_t n)
{
    Svf2 st = { 0 };
    for (size_t i = 0; i < n; i++) {
        output[i] = svf2_tst(&st, sample_rate, 0.25f, 0.25f, 0.25f, 500.0f, 0.5f, input[i]);
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

    for (size_t i = 0; i < n; i++) {
        outputs[0][i] = ii1_hp_process(&hp3, 5.8f, sample_rate, input[i]);
        outputs[1][i] = ii1_hp_process(&hp4, 6.4f, sample_rate, input[i]);
        outputs[2][i] = svf2_peq(&peq1, sample_rate, 1.1220184543f, 80.0f, 2.6685237666f, input[i]);
        outputs[3][i] = svf1_hs(&hs1, sample_rate, 1.4125375446f, 2098.1359672f, input[i]);
        const float peq_hs = svf2_peq(&peq1_hs1_peq, sample_rate, 1.1220184543f, 80.0f, 2.6685237666f, input[i]);
        outputs[4][i] = svf1_hs(&peq1_hs1_hs, sample_rate, 1.4125375446f, 2098.1359672f, peq_hs);
        float t4 = ii1_hp_process(&t4_hp, 5.8f, sample_rate, input[i] * 0.797f);
        t4 = svf2_peq(&t4_peq, sample_rate, 1.1220184543f, 80.0f, 2.6685237666f, t4);
        outputs[5][i] = svf1_hs(&t4_hs, sample_rate, 1.4125375446f, 2098.1359672f, t4);
        float t5 = ii1_hp_process(&t5_hp, 6.4f, sample_rate, input[i] * 0.940f);
        t5 = svf2_peq(&t5_peq, sample_rate, 1.1220184543f, 80.0f, 2.6685237666f, t5);
        outputs[6][i] = svf1_hs(&t5_hs, sample_rate, 1.4125375446f, 2098.1359672f, t5);
    }
}

static void test_tube_ck(const StageCfg *cfg, double sample_rate, const float *input, float *v_out, float *dia, size_t n)
{
    TubeCk st = { 0 };
    for (size_t i = 0; i < n; i++) {
        tube_ck_process(&st, cfg, sample_rate, input[i], 0.0f, &v_out[i], &dia[i]);
    }
}

void nilamp_test_tube_ck_t2(double sample_rate, const float *input, float *v_out, float *dia, size_t n)
{
    test_tube_ck(&T2, sample_rate, input, v_out, dia, n);
}

void nilamp_test_tube_ck_t5(double sample_rate, const float *input, float *v_out, float *dia, size_t n)
{
    test_tube_ck(&T5, sample_rate, input, v_out, dia, n);
}

void nilamp_test_tube_ck_t2_dz(double sample_rate, const float *input, float *v_out, float *dia, size_t n)
{
    test_tube_ck(&T2_DZ, sample_rate, input, v_out, dia, n);
}

static void test_tube_cd(const StageCfg *cfg, double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n)
{
    TubeCd st = { 0 };
    for (size_t i = 0; i < n; i++) {
        tube_cd_process(&st, cfg, sample_rate, input[i], 0.0f, &v_out[i], &vk_out[i], &dia[i]);
    }
}

void nilamp_test_tube_cd_t3(double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n)
{
    test_tube_cd(&T3, sample_rate, input, v_out, vk_out, dia, n);
}

void nilamp_test_tube_cd_t3_dz(double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n)
{
    test_tube_cd(&T3_DZ, sample_rate, input, v_out, vk_out, dia, n);
}

void nilamp_test_power_pair(double sample_rate, const float *t3_v, const float *t3_vk, float *outputs[NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS], size_t n)
{
    Ii1 hp3 = { 0 };
    Svf2 peq_t4 = { 0 };
    Svf1 hs_t4 = { 0 };
    Ii1 hp4 = { 0 };
    Svf2 peq_t5 = { 0 };
    Svf1 hs_t5 = { 0 };
    TubeCk t4 = { 0 };
    TubeCk t5 = { 0 };

    for (size_t i = 0; i < n; i++) {
        float t4_in = ii1_hp_process(&hp3, 5.8f, sample_rate, t3_v[i] * 0.797f);
        t4_in = svf2_peq(&peq_t4, sample_rate, 1.1220184543f, 80.0f, 2.6685237666f, t4_in);
        t4_in = svf1_hs(&hs_t4, sample_rate, 1.4125375446f, 2098.1359672f, t4_in);

        float t5_in = ii1_hp_process(&hp4, 6.4f, sample_rate, t3_vk[i] * 0.940f);
        t5_in = svf2_peq(&peq_t5, sample_rate, 1.1220184543f, 80.0f, 2.6685237666f, t5_in);
        t5_in = svf1_hs(&hs_t5, sample_rate, 1.4125375446f, 2098.1359672f, t5_in);

        float t4_dia;
        float t5_dia;
        tube_ck_process(&t4, &T4, sample_rate, t4_in, 0.0f, &outputs[0][i], &t4_dia);
        tube_ck_process(&t5, &T5, sample_rate, t5_in, 0.0f, &outputs[1][i], &t5_dia);
        outputs[2][i] = outputs[0][i] - outputs[1][i];
        outputs[3][i] = t4_dia + t5_dia;
    }
}

void nilamp_test_pss(float r, float tau, double sample_rate, const float *dia, float *dvs, float *s, size_t n)
{
    TubePss st = { 0 };
    for (size_t i = 0; i < n; i++) {
        dvs[i] = tube_pss_process(&st, r, tau, sample_rate, 0.0f, dia[i], 0.0f);
        s[i] = st.s;
    }
}
#endif
