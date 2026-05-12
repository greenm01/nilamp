// SPDX-License-Identifier: MIT

#include "nilamp_topologies.h"

#include "nilamp_tables.h"

#include <math.h>
#include <stdbool.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* Keller TWD DLX II topology switches from
 * vendor/keller-jsfx/TWD DLX  II.jsfx: Tube 1 lines 276-284,
 * Splitter lines 292-334, LTP setup line 182.
 */
static const StageCfg T1_12AY7 = {
    nilamp_t1_12ay7_table, 13503, 0.16f, 0.0022f, 100000.0f, 820.0f,
    0.004201680672f, 0.004201680672f, 0.2f, 0.0f, 5.042016807e-06f,
    0.008386363636f, 0.0f, 0.25f, 0.25f, 0.01f, 0.05f, 0.0205f,
    NILAMP_STAGE_KIND_TRIODE_CK, NILAMP_DSP_METHOD_KELLER_GLF_ADAA,
};

static const StageCfg T5_CD_BAL = {
    nilamp_t4_6v6_table, 13503, 0.02642706131f, 0.11f, 3000.0f, 540.0f,
    0.0f, 0.00289017341f, 0.9302325581f, 0.0f, 0.0001213872832f,
    0.18144f, 0.125f, 0.309f, 0.437f, 0.00575f, 0.0276f, 0.00675f,
    NILAMP_STAGE_KIND_POWER_CK, NILAMP_DSP_METHOD_KELLER_GLF_ADAA,
};

static const StageCfg T4_LTP = {
    nilamp_t4_6v6_table, 13503, 0.02642706131f, 0.11f, 3000.0f, 540.0f,
    0.0f, 0.00289017341f, 0.9302325581f, 0.0f, 0.0001213872832f,
    0.18144f, 0.125f, 0.309f, 0.439f, 0.00594f, 0.0278f, 0.00675f,
    NILAMP_STAGE_KIND_POWER_CK, NILAMP_DSP_METHOD_KELLER_GLF_ADAA,
};

static const StageCfg T5_LTP = {
    nilamp_t4_6v6_table, 13503, 0.02642706131f, 0.11f, 3000.0f, 540.0f,
    0.0f, 0.00289017341f, 0.9302325581f, 0.0f, 0.0001213872832f,
    0.18144f, 0.125f, 0.317f, 0.4442f, 0.00663f, 0.0285f, 0.00675f,
    NILAMP_STAGE_KIND_POWER_CK, NILAMP_DSP_METHOD_KELLER_GLF_ADAA,
};

static const LtpCfg T6_LTP = {
    nilamp_t6_ltp1_table, 13503, nilamp_t6_ltp2_table, 13503,
    0.2060749075f, -0.2013672361f, 0.2055534423f, -0.2013672361f,
    -0.004707671307f, -0.004186206178f, 0.0016f, 82000.0f, 100000.0f,
    0.004201680672f, 0.004201680672f, 0.8090508876f, 0.7929294804f,
    3.109243697e-06f, 0.05f, 0.269f, 0.602f, 0.015f, 0.05f,
};

static const SplitterModeCfg TWEED_5E3_PP_SPLITTER_MODES[] = {
    {false, NULL, NULL, 0.797f, 0.940f, 0.41f, 5.8f, 6.4f, 0.0f, 0.0345f},
    {false, NULL, &T5_CD_BAL, 0.797f, 0.797f, 0.41f, 5.8f, 5.8f, 0.0f, 0.0345f},
    {true, &T4_LTP, &T5_LTP, 0.792f, 0.772f, 10.0f, 5.7f, 5.6f, 0.0f, 0.0345f},
    {true, &T4_LTP, &T5_LTP, 0.792f, 0.772f, 10.0f, 5.7f, 5.6f, 0.5f, 0.185741756f},
    {true, &T4_LTP, &T5_LTP, 0.792f, 0.772f, 10.0f, 5.7f, 5.6f, 1.0f, 1.0f},
};

