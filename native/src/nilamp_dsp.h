// SPDX-License-Identifier: MIT
#ifndef NILAMP_DSP_H
#define NILAMP_DSP_H

#include <stddef.h>
#include <stdint.h>

typedef struct NilampEngine NilampEngine;

typedef enum NilampModelId {
    NILAMP_MODEL_KELLER_TWD_DLX_II = 0,
    NILAMP_MODEL_DEFAULT = NILAMP_MODEL_KELLER_TWD_DLX_II,
} NilampModelId;

typedef enum NilampParamId {
    NILAMP_PARAM_GAIN_DB = 0,
    NILAMP_PARAM_VOLUME_PCT = 1,
    NILAMP_PARAM_BASS_PCT = 2,
    NILAMP_PARAM_MID_PCT = 3,
    NILAMP_PARAM_TREBLE_PCT = 4,
    NILAMP_PARAM_SAG_PCT = 5,
    NILAMP_PARAM_OUTPUT_GAIN_DB = 6,
    NILAMP_PARAM_TONE_FMID_DBHZ = 7,
    NILAMP_PARAM_TONE_QMID_DB = 8,
    NILAMP_PARAM_SPK_RES_GAIN1_DB = 9,
    NILAMP_PARAM_SPK_RES_GAIN2_DB = 10,
    NILAMP_PARAM_SPK_RES_FRES_DBHZ = 11,
    NILAMP_PARAM_SPK_RES_QTS_DB = 12,
    NILAMP_PARAM_SPK_IND_GAIN1_DB = 13,
    NILAMP_PARAM_SPK_IND_GAIN2_DB = 14,
    NILAMP_PARAM_SPK_IND_FIND_DBHZ = 15,
    NILAMP_PARAM_GAIN_COMP = 16,
    NILAMP_PARAM_TUBE1 = 17,
    NILAMP_PARAM_PHASE_SPLITTER = 18,
    NILAMP_PARAM_COUNT = 19,
} NilampParamId;

typedef enum NilampControlDisplay {
    NILAMP_CONTROL_DISPLAY_LINEAR = 0,
    NILAMP_CONTROL_DISPLAY_ISO266 = 1,
    NILAMP_CONTROL_DISPLAY_ENUM = 2,
} NilampControlDisplay;

typedef struct NilampControlSpec {
    uint32_t id;
    const char *name;
    const char *module;
    const char *unit;
    float min_value;
    float max_value;
    float default_value;
    float step;
    NilampControlDisplay display;
    const char *const *enum_names;
    uint32_t enum_count;
} NilampControlSpec;

typedef enum NilampGuiScreenId {
    NILAMP_GUI_SCREEN_ID_MAIN = 0,
    NILAMP_GUI_SCREEN_ID_OPTIONS = 1,
    NILAMP_GUI_SCREEN_ID_ABOUT = 2,
} NilampGuiScreenId;

typedef enum NilampGuiWidgetType {
    NILAMP_GUI_WIDGET_TEXT = 0,
    NILAMP_GUI_WIDGET_BUTTON = 1,
    NILAMP_GUI_WIDGET_PANEL = 2,
    NILAMP_GUI_WIDGET_KNOB = 3,
    NILAMP_GUI_WIDGET_ENUM = 4,
} NilampGuiWidgetType;

typedef enum NilampGuiTextStyle {
    NILAMP_GUI_TEXT_NORMAL = 0,
    NILAMP_GUI_TEXT_TITLE = 1,
    NILAMP_GUI_TEXT_SUBTITLE = 2,
    NILAMP_GUI_TEXT_ABOUT = 3,
} NilampGuiTextStyle;

typedef enum NilampGuiKnobDisplay {
    NILAMP_GUI_KNOB_DISPLAY_PERCENT = 0,
    NILAMP_GUI_KNOB_DISPLAY_GAIN_UNIPOLAR = 1,
    NILAMP_GUI_KNOB_DISPLAY_GAIN_BIPOLAR = 2,
} NilampGuiKnobDisplay;

typedef struct NilampGuiRectSpec {
    float x;
    float y;
    float w;
    float h;
} NilampGuiRectSpec;

typedef struct NilampGuiThemeSpec {
    uint32_t background;
    uint32_t header;
    uint32_t header_rule;
    uint32_t panel;
    uint32_t border;
    uint32_t text;
} NilampGuiThemeSpec;

typedef struct NilampGuiWidgetSpec {
    NilampGuiWidgetType type;
    NilampGuiScreenId screen;
    const char *label;
    uint32_t param_id;
    NilampGuiRectSpec bounds;
    NilampGuiTextStyle text_style;
    NilampGuiKnobDisplay knob_display;
    float radius;
    NilampGuiScreenId target_screen;
} NilampGuiWidgetSpec;