#ifdef NILAMP_ENABLE_TEST_API
static const StageCfg T2_DZ = {
    nilamp_t2_12ax7_table_dz, 13503, 0.3970223325f, 0.00155f, 100000.0f, 0.0f,
    0.004201680672f, 0.004201680672f, 0.3846153846f, 0.0f, 3.193277311e-06f,
    0.01515f, 0.05f, 0.255f, 0.57f, 0.015f, 0.05f, 0.0375f,
    NILAMP_STAGE_KIND_TRIODE_CK, NILAMP_DSP_METHOD_DEMPWOLF_ZOLZER_ADAA,
};

static const StageCfg T3_DZ = {
    nilamp_t3_cd_table_dz, 13503, 0.01054674317f, 0.0016f, 56000.0f, 57500.0f,
    0.004201680672f, 0.004201680672f, 0.8282208589f, 0.1763803681f,
    3.067226891e-06f, 0.0f, 0.125f, 0.272f, 0.394f, 0.00085f, 0.3872f, 0.0f,
    NILAMP_STAGE_KIND_CATHODYNE, NILAMP_DSP_METHOD_DEMPWOLF_ZOLZER_ADAA,
};
#endif

static const NilampTweed5e3PpData *nilamp_tweed_5e3_pp_data(const void *topology_data)
{
    const NilampTweed5e3PpData *data = (const NilampTweed5e3PpData *)topology_data;
    return data != NULL && data->topology == NILAMP_TOPOLOGY_TWEED_5E3_CATHODYNE_PP ? data : NULL;
}

static bool nilamp_tweed_5e3_pp_accepts_model_data(const void *topology_data)
{
    return nilamp_tweed_5e3_pp_data(topology_data) != NULL;
}

#ifdef NILAMP_ENABLE_TEST_API
static const NilampTweed5e3PpData *nilamp_default_tweed_5e3_pp_data(void)
{
    const NilampModelSpec *model = nilamp_find_model(NILAMP_MODEL_DEFAULT);
    return model != NULL ? nilamp_tweed_5e3_pp_data(model->topology_data) : NULL;
}
#endif

static const StageCfg *nilamp_tube1_cfg(const NilampTweed5e3PpData *model, int tube1)
{
    return tube1 == 0 ? &T1_12AY7 : model->t1;
}

static const SplitterModeCfg *nilamp_splitter_cfg(int splitter)
{
    const int count = (int)(sizeof(TWEED_5E3_PP_SPLITTER_MODES) / sizeof(TWEED_5E3_PP_SPLITTER_MODES[0]));
    return &TWEED_5E3_PP_SPLITTER_MODES[nilamp_enum_param((float)splitter, count, 2)];
}

static const StageCfg *nilamp_splitter_t4_cfg(const SplitterModeCfg *splitter,
                                              const NilampTweed5e3PpData *model)
{
    return splitter->t4 != NULL ? splitter->t4 : model->t4;
}

static const StageCfg *nilamp_splitter_t5_cfg(const SplitterModeCfg *splitter,
                                              const NilampTweed5e3PpData *model)
{
    return splitter->t5 != NULL ? splitter->t5 : model->t5;
}

static Ii1Coeffs tube_pss_coeffs(float tau, double sr)
{
    const float f = 1.0f / (float)(2.0 * M_PI * fmax(1e-10, (double)tau));
    return ii1_lp_coeffs(f, sr);
}

static void nilamp_tweed_5e3_pp_apply_coeffs(NilampTweed5e3PpCoeffs *c,
                                             const NilampParams *params,
                                             const NilampTweed5e3PpData *model,
                                             double sr,
                                             bool snap)
{
    const int tube1 = nilamp_enum_param(params->tube1, 2, 1);
    const int splitter = nilamp_enum_param(params->phase_splitter,
                                           (int)(sizeof(TWEED_5E3_PP_SPLITTER_MODES) /
                                                 sizeof(TWEED_5E3_PP_SPLITTER_MODES[0])),
                                           2);
    const StageCfg *t1_cfg = nilamp_tube1_cfg(model, tube1);
    const SplitterModeCfg *splitter_cfg = nilamp_splitter_cfg(splitter);
    const StageCfg *t4_cfg = nilamp_splitter_t4_cfg(splitter_cfg, model);
    const StageCfg *t5_cfg = nilamp_splitter_t5_cfg(splitter_cfg, model);

    c->smooth_k = 1.0f / (0.01f * (float)sr);
    c->tube1_mode = tube1;
    c->splitter_mode = splitter;

    c->hp1 = ii1_lp_coeffs(10.0f, sr);
    c->lp1 = ii1_lp_coeffs(8800.0f, sr);
    c->hp2 = ii1_lp_coeffs(splitter_cfg->hp2, sr);
    c->hp3 = ii1_lp_coeffs(splitter_cfg->hp3, sr);
    c->hp4 = ii1_lp_coeffs(splitter_cfg->hp4, sr);
    c->hp5 = ii1_lp_coeffs(40.0f, sr);
    c->pss1_f = tube_pss_coeffs(model->pss1_tau, sr);
    c->pss2_f = tube_pss_coeffs(model->pss2_tau, sr);
    c->pss3_f = tube_pss_coeffs(model->pss3_tau, sr);
    c->advk_t1 = tube_advk_coeffs(t1_cfg, sr);
    c->advk_t2 = tube_advk_coeffs(model->t2, sr);
    c->advk_t4 = tube_advk_coeffs(t4_cfg, sr);
    c->advk_t5 = tube_advk_coeffs(t5_cfg, sr);

    c->lp2 = df2_lp_coeffs(sr, 10000.0f, sqrtf(0.5f), false);
    c->adnl_eq = adnl_eq_make_coeffs(sr);
    c->pk_t1 = pkd_coeffs(t1_cfg->pk_attack, t1_cfg->pk_release, sr);
    c->pk_t2 = pkd_coeffs(model->t2->pk_attack, model->t2->pk_release, sr);
    c->pk_t3 = pkd_coeffs(model->t3->pk_attack, model->t3->pk_release, sr);
    c->pk_t4 = pkd_coeffs(t4_cfg->pk_attack, t4_cfg->pk_release, sr);
    c->pk_t5 = pkd_coeffs(t5_cfg->pk_attack, t5_cfg->pk_release, sr);
    c->pk_ltp1 = pkd_coeffs(T6_LTP.pk_attack, T6_LTP.pk_release, sr);
    c->pk_ltp2 = c->pk_ltp1;

    const float gain =
        db_to_linear(params->gain_db + model->input_gain_offset_db) *
        model->input_feed_gain * sqrtf(model->input_keller_gain_sq);
    const float volume_knob = params->volume_pct * 0.01f;
    float volume_gain = volume_knob * volume_knob;
    const int gain_comp = (int)lroundf(params->gain_comp);
    if ((gain_comp == 1 || gain_comp == 3) && tube1 == 1) {
        volume_gain *= model->gain_comp_12ax7;
    }
    if ((gain_comp == 2 || gain_comp == 3) && splitter_cfg->is_ltp) {
        volume_gain *= powf(model->gain_comp_ltp_base, splitter_cfg->kmst);
    }
    float sag = params->sag_pct * 0.02f;
    if (!isfinite(sag)) {
        sag = 1.0f;
    } else if (sag < 0.0f) {
        sag = 0.0f;
    }
    sag *= sag;
    c->pss3_r = sag * model->pss3_r_at_full_sag;
    c->post_scale = 0.5f / (t4_cfg->rl * t4_cfg->isat + t5_cfg->rl * t5_cfg->isat);

    const float bass = params->bass_pct * 0.01f;
    const float mid = params->mid_pct * 0.01f;
    const float treble = params->treble_pct * 0.01f;
    const float tone_fmid = iso266(params->tone_fmid_dbhz);
    const float tone_qmid = iso266(params->tone_qmid_db);
    const float res_gain1 = db_to_linear(params->spk_res_gain1_db);
    const float res_gain2 = db_to_linear(params->spk_res_gain2_db);
    const float res_fres = iso266(params->spk_res_fres_dbhz);
    const float res_qts = iso266(params->spk_res_qts_db);
    const float res_q2 = res_qts * sqrtf(res_gain2);
    const float res_q1 = res_q2 * sqrtf(res_gain2 * res_gain1);
    const float ind_gain1 = db_to_linear(params->spk_ind_gain1_db);
    const float ind_gain2 = db_to_linear(params->spk_ind_gain2_db);
    const float ind_find = iso266(params->spk_ind_find_dbhz);
    float ind_f2 = ind_find * sqrtf(ind_gain2);
    if (ind_f2 >= 0.4f * (float)sr) {
        ind_f2 = 0.4f * (float)sr;
    }
    float ind_f1 = ind_f2 * sqrtf(ind_gain2 * ind_gain1);
    if (ind_f1 >= 0.4f * (float)sr) {
        ind_f1 = 0.4f * (float)sr;
    }

    const Svf2Coeffs tone = svf2_tst_coeffs(bass * bass, mid * mid, treble * treble,
                                            tone_fmid, tone_qmid, sr);
    const Svf2Coeffs peq1 = svf2_peq_coeffs(res_gain1, res_fres, res_q1, sr);
    const Svf2Coeffs peq3 = svf2_peq_coeffs(res_gain2, res_fres, res_q2, sr);
    const Svf1HsCoeffs hs1 = svf1_hs_coeffs(ind_gain1, ind_f1, sr);
    const Svf1HsCoeffs hs3 = svf1_hs_coeffs(ind_gain2, ind_f2, sr);

    if (snap) {
        smooth_snap(&c->gain_stage.g, gain);
        smooth_snap(&c->volume_stage.g, volume_gain);
        smooth_snap(&c->output_stage.g, db_to_linear(params->output_gain_db));
        svf2_sm_snap(&c->tone, tone);
        svf2_sm_snap(&c->peq1_t4, peq1);
        svf2_sm_snap(&c->peq1_t5, peq1);
        svf2_sm_snap(&c->peq3, peq3);
        svf1_hs_sm_snap(&c->hs1_t4, hs1);
        svf1_hs_sm_snap(&c->hs1_t5, hs1);
        svf1_hs_sm_snap(&c->hs3, hs3);
    } else {
        const float sk = c->smooth_k;
        smooth_set(&c->gain_stage.g, gain, sk);
        smooth_set(&c->volume_stage.g, volume_gain, sk);
        smooth_set(&c->output_stage.g, db_to_linear(params->output_gain_db), sk);
        svf2_sm_set(&c->tone, tone, sk);
        svf2_sm_set(&c->peq1_t4, peq1, sk);
        svf2_sm_set(&c->peq1_t5, peq1, sk);
        svf2_sm_set(&c->peq3, peq3, sk);
        svf1_hs_sm_set(&c->hs1_t4, hs1, sk);
        svf1_hs_sm_set(&c->hs1_t5, hs1, sk);
        svf1_hs_sm_set(&c->hs3, hs3, sk);
    }
}