typedef struct NilampGuiScreenSpec {
    NilampGuiScreenId id;
    const char *title;
    const NilampGuiWidgetSpec *widgets;
    uint32_t widget_count;
} NilampGuiScreenSpec;

typedef struct NilampGuiLayoutSpec {
    uint32_t design_width;
    uint32_t design_height;
    NilampGuiScreenId default_screen;
    NilampGuiThemeSpec theme;
    const NilampGuiScreenSpec *screens;
    uint32_t screen_count;
} NilampGuiLayoutSpec;

typedef struct NilampParams {
    float gain_db;
    float volume_pct;
    float bass_pct;
    float mid_pct;
    float treble_pct;
    float sag_pct;
    float output_gain_db;
    float tone_fmid_dbhz;
    float tone_qmid_db;
    float spk_res_gain1_db;
    float spk_res_gain2_db;
    float spk_res_fres_dbhz;
    float spk_res_qts_db;
    float spk_ind_gain1_db;
    float spk_ind_gain2_db;
    float spk_ind_find_dbhz;
    float gain_comp;
    float tube1;
    float phase_splitter;
} NilampParams;

enum {
    NILAMP_NUM_TAPS = 23,
    NILAMP_TEST_NUM_BACKEND_FILTERS = 7,
    NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS = 4,
};

typedef enum NilampTestAdnlTable {
    NILAMP_TEST_ADNL_T1_12AX7,
    NILAMP_TEST_ADNL_T2_12AX7,
    NILAMP_TEST_ADNL_T3_CD,
    NILAMP_TEST_ADNL_T4_6V6,
    NILAMP_TEST_ADNL_T5_6V6,
} NilampTestAdnlTable;

NilampEngine *nilamp_engine_create(double sample_rate);
NilampEngine *nilamp_engine_create_model(double sample_rate, NilampModelId model_id);
void nilamp_engine_destroy(NilampEngine *engine);
void nilamp_engine_reset(NilampEngine *engine);
void nilamp_engine_set_params(NilampEngine *engine, const NilampParams *params);
void nilamp_engine_process(NilampEngine *engine, const float *input, float *output, uint32_t nframes);
void nilamp_engine_process_taps(NilampEngine *engine, const float *input, float *outputs[NILAMP_NUM_TAPS], uint32_t nframes);

NilampParams nilamp_default_params(void);
NilampModelId nilamp_engine_model_id(const NilampEngine *engine);
const char *nilamp_model_name(NilampModelId model_id);
const NilampControlSpec *nilamp_control_specs(uint32_t *count);
const NilampControlSpec *nilamp_control_spec(uint32_t id);
const NilampGuiLayoutSpec *nilamp_model_gui_layout(NilampModelId model_id);
float nilamp_control_display_value(const NilampControlSpec *spec, float raw_value);

#ifdef NILAMP_ENABLE_TEST_API
void nilamp_test_flt_ii1_lp(float f, double sample_rate, const float *input, float *output, size_t n);
void nilamp_test_flt_ii1_hp(float f, double sample_rate, const float *input, float *output, size_t n);
void nilamp_test_flt_sv2_tst(double sample_rate, const float *input, float *output, size_t n);
void nilamp_test_pkd(float xth, float xdiode, float k1, float k2, const float *input, float *output, size_t n);
void nilamp_test_adnl(NilampTestAdnlTable table, const float *input, float *output, size_t n);
void nilamp_test_filter_backend(double sample_rate, const float *input, float *outputs[NILAMP_TEST_NUM_BACKEND_FILTERS], size_t n);
void nilamp_test_tube_ck_t2(double sample_rate, const float *input, float *v_out, float *dia, size_t n);
void nilamp_test_tube_ck_t5(double sample_rate, const float *input, float *v_out, float *dia, size_t n);
void nilamp_test_tube_ck_t2_dz(double sample_rate, const float *input, float *v_out, float *dia, size_t n);
void nilamp_test_tube_cd_t3(double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n);
void nilamp_test_tube_cd_t3_dz(double sample_rate, const float *input, float *v_out, float *vk_out, float *dia, size_t n);
void nilamp_test_power_pair(double sample_rate, const float *t3_v, const float *t3_vk, float *outputs[NILAMP_TEST_NUM_POWER_PAIR_OUTPUTS], size_t n);
void nilamp_test_pss(float r, float tau, double sample_rate, const float *dia, float *dvs, float *s, size_t n);
#endif

#endif