static void nilamp_tweed_5e3_pp_seed_modes(NilampTweed5e3PpState *st,
                                           const NilampParams *params,
                                           const NilampTweed5e3PpData *model,
                                           bool force)
{
    const int tube1 = nilamp_enum_param(params->tube1, 2, 1);
    const int splitter = nilamp_enum_param(params->phase_splitter,
                                           (int)(sizeof(TWEED_5E3_PP_SPLITTER_MODES) /
                                                 sizeof(TWEED_5E3_PP_SPLITTER_MODES[0])),
                                           2);
    const SplitterModeCfg *splitter_cfg = nilamp_splitter_cfg(splitter);
    const StageCfg *t4_cfg = nilamp_splitter_t4_cfg(splitter_cfg, model);
    const StageCfg *t5_cfg = nilamp_splitter_t5_cfg(splitter_cfg, model);

    if (force || st->active_tube1 != tube1) {
        memset(&st->t1, 0, sizeof(st->t1));
        adnl_seed_at_zero(&st->t1.adnl, nilamp_tube1_cfg(model, tube1)->table);
        st->active_tube1 = tube1;
    }

    if (force || st->active_splitter != splitter) {
        memset(&st->t3, 0, sizeof(st->t3));
        memset(&st->t6, 0, sizeof(st->t6));
        memset(&st->t4, 0, sizeof(st->t4));
        memset(&st->t5, 0, sizeof(st->t5));
        memset(&st->hp2, 0, sizeof(st->hp2));
        memset(&st->hp3, 0, sizeof(st->hp3));
        memset(&st->hp4, 0, sizeof(st->hp4));
        memset(&st->peq1_t4, 0, sizeof(st->peq1_t4));
        memset(&st->peq1_t5, 0, sizeof(st->peq1_t5));
        memset(&st->hs1_t4, 0, sizeof(st->hs1_t4));
        memset(&st->hs1_t5, 0, sizeof(st->hs1_t5));
        adnl_seed_at_zero(&st->t3.adnl, model->t3->table);
        adnl_seed_at_zero(&st->t6.adnl1, T6_LTP.table1);
        adnl_seed_at_zero(&st->t6.adnl2, T6_LTP.table2);
        adnl_seed_at_zero(&st->t4.adnl, t4_cfg->table);
        adnl_seed_at_zero(&st->t5.adnl, t5_cfg->table);
        st->prev_dia1 = 0.0f;
        st->prev_dig = 0.0f;
        st->prev_dia3 = 0.0f;
        st->active_splitter = splitter;
    }
}

static void nilamp_tweed_5e3_pp_reset(void *state, void *coeffs,
                                      const NilampParams *params,
                                      const void *topology_data,
                                      double sr)
{
    NilampTweed5e3PpState *st = (NilampTweed5e3PpState *)state;
    NilampTweed5e3PpCoeffs *c = (NilampTweed5e3PpCoeffs *)coeffs;
    const NilampTweed5e3PpData *model = nilamp_tweed_5e3_pp_data(topology_data);
    if (st == NULL || c == NULL || params == NULL || model == NULL) {
        return;
    }
    memset(st, 0, sizeof(*st));
    memset(c, 0, sizeof(*c));
    nilamp_tweed_5e3_pp_apply_coeffs(c, params, model, sr, true);
    adnl_seed_at_zero(&st->t2.adnl, model->t2->table);
    nilamp_tweed_5e3_pp_seed_modes(st, params, model, true);
}

static void nilamp_tweed_5e3_pp_set_params(void *state, void *coeffs,
                                           const NilampParams *params,
                                           const void *topology_data,
                                           double sr,
                                           bool has_processed)
{
    NilampTweed5e3PpState *st = (NilampTweed5e3PpState *)state;
    NilampTweed5e3PpCoeffs *c = (NilampTweed5e3PpCoeffs *)coeffs;
    const NilampTweed5e3PpData *model = nilamp_tweed_5e3_pp_data(topology_data);
    if (st == NULL || c == NULL || params == NULL || model == NULL) {
        return;
    }
    const int tube1 = nilamp_enum_param(params->tube1, 2, 1);
    const int splitter = nilamp_enum_param(params->phase_splitter,
                                           (int)(sizeof(TWEED_5E3_PP_SPLITTER_MODES) /
                                                 sizeof(TWEED_5E3_PP_SPLITTER_MODES[0])),
                                           2);
    const bool topology_changed = tube1 != c->tube1_mode || splitter != c->splitter_mode;
    const bool snap = !has_processed || topology_changed;
    nilamp_tweed_5e3_pp_apply_coeffs(c, params, model, sr, snap);
    nilamp_tweed_5e3_pp_seed_modes(st, params, model, topology_changed);
}

static NilampTapFrame nilamp_tweed_5e3_pp_process(NilampTweed5e3PpState *st,
                                                  NilampTweed5e3PpCoeffs *c,
                                                  const NilampParams *params,
                                                  const NilampTweed5e3PpData *model,
                                                  float input)
{
    nilamp_tweed_5e3_pp_seed_modes(st, params, model, false);
    const StageCfg *t1_cfg = nilamp_tube1_cfg(model, c->tube1_mode);
    const SplitterModeCfg *splitter_cfg = nilamp_splitter_cfg(c->splitter_mode);
    const StageCfg *t4_cfg = nilamp_splitter_t4_cfg(splitter_cfg, model);
    const StageCfg *t5_cfg = nilamp_splitter_t5_cfg(splitter_cfg, model);

    st->t4.advk = 0.5f * (st->t4.advk + st->t5.advk);
    st->t5.advk = st->t4.advk;
    const float t4_advk_in = st->t4.advk;
    const float t5_advk_in = st->t5.advk;

    const float old_s2 = st->p2.s;
    const float old_s3 = st->p3.s;

    const float dvs1 =
        tube_pss_process(&st->p1, model->pss1_r, c->pss1_f, old_s2, st->prev_dia1, 0.0f);
    const float dvs2 =
        tube_pss_process(&st->p2, model->pss2_r, c->pss2_f, old_s3, st->prev_dig, dvs1);
    const float dvs3 = tube_pss_process(
        &st->p3, c->pss3_r, c->pss3_f, 0.0f, st->prev_dia3, dvs2);
    const float p2_s = st->p2.s;
    const float p3_s = st->p3.s;

    float res1_v, res1_dia;
    tube_ck_process(&st->t1, t1_cfg, c->pk_t1, c->adnl_eq, c->advk_t1,
                    gain_sm_process(&c->gain_stage, input), dvs3, &res1_v, &res1_dia);

    float v2 = ii1_hp_process(&st->hp1, c->hp1, res1_v);
    v2 = gain_sm_process(&c->volume_stage, v2);
    v2 = svf2_sm_process(&st->tone, &c->tone, v2);
    v2 = ii1_lp_process(&st->lp1, c->lp1, v2);

    float res3_v, res3_dia;
    tube_ck_process(&st->t2, model->t2, c->pk_t2, c->adnl_eq, c->advk_t2,
                    v2, dvs3, &res3_v, &res3_dia);
    const float res3_hp = ii1_hp_process(&st->hp2, c->hp2, res3_v);

    float res4_v, res4_aux, res4_dia;
    if (splitter_cfg->is_ltp) {
        tube_ltp_process(&st->t6, &T6_LTP, c->pk_ltp1, c->pk_ltp2, c->adnl_eq,
                         res3_hp * splitter_cfg->k4, 0.0f, 0.0f,
                         dvs3, &res4_v, &res4_aux, &res4_dia);
    } else {
        tube_cd_process(&st->t3, model->t3, c->pk_t3, c->adnl_eq,
                        res3_hp, dvs3, &res4_v, &res4_aux, &res4_dia);
    }

    float drive_t4 = res4_v * splitter_cfg->k1;
    drive_t4 = ii1_hp_process(&st->hp3, c->hp3, drive_t4);
    drive_t4 = svf2_sm_process(&st->peq1_t4, &c->peq1_t4, drive_t4);
    drive_t4 = svf1_hs_sm_process(&st->hs1_t4, &c->hs1_t4, drive_t4);

    float res5_v, res5_dia;
    tube_ck_process(&st->t4, t4_cfg, c->pk_t4, c->adnl_eq, c->advk_t4,
                    drive_t4, dvs2, &res5_v, &res5_dia);
    const float t4_advk_out = st->t4.advk;

    float aux = res4_aux * splitter_cfg->k2;
    aux = ii1_hp_process(&st->hp4, c->hp4, aux);
    aux = svf2_sm_process(&st->peq1_t5, &c->peq1_t5, aux);
    aux = svf1_hs_sm_process(&st->hs1_t5, &c->hs1_t5, aux);
    const float drive_t5 = aux;

    float res_t5_v, res_t5_dia;
    tube_ck_process(&st->t5, t5_cfg, c->pk_t5, c->adnl_eq, c->advk_t5,
                    aux, dvs2, &res_t5_v, &res_t5_dia);
    const float t5_advk_out = st->t5.advk;

    const float post_pp = res5_v - res_t5_v;
    const float post_peq3 = svf2_sm_process(&st->peq3, &c->peq3, post_pp);
    const float post_hs3 = svf1_hs_sm_process(&st->hs3, &c->hs3, post_peq3);
    const float post_hp5 = ii1_hp_process(&st->hp5, c->hp5, post_hs3);
    float v_out = post_hp5;
    v_out = df2_coeffs_process(&st->lp2, c->lp2, v_out);
    v_out *= c->post_scale;
    v_out = gain_sm_process(&c->output_stage, v_out);

    const float dia1_next = res5_dia + res_t5_dia;
    st->prev_dia1 = dia1_next;
    st->prev_dig = model->screen_current_feedback * st->prev_dia1;
    st->prev_dia3 = res1_dia + res3_dia + res4_dia;

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

static NilampTapFrame nilamp_tweed_5e3_pp_process_sample(void *state,
                                                         void *coeffs,
                                                         const NilampParams *params,
                                                         const void *topology_data,
                                                         float input)
{
    NilampTweed5e3PpState *st = (NilampTweed5e3PpState *)state;
    NilampTweed5e3PpCoeffs *c = (NilampTweed5e3PpCoeffs *)coeffs;
    const NilampTweed5e3PpData *model = nilamp_tweed_5e3_pp_data(topology_data);
    if (st == NULL || c == NULL || params == NULL || model == NULL) {
        return (NilampTapFrame) { 0 };
    }
    return nilamp_tweed_5e3_pp_process(st, c, params, model, input);
}

const NilampTopologyOps NILAMP_TWEED_5E3_PP_OPS = {
    .topology = NILAMP_TOPOLOGY_TWEED_5E3_CATHODYNE_PP,
    .state_size = sizeof(NilampTweed5e3PpState),
    .coeffs_size = sizeof(NilampTweed5e3PpCoeffs),
    .accepts_model_data = nilamp_tweed_5e3_pp_accepts_model_data,
    .reset = nilamp_tweed_5e3_pp_reset,
    .set_params = nilamp_tweed_5e3_pp_set_params,
    .process_sample = nilamp_tweed_5e3_pp_process_sample,
};

#ifdef NILAMP_ENABLE_TEST_API
static void test_tube_ck(const StageCfg *cfg, double sample_rate, const float *input, float *v_out, float *dia, size_t n)
{
    TubeCk st = { 0 };
    adnl_seed_at_zero(&st.adnl, cfg->table);
    const PkdCoeffs pk = pkd_coeffs(cfg->pk_attack, cfg->pk_release, sample_rate);
    const Df2Coeffs adnl_eq = adnl_eq_make_coeffs(sample_rate);
    const Ii1Coeffs advk = tube_advk_coeffs(cfg, sample_rate);
    for (size_t i = 0; i < n; i++) {
        tube_ck_process(&st, cfg, pk, adnl_eq, advk, input[i], 0.0f, &v_out[i], &dia[i]);
    }
}

void nilamp_test_tube_ck_t2(double sample_rate, const float *input, float *v_out, float *dia, size_t n)
{
    test_tube_ck(nilamp_default_tweed_5e3_pp_data()->t2, sample_rate, input, v_out, dia, n);
}

void nilamp_test_tube_ck_t5(double sample_rate, const float *input, float *v_out, float *dia, size_t n)
{
    test_tube_ck(nilamp_default_tweed_5e3_pp_data()->t5, sample_rate, input, v_out, dia, n);
}

void nilamp_test_tube_ck_t2_dz(double sample_rate, const float *input, float *v_out, float *dia, size_t n)
{
    test_tube_ck(&T2_DZ, sample_rate, input, v_out, dia, n);
}

static void test_tube_cd(const StageCfg *cfg, double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n)
{
    TubeCd st = { 0 };
    adnl_seed_at_zero(&st.adnl, cfg->table);
    const PkdCoeffs pk = pkd_coeffs(cfg->pk_attack, cfg->pk_release, sample_rate);
    const Df2Coeffs adnl_eq = adnl_eq_make_coeffs(sample_rate);
    for (size_t i = 0; i < n; i++) {
        tube_cd_process(&st, cfg, pk, adnl_eq, input[i], 0.0f, &v_out[i], &vk_out[i], &dia[i]);
    }
}

void nilamp_test_tube_cd_t3(double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n)
{
    test_tube_cd(nilamp_default_tweed_5e3_pp_data()->t3, sample_rate, input, v_out, vk_out, dia, n);
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
    const NilampTweed5e3PpData *model = nilamp_default_tweed_5e3_pp_data();
    const StageCfg *t4_cfg = model->t4;
    const StageCfg *t5_cfg = model->t5;
    adnl_seed_at_zero(&t4.adnl, t4_cfg->table);
    adnl_seed_at_zero(&t5.adnl, t5_cfg->table);
    const Ii1Coeffs hp3_coeffs = ii1_lp_coeffs(5.8f, sample_rate);
    const Ii1Coeffs hp4_coeffs = ii1_lp_coeffs(6.4f, sample_rate);
    const Svf2Coeffs peq_coeffs =
        svf2_peq_coeffs(1.1220184543f, 80.0f, 2.6685237666f, sample_rate);
    const Svf1HsCoeffs hs_coeffs =
        svf1_hs_coeffs(1.4125375446f, 2098.1359672f, sample_rate);
    const Df2Coeffs adnl_eq = adnl_eq_make_coeffs(sample_rate);
    const PkdCoeffs t4_pk = pkd_coeffs(t4_cfg->pk_attack, t4_cfg->pk_release, sample_rate);
    const PkdCoeffs t5_pk = pkd_coeffs(t5_cfg->pk_attack, t5_cfg->pk_release, sample_rate);
    const Ii1Coeffs t4_advk = tube_advk_coeffs(t4_cfg, sample_rate);
    const Ii1Coeffs t5_advk = tube_advk_coeffs(t5_cfg, sample_rate);

    for (size_t i = 0; i < n; i++) {
        float t4_in = ii1_hp_process(&hp3, hp3_coeffs, t3_v[i] * 0.797f);
        t4_in = svf2_coeffs_process(&peq_t4, peq_coeffs, t4_in);
        t4_in = svf1_hs_coeffs_process(&hs_t4, hs_coeffs, t4_in);

        float t5_in = ii1_hp_process(&hp4, hp4_coeffs, t3_vk[i] * 0.940f);
        t5_in = svf2_coeffs_process(&peq_t5, peq_coeffs, t5_in);
        t5_in = svf1_hs_coeffs_process(&hs_t5, hs_coeffs, t5_in);

        float t4_dia;
        float t5_dia;
        tube_ck_process(&t4, t4_cfg, t4_pk, adnl_eq, t4_advk,
                        t4_in, 0.0f, &outputs[0][i], &t4_dia);
        tube_ck_process(&t5, t5_cfg, t5_pk, adnl_eq, t5_advk,
                        t5_in, 0.0f, &outputs[1][i], &t5_dia);
        outputs[2][i] = outputs[0][i] - outputs[1][i];
        outputs[3][i] = t4_dia + t5_dia;
    }
}

void nilamp_test_pss(float r, float tau, double sample_rate, const float *dia, float *dvs, float *s, size_t n)
{
    TubePss st = { 0 };
    const Ii1Coeffs coeffs = tube_pss_coeffs(tau, sample_rate);
    for (size_t i = 0; i < n; i++) {
        dvs[i] = tube_pss_process(&st, r, coeffs, 0.0f, dia[i], 0.0f);
        s[i] = st.s;
    }
}
#endif
