CC ?= cc
CXX ?= c++
OBJC ?= $(CC)
OBJCXX ?= $(CXX)
CMAKE ?= $(or $(shell command -v cmake 2>/dev/null),/opt/homebrew/bin/cmake)
CODESIGN ?= codesign
PYTHON_BOOTSTRAP ?= $(or $(wildcard /opt/homebrew/bin/python3.13),$(shell command -v python3 2>/dev/null),python3)
PYTHON ?= $(or $(wildcard .venv/bin/python3),$(shell command -v python3 2>/dev/null),/opt/homebrew/bin/python3.13)
UNAME_S := $(shell uname -s)

NATIVE_DIR := native
NATIVE_BUILD := $(NATIVE_DIR)/build
NATIVE_BIN := $(NATIVE_DIR)/bin
NATIVE_GENERATED := $(NATIVE_DIR)/generated
CLAP_INCLUDE := third_party/clap/include
VST3SDK_DIR := third_party/vst3sdk
PUGL_INCLUDE := third_party/pugl/include
PUGL_SRC := third_party/pugl/src
SOKOL_INCLUDE := third_party/sokol
NUKLEAR_INCLUDE := third_party/nuklear
YSFX_ROOT ?= $(HOME)/src/ysfx
YSFX_BUILD := $(NATIVE_BUILD)/ysfx
YSFX_LIB := $(YSFX_BUILD)/libysfx.a
CLAP_INSTALL_DIR_DEFAULT := $(HOME)/.clap
DIST_DIR ?= dist
RELEASE_VERSION ?= 1.0.3
RELEASE_GPG_KEY ?= C3504EE1EE38410CE1C433BC372B8AAACB867F13
AMP_MODELS := models/amps/keller_twd_dlx_ii.kdl
CLAP_NAME_C := $(strip $(shell $(PYTHON) tools/gen_amp_models.py --print-clap-name-c $(AMP_MODELS)))
CLAP_BUNDLE := $(strip $(shell $(PYTHON) tools/gen_amp_models.py --print-clap-filename $(AMP_MODELS)))
VST3_NAME_C := $(strip $(shell $(PYTHON) tools/gen_amp_models.py --print-vst3-name-c $(AMP_MODELS)))
VST3_BUNDLE := $(strip $(shell $(PYTHON) tools/gen_amp_models.py --print-vst3-filename $(AMP_MODELS)))
VST3_EXECUTABLE := $(strip $(shell $(PYTHON) tools/gen_amp_models.py --print-vst3-executable $(AMP_MODELS)))
VST3_BUNDLE_ID := $(strip $(shell $(PYTHON) tools/gen_amp_models.py --print-vst3-bundle-id $(AMP_MODELS)))
ifeq ($(CLAP_NAME_C),)
$(error failed to read CLAP descriptor name from $(AMP_MODELS))
endif
ifeq ($(CLAP_BUNDLE),)
$(error failed to read CLAP bundle filename from $(AMP_MODELS))
endif
ifeq ($(VST3_NAME_C),)
$(error failed to read VST3 descriptor name from $(AMP_MODELS))
endif
ifeq ($(VST3_BUNDLE),)
$(error failed to read VST3 bundle filename from $(AMP_MODELS))
endif
ifeq ($(VST3_EXECUTABLE),)
$(error failed to read VST3 executable filename from $(AMP_MODELS))
endif
ifeq ($(VST3_BUNDLE_ID),)
$(error failed to read VST3 bundle identifier from $(AMP_MODELS))
endif
CLAP_PLUGIN := $(NATIVE_BIN)/$(CLAP_BUNDLE)
VST3_PLUGIN := $(NATIVE_BIN)/$(VST3_BUNDLE)
VST3_BINARY := $(VST3_PLUGIN)/Contents/MacOS/$(VST3_EXECUTABLE)
VST3_INFO_PLIST := $(VST3_PLUGIN)/Contents/Info.plist
VST3_LINUX_ARCH := $(shell uname -m)-linux

CFLAGS ?= -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -I$(NATIVE_DIR)/src -I$(NATIVE_GENERATED)
CLAP_CFLAGS := $(CFLAGS) -I$(CLAP_INCLUDE)
VST3_CXXFLAGS := -std=c++17 -O3 -Wall -Wextra -Wpedantic -Werror -fPIC -DNDEBUG -I$(NATIVE_DIR)/src -I$(NATIVE_GENERATED) -I$(VST3SDK_DIR)
VST3_VENDOR_CXXFLAGS := -std=c++17 -O3 -w -fPIC -DNDEBUG -I$(VST3SDK_DIR)
GUI_CFLAGS := $(CLAP_CFLAGS) -I$(PUGL_INCLUDE) -I$(PUGL_SRC) -I$(SOKOL_INCLUDE) -I$(SOKOL_INCLUDE)/util -I$(NUKLEAR_INCLUDE)
GUI_VENDOR_CFLAGS := -std=c11 -O3 -w -fPIC -D_POSIX_C_SOURCE=200809L -I$(PUGL_INCLUDE) -I$(PUGL_SRC) -I$(SOKOL_INCLUDE) -I$(SOKOL_INCLUDE)/util -I$(NUKLEAR_INCLUDE)
GUI_VENDOR_OBJCFLAGS := -O3 -w -fPIC -I$(PUGL_INCLUDE) -I$(PUGL_SRC) -I$(SOKOL_INCLUDE) -I$(SOKOL_INCLUDE)/util -I$(NUKLEAR_INCLUDE)
YSFX_CFLAGS := $(CFLAGS) -isystem $(YSFX_ROOT)/include
LDFLAGS ?=
LDLIBS ?= -lm
DL_LDLIBS :=
PLUGIN_LDFLAGS := -shared
GUI_LDLIBS :=
CLAP_INSTALL_CODESIGN :=
VST3_INSTALL_DIR_DEFAULT :=
VST3_INSTALL_CODESIGN :=
NILAMP_ENABLE_CLAP_GUI ?= $(if $(filter Linux Darwin,$(UNAME_S)),1,0)

ifeq ($(UNAME_S),Linux)
DL_LDLIBS := -ldl
GUI_LDLIBS := -lX11 -lXrandr -lXcursor -lXext -lGL -lEGL -lwayland-client -lwayland-egl -ldl
VST3_BINARY := $(VST3_PLUGIN)/Contents/$(VST3_LINUX_ARCH)/$(VST3_EXECUTABLE).so
VST3_INSTALL_DIR_DEFAULT := $(HOME)/.vst3
endif

ifeq ($(UNAME_S),Darwin)
CLAP_INSTALL_DIR_DEFAULT := $(HOME)/Library/Audio/Plug-Ins/CLAP
VST3_INSTALL_DIR_DEFAULT := $(HOME)/Library/Audio/Plug-Ins/VST3
CLAP_EXECUTABLE := $(basename $(CLAP_BUNDLE))
CLAP_BUNDLE_ID := dev.niltempus.$(CLAP_EXECUTABLE).clap
CLAP_BINARY := $(CLAP_PLUGIN)/Contents/MacOS/$(CLAP_EXECUTABLE)
CLAP_INFO_PLIST := $(CLAP_PLUGIN)/Contents/Info.plist
PLUGIN_LDFLAGS := -dynamiclib -Wl,-install_name,@rpath/$(CLAP_EXECUTABLE)
GUI_LDLIBS := -framework Cocoa -framework CoreVideo -framework OpenGL
CLAP_INSTALL_CODESIGN := $(CODESIGN) --force --sign -
VST3_INSTALL_CODESIGN := $(CODESIGN) --force --sign -
endif

CLAP_INSTALL_DIR ?= $(CLAP_INSTALL_DIR_DEFAULT)
VST3_INSTALL_DIR ?= $(VST3_INSTALL_DIR_DEFAULT)

NILAMP_RELEASE_DEFINE := '-DNILAMP_RELEASE_VERSION="$(RELEASE_VERSION)"'
NILAMP_NATIVE_CFLAGS := $(CFLAGS) $(NILAMP_RELEASE_DEFINE)
CLAP_PLUGIN_CFLAGS := $(CLAP_CFLAGS) -DNILAMP_ENABLE_CLAP_GUI=$(NILAMP_ENABLE_CLAP_GUI) '-DNILAMP_CLAP_NAME=$(CLAP_NAME_C)' $(NILAMP_RELEASE_DEFINE)
VST3_PLUGIN_CXXFLAGS := $(VST3_CXXFLAGS) '-DNILAMP_VST3_NAME=$(VST3_NAME_C)' $(NILAMP_RELEASE_DEFINE)
TEST_VST3_CXXFLAGS := $(VST3_CXXFLAGS) $(NILAMP_RELEASE_DEFINE)
TEST_CLAP_CFLAGS := $(CLAP_CFLAGS) -DNILAMP_EXPECT_CLAP_GUI=$(NILAMP_ENABLE_CLAP_GUI) '-DNILAMP_EXPECT_CLAP_NAME=$(CLAP_NAME_C)' $(NILAMP_RELEASE_DEFINE)
ifneq ($(NILAMP_ENABLE_CLAP_GUI),0)
CLAP_PLUGIN_CFLAGS += -I$(PUGL_INCLUDE) -I$(PUGL_SRC) -I$(SOKOL_INCLUDE) -I$(SOKOL_INCLUDE)/util -I$(NUKLEAR_INCLUDE)
endif
YSFX_LDLIBS := $(DL_LDLIBS) -pthread -lm

YSFX_AVAILABLE := $(if $(and $(wildcard $(YSFX_ROOT)/include/ysfx.h),$(wildcard $(YSFX_ROOT)/thirdparty/dr_libs/dr_wav.h)),1,0)

NATIVE_TABLES_C := $(NATIVE_GENERATED)/nilamp_tables.c
NATIVE_TABLES_H := $(NATIVE_GENERATED)/nilamp_tables.h
NATIVE_MODELS_INC := $(NATIVE_GENERATED)/nilamp_models.inc
NATIVE_FONT_TTF := third_party/fonts/0xproto/0xProto-Regular.ttf
NATIVE_FONT_C := $(NATIVE_GENERATED)/nilamp_font_0xproto.c
NATIVE_FONT_H := $(NATIVE_GENERATED)/nilamp_font_0xproto.h

NATIVE_DSP_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.o \
	$(NATIVE_BUILD)/nilamp_topologies.o \
	$(NATIVE_BUILD)/nilamp_topology_tweed_5e3_pp.o \
	$(NATIVE_BUILD)/nilamp_tables.o

NATIVE_OBJS := \
	$(NATIVE_DSP_OBJS) \
	$(NATIVE_BUILD)/nilamp_host.o \
	$(NATIVE_BUILD)/nilamp_process_log.o

NATIVE_DSP_PIC_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.pic.o \
	$(NATIVE_BUILD)/nilamp_topologies.pic.o \
	$(NATIVE_BUILD)/nilamp_topology_tweed_5e3_pp.pic.o \
	$(NATIVE_BUILD)/nilamp_tables.pic.o

NATIVE_PIC_OBJS := \
	$(NATIVE_DSP_PIC_OBJS) \
	$(NATIVE_BUILD)/nilamp_host.pic.o \
	$(NATIVE_BUILD)/nilamp_process_log.pic.o

NATIVE_DSP_TEST_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp_test.o \
	$(NATIVE_BUILD)/nilamp_topologies_test.o \
	$(NATIVE_BUILD)/nilamp_topology_tweed_5e3_pp_test.o \
	$(NATIVE_BUILD)/nilamp_tables.o

VST3_SDK_OBJS := \
	$(NATIVE_BUILD)/vst3_baseiids.o \
	$(NATIVE_BUILD)/vst3_fbuffer.o \
	$(NATIVE_BUILD)/vst3_fdebug.o \
	$(NATIVE_BUILD)/vst3_fobject.o \
	$(NATIVE_BUILD)/vst3_fstreamer.o \
	$(NATIVE_BUILD)/vst3_fstring.o \
	$(NATIVE_BUILD)/vst3_timer.o \
	$(NATIVE_BUILD)/vst3_updatehandler.o \
	$(NATIVE_BUILD)/vst3_flock.o \
	$(NATIVE_BUILD)/vst3_fcondition.o \
	$(NATIVE_BUILD)/vst3_conststringtable.o \
	$(NATIVE_BUILD)/vst3_coreiids.o \
	$(NATIVE_BUILD)/vst3_funknown.o \
	$(NATIVE_BUILD)/vst3_ustring.o \
	$(NATIVE_BUILD)/vst3_commoniids.o \
	$(NATIVE_BUILD)/vst3_pluginview.o \
	$(NATIVE_BUILD)/vst3_moduleinit.o \
	$(NATIVE_BUILD)/vst3_pluginfactory.o \
	$(NATIVE_BUILD)/vst3_vstaudioeffect.o \
	$(NATIVE_BUILD)/vst3_vstbus.o \
	$(NATIVE_BUILD)/vst3_vstcomponent.o \
	$(NATIVE_BUILD)/vst3_vstcomponentbase.o \
	$(NATIVE_BUILD)/vst3_vsteditcontroller.o \
	$(NATIVE_BUILD)/vst3_vstinitiids.o \
	$(NATIVE_BUILD)/vst3_vstparameters.o \
	$(NATIVE_BUILD)/vst3_vstnoteexpressiontypes.o

NATIVE_GUI_OBJS := \
	$(NATIVE_BUILD)/nilamp_gui_input.pic.o \
	$(NATIVE_BUILD)/nilamp_gui_edit.pic.o \
	$(NATIVE_BUILD)/nilamp_gui.pic.o \
	$(NATIVE_BUILD)/nilamp_font_0xproto.pic.o \
	$(NATIVE_BUILD)/nilamp_sokol.pic.o \
	$(NATIVE_BUILD)/nilamp_sokol_nuklear.pic.o \
	$(NATIVE_BUILD)/nilamp_nuklear.pic.o \
	$(NATIVE_BUILD)/pugl_common.pic.o \
	$(NATIVE_BUILD)/pugl_internal.pic.o

ifeq ($(UNAME_S),Linux)
NATIVE_GUI_OBJS += \
	$(NATIVE_BUILD)/pugl_x11.pic.o \
	$(NATIVE_BUILD)/pugl_x11_gl.pic.o
VST3_SDK_OBJS += $(NATIVE_BUILD)/vst3_linuxmain.o
NATIVE_TARGETS_VST3 := $(VST3_PLUGIN)
NATIVE_TEST_TARGETS_VST3 := $(NATIVE_BIN)/test_vst3_load
TEST_VST3_CXXFLAGS += '-DNILAMP_TEST_VST3_LINUX_BINARY="$(VST3_LINUX_ARCH)/$(VST3_EXECUTABLE).so"'
endif

ifeq ($(UNAME_S),Darwin)
NATIVE_GUI_OBJS += \
	$(NATIVE_BUILD)/pugl_mac.pic.o \
	$(NATIVE_BUILD)/pugl_mac_gl.pic.o
VST3_SDK_OBJS += $(NATIVE_BUILD)/vst3_macmain.o
NATIVE_TARGETS_VST3 := $(VST3_PLUGIN)
NATIVE_TEST_TARGETS_VST3 := $(NATIVE_BIN)/test_vst3_load
endif

ifeq ($(NILAMP_ENABLE_CLAP_GUI),0)
NATIVE_GUI_OBJS :=
endif

NATIVE_TARGETS := \
	$(NATIVE_BIN)/nilamp_render \
	$(NATIVE_BIN)/nilamp_taps_render \
	$(NATIVE_BIN)/test_native \
	$(NATIVE_BIN)/test_gui_edit \
	$(NATIVE_BIN)/test_clap_load \
	$(NATIVE_TEST_TARGETS_VST3) \
	$(CLAP_PLUGIN) \
	$(NATIVE_TARGETS_VST3)

ifeq ($(YSFX_AVAILABLE),1)
NATIVE_TARGETS += $(NATIVE_BIN)/ysfx_render
endif

.PHONY: all native native-test native-bench native-perf-bench native-host-test native-reaper-host-test native-jsfx-test native-jsfx-matrix-test native-loaded-clap-diagnose install-clap-user install-vst3-user package-linux-release package-macos-release setup-python clean-native FORCE

all: native

native: $(NATIVE_TARGETS)

install-clap-user: $(CLAP_PLUGIN)
	mkdir -p $(CLAP_INSTALL_DIR)
	rm -rf $(CLAP_INSTALL_DIR)/$(CLAP_BUNDLE)
	cp -R $< $(CLAP_INSTALL_DIR)/$(CLAP_BUNDLE)
	$(if $(CLAP_INSTALL_CODESIGN),$(CLAP_INSTALL_CODESIGN) $(CLAP_INSTALL_DIR)/$(CLAP_BUNDLE))

package-linux-release: $(CLAP_PLUGIN) $(VST3_PLUGIN)
	tools/package_linux_release.sh \
	    --version $(RELEASE_VERSION) \
	    --plugin $(CLAP_PLUGIN) \
	    --clap-bundle $(CLAP_BUNDLE) \
	    --vst3-plugin $(VST3_PLUGIN) \
	    --vst3-bundle $(VST3_BUNDLE) \
	    --dist-dir $(DIST_DIR) \
	    --gpg-key $(RELEASE_GPG_KEY) \
	    --existing-sums $(DIST_DIR)/SHA256SUMS

install-vst3-user: $(VST3_PLUGIN)
	mkdir -p $(VST3_INSTALL_DIR)
	rm -rf $(VST3_INSTALL_DIR)/$(VST3_BUNDLE)
	cp -R $< $(VST3_INSTALL_DIR)/$(VST3_BUNDLE)
	$(if $(VST3_INSTALL_CODESIGN),$(VST3_INSTALL_CODESIGN) $(VST3_INSTALL_DIR)/$(VST3_BUNDLE))

package-macos-release: $(CLAP_PLUGIN) $(VST3_PLUGIN)
	tools/package_macos_release.sh \
	    --version $(RELEASE_VERSION) \
	    --plugin $(CLAP_PLUGIN) \
	    --clap-bundle $(CLAP_BUNDLE) \
	    --vst3-plugin $(VST3_PLUGIN) \
	    --vst3-bundle $(VST3_BUNDLE) \
	    --dist-dir $(DIST_DIR) \
	    --gpg-key $(RELEASE_GPG_KEY)

native-test: $(NATIVE_BIN)/test_native $(NATIVE_BIN)/test_gui_edit $(NATIVE_BIN)/test_clap_load $(NATIVE_TEST_TARGETS_VST3) $(CLAP_PLUGIN) $(NATIVE_TARGETS_VST3)
	$(NATIVE_BIN)/test_native
	$(NATIVE_BIN)/test_gui_edit
	$(NATIVE_BIN)/test_clap_load $(CLAP_PLUGIN)
	$(if $(NATIVE_TEST_TARGETS_VST3),$(NATIVE_BIN)/test_vst3_load $(VST3_PLUGIN),true)

native-bench: $(NATIVE_BIN)/bench_native
	$(NATIVE_BIN)/bench_native

PERF_BENCH_ARGS ?=
native-perf-bench: $(NATIVE_BIN)/bench_ysfx_perf $(NATIVE_BIN)/bench_clap_perf $(NATIVE_BIN)/bench_vst3_perf $(CLAP_PLUGIN) $(VST3_PLUGIN)
	$(PYTHON) tools/benchmark_keller_perf.py $(PERF_BENCH_ARGS)

native-host-test: native-test
	$(PYTHON) tools/clap_validate/validate_clap.py --plugin $(CLAP_PLUGIN)
	$(if $(NATIVE_TARGETS_VST3),$(PYTHON) tools/vst3_validate/validate_vst3.py --plugin $(VST3_PLUGIN),true)

native-reaper-host-test: $(CLAP_PLUGIN)
	$(PYTHON) tools/clap_validate/validate_reaper_clap.py --plugin $<

native-jsfx-test: $(NATIVE_BIN)/nilamp_render $(NATIVE_BIN)/nilamp_taps_render $(NATIVE_BIN)/ysfx_render
	$(PYTHON) -m tools.jsfx_render.stage_jsfx
	$(PYTHON) tools/abx_compare.py --preset sine --rms-threshold-db -16
	$(PYTHON) tools/compare_taps.py --preset sine
	$(PYTHON) tools/low_input_regression.py --require-jsfx

native-jsfx-matrix-test: $(NATIVE_BIN)/nilamp_render $(NATIVE_BIN)/ysfx_render
	$(PYTHON) -m tools.jsfx_render.stage_jsfx
	$(PYTHON) tools/parity_matrix.py

native-low-input-test: $(NATIVE_BIN)/nilamp_render
	$(PYTHON) tools/low_input_regression.py

setup-python:
	$(PYTHON_BOOTSTRAP) -m venv .venv
	./.venv/bin/python3 -m pip install --upgrade pip
	./.venv/bin/python3 -m pip install -r requirements-dev.txt

$(NATIVE_BUILD) $(NATIVE_BIN) $(NATIVE_GENERATED):
	mkdir -p $@

$(NATIVE_MODELS_INC): tools/gen_amp_models.py $(AMP_MODELS) | $(NATIVE_GENERATED)
	$(PYTHON) tools/gen_amp_models.py $@ $(AMP_MODELS)

$(NATIVE_FONT_C): tools/gen_font_asset.py $(NATIVE_FONT_TTF) | $(NATIVE_GENERATED)
	$(PYTHON) tools/gen_font_asset.py \
	    --symbol nilamp_font_0xproto_regular \
	    --header $(NATIVE_FONT_H) \
	    --source $(NATIVE_FONT_C) \
	    $(NATIVE_FONT_TTF)

$(NATIVE_FONT_H): $(NATIVE_FONT_C)

$(NATIVE_BUILD)/nilamp_tables.o: $(NATIVE_TABLES_C) $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $(NATIVE_TABLES_C) -o $@

$(NATIVE_BUILD)/nilamp_dsp.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_TABLES_H) $(NATIVE_MODELS_INC) | $(NATIVE_BUILD)
	$(CC) $(NILAMP_NATIVE_CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_topologies.o: $(NATIVE_DIR)/src/nilamp_topologies.c $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_dsp.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_topology_tweed_5e3_pp.o: $(NATIVE_DIR)/src/nilamp_topology_tweed_5e3_pp.c $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_host.o: $(NATIVE_DIR)/src/nilamp_host.c $(NATIVE_DIR)/src/nilamp_host.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_process_log.o: $(NATIVE_DIR)/src/nilamp_process_log.c $(NATIVE_DIR)/src/nilamp_process_log.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_tables.pic.o: $(NATIVE_TABLES_C) $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $(NATIVE_TABLES_C) -o $@

$(NATIVE_BUILD)/nilamp_dsp.pic.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_TABLES_H) $(NATIVE_MODELS_INC) | $(NATIVE_BUILD)
	$(CC) $(NILAMP_NATIVE_CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_topologies.pic.o: $(NATIVE_DIR)/src/nilamp_topologies.c $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_dsp.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_topology_tweed_5e3_pp.pic.o: $(NATIVE_DIR)/src/nilamp_topology_tweed_5e3_pp.c $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_host.pic.o: $(NATIVE_DIR)/src/nilamp_host.c $(NATIVE_DIR)/src/nilamp_host.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_process_log.pic.o: $(NATIVE_DIR)/src/nilamp_process_log.c $(NATIVE_DIR)/src/nilamp_process_log.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_dsp_test.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_TABLES_H) $(NATIVE_MODELS_INC) | $(NATIVE_BUILD)
	$(CC) $(NILAMP_NATIVE_CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BUILD)/nilamp_topologies_test.o: $(NATIVE_DIR)/src/nilamp_topologies.c $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_dsp.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BUILD)/nilamp_topology_tweed_5e3_pp_test.o: $(NATIVE_DIR)/src/nilamp_topology_tweed_5e3_pp.c $(NATIVE_DIR)/src/nilamp_topologies.h $(NATIVE_DIR)/src/nilamp_dsp_internal.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BUILD)/nilamp_render.o: $(NATIVE_DIR)/src/nilamp_render.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BIN)/nilamp_render: $(NATIVE_BUILD)/nilamp_render.o $(NATIVE_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/nilamp_taps_render.o: $(NATIVE_DIR)/src/nilamp_render.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_TAPS_RENDER -c $< -o $@

$(NATIVE_BIN)/nilamp_taps_render: $(NATIVE_BUILD)/nilamp_taps_render.o $(NATIVE_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/nilamp_clap.o: $(NATIVE_DIR)/src/nilamp_clap.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h $(NATIVE_DIR)/src/nilamp_gui.h $(NATIVE_DIR)/src/nilamp_gui_input.h $(CLAP_INCLUDE)/clap/clap.h | $(NATIVE_BUILD)
	$(CC) $(CLAP_PLUGIN_CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_gui_edit.o: $(NATIVE_DIR)/src/nilamp_gui_edit.c $(NATIVE_DIR)/src/nilamp_gui_edit.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_gui_edit.pic.o: $(NATIVE_DIR)/src/nilamp_gui_edit.c $(NATIVE_DIR)/src/nilamp_gui_edit.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_gui_input.o: $(NATIVE_DIR)/src/nilamp_gui_input.c $(NATIVE_DIR)/src/nilamp_gui_input.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_gui_input.pic.o: $(NATIVE_DIR)/src/nilamp_gui_input.c $(NATIVE_DIR)/src/nilamp_gui_input.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_gui.pic.o: $(NATIVE_DIR)/src/nilamp_gui.c $(NATIVE_DIR)/src/nilamp_gui.h $(NATIVE_DIR)/src/nilamp_gui_edit.h $(NATIVE_DIR)/src/nilamp_gui_input.h $(NATIVE_FONT_H) | $(NATIVE_BUILD)
	$(CC) $(GUI_CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_font_0xproto.pic.o: $(NATIVE_FONT_C) $(NATIVE_FONT_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $(NATIVE_FONT_C) -o $@

$(NATIVE_BUILD)/nilamp_sokol.pic.o: $(NATIVE_DIR)/src/nilamp_sokol.c | $(NATIVE_BUILD)
	$(CC) $(GUI_VENDOR_CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_sokol_nuklear.pic.o: $(NATIVE_DIR)/src/nilamp_sokol_nuklear.c | $(NATIVE_BUILD)
	$(CC) $(GUI_VENDOR_CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_nuklear.pic.o: $(NATIVE_DIR)/src/nilamp_nuklear.c | $(NATIVE_BUILD)
	$(CC) $(GUI_VENDOR_CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/pugl_common.pic.o: $(PUGL_SRC)/common.c | $(NATIVE_BUILD)
	$(CC) $(GUI_VENDOR_CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/pugl_internal.pic.o: $(PUGL_SRC)/internal.c | $(NATIVE_BUILD)
	$(CC) $(GUI_VENDOR_CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/pugl_x11.pic.o: $(PUGL_SRC)/x11.c | $(NATIVE_BUILD)
	$(CC) $(GUI_VENDOR_CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/pugl_x11_gl.pic.o: $(PUGL_SRC)/x11_gl.c | $(NATIVE_BUILD)
	$(CC) $(GUI_VENDOR_CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/pugl_mac.pic.o: $(PUGL_SRC)/mac.m $(PUGL_SRC)/mac.h | $(NATIVE_BUILD)
	$(OBJC) $(GUI_VENDOR_OBJCFLAGS) -c $< -o $@

$(NATIVE_BUILD)/pugl_mac_gl.pic.o: $(PUGL_SRC)/mac_gl.m $(PUGL_SRC)/mac.h | $(NATIVE_BUILD)
	$(OBJC) $(GUI_VENDOR_OBJCFLAGS) -c $< -o $@

ifeq ($(UNAME_S),Darwin)
$(CLAP_BINARY): $(NATIVE_BUILD)/nilamp_clap.o $(NATIVE_PIC_OBJS) $(NATIVE_GUI_OBJS) Makefile | $(NATIVE_BIN)
	if [ -f $(CLAP_PLUGIN) ]; then rm -f $(CLAP_PLUGIN); fi
	mkdir -p $(CLAP_PLUGIN)/Contents/MacOS
	$(CC) $(LDFLAGS) $(PLUGIN_LDFLAGS) $(filter-out Makefile,$^) $(LDLIBS) $(GUI_LDLIBS) -o $@

$(CLAP_INFO_PLIST): Makefile | $(NATIVE_BIN)
	if [ -f $(CLAP_PLUGIN) ]; then rm -f $(CLAP_PLUGIN); fi
	mkdir -p $(CLAP_PLUGIN)/Contents
	printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' '<plist version="1.0">' '<dict>' '  <key>CFBundleExecutable</key>' '  <string>$(CLAP_EXECUTABLE)</string>' '  <key>CFBundleIdentifier</key>' '  <string>$(CLAP_BUNDLE_ID)</string>' '  <key>CFBundleName</key>' '  <string>$(CLAP_EXECUTABLE)</string>' '  <key>CFBundlePackageType</key>' '  <string>BNDL</string>' '  <key>CFBundleShortVersionString</key>' '  <string>$(RELEASE_VERSION)</string>' '  <key>CFBundleVersion</key>' '  <string>$(RELEASE_VERSION)</string>' '</dict>' '</plist>' > $@

$(CLAP_PLUGIN): $(CLAP_BINARY) $(CLAP_INFO_PLIST)
else
$(CLAP_PLUGIN): $(NATIVE_BUILD)/nilamp_clap.o $(NATIVE_PIC_OBJS) $(NATIVE_GUI_OBJS) Makefile | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $(PLUGIN_LDFLAGS) $(filter-out Makefile,$^) $(LDLIBS) $(GUI_LDLIBS) -o $@
endif

ifeq ($(UNAME_S),Linux)
$(NATIVE_BUILD)/nilamp_vst3.o: $(NATIVE_DIR)/src/nilamp_vst3.mm $(NATIVE_DIR)/src/nilamp_host.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_gui.h $(NATIVE_DIR)/src/nilamp_gui_input.h $(NATIVE_DIR)/src/nilamp_vst3_keys.h $(VST3SDK_DIR)/pluginterfaces/gui/iwaylandframe.h | $(NATIVE_BUILD)
	$(CXX) $(VST3_PLUGIN_CXXFLAGS) -x c++ -c $< -o $@
else
$(NATIVE_BUILD)/nilamp_vst3.o: $(NATIVE_DIR)/src/nilamp_vst3.mm $(NATIVE_DIR)/src/nilamp_host.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_gui.h $(NATIVE_DIR)/src/nilamp_gui_input.h $(NATIVE_DIR)/src/nilamp_vst3_keys.h | $(NATIVE_BUILD)
	$(OBJCXX) $(VST3_PLUGIN_CXXFLAGS) -c $< -o $@
endif

$(VST3_BINARY): $(NATIVE_BUILD)/nilamp_vst3.o $(VST3_SDK_OBJS) $(NATIVE_PIC_OBJS) $(NATIVE_GUI_OBJS) Makefile | $(NATIVE_BIN)
ifeq ($(UNAME_S),Linux)
	mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) -shared $(filter-out Makefile,$^) $(LDLIBS) $(GUI_LDLIBS) -o $@
else
	mkdir -p $(VST3_PLUGIN)/Contents/MacOS
	$(OBJCXX) $(LDFLAGS) -dynamiclib -Wl,-install_name,@rpath/$(VST3_EXECUTABLE) $(filter-out Makefile,$^) $(LDLIBS) $(GUI_LDLIBS) -framework CoreFoundation -o $@
endif

$(VST3_INFO_PLIST): Makefile | $(NATIVE_BIN)
	mkdir -p $(VST3_PLUGIN)/Contents
	printf '%s\n' '<?xml version="1.0" encoding="UTF-8"?>' '<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">' '<plist version="1.0">' '<dict>' '  <key>CFBundleExecutable</key>' '  <string>$(VST3_EXECUTABLE)</string>' '  <key>CFBundleIdentifier</key>' '  <string>$(VST3_BUNDLE_ID)</string>' '  <key>CFBundleName</key>' '  <string>$(VST3_EXECUTABLE)</string>' '  <key>CFBundlePackageType</key>' '  <string>BNDL</string>' '  <key>CFBundleShortVersionString</key>' '  <string>$(RELEASE_VERSION)</string>' '  <key>CFBundleVersion</key>' '  <string>$(RELEASE_VERSION)</string>' '</dict>' '</plist>' > $@

ifeq ($(UNAME_S),Linux)
$(VST3_PLUGIN): $(VST3_BINARY)
else
$(VST3_PLUGIN): $(VST3_BINARY) $(VST3_INFO_PLIST)
endif

$(NATIVE_BUILD)/vst3_baseiids.o: $(VST3SDK_DIR)/base/source/baseiids.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_fbuffer.o: $(VST3SDK_DIR)/base/source/fbuffer.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_fdebug.o: $(VST3SDK_DIR)/base/source/fdebug.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_fobject.o: $(VST3SDK_DIR)/base/source/fobject.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_fstreamer.o: $(VST3SDK_DIR)/base/source/fstreamer.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_fstring.o: $(VST3SDK_DIR)/base/source/fstring.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_timer.o: $(VST3SDK_DIR)/base/source/timer.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_updatehandler.o: $(VST3SDK_DIR)/base/source/updatehandler.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_flock.o: $(VST3SDK_DIR)/base/thread/source/flock.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_fcondition.o: $(VST3SDK_DIR)/base/thread/source/fcondition.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_conststringtable.o: $(VST3SDK_DIR)/pluginterfaces/base/conststringtable.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_coreiids.o: $(VST3SDK_DIR)/pluginterfaces/base/coreiids.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_funknown.o: $(VST3SDK_DIR)/pluginterfaces/base/funknown.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_ustring.o: $(VST3SDK_DIR)/pluginterfaces/base/ustring.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_commoniids.o: $(VST3SDK_DIR)/public.sdk/source/common/commoniids.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_pluginview.o: $(VST3SDK_DIR)/public.sdk/source/common/pluginview.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_macmain.o: $(VST3SDK_DIR)/public.sdk/source/main/macmain.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_linuxmain.o: $(VST3SDK_DIR)/public.sdk/source/main/linuxmain.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_moduleinit.o: $(VST3SDK_DIR)/public.sdk/source/main/moduleinit.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_pluginfactory.o: $(VST3SDK_DIR)/public.sdk/source/main/pluginfactory.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_vstaudioeffect.o: $(VST3SDK_DIR)/public.sdk/source/vst/vstaudioeffect.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_vstbus.o: $(VST3SDK_DIR)/public.sdk/source/vst/vstbus.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_vstcomponent.o: $(VST3SDK_DIR)/public.sdk/source/vst/vstcomponent.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_vstcomponentbase.o: $(VST3SDK_DIR)/public.sdk/source/vst/vstcomponentbase.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_vsteditcontroller.o: $(VST3SDK_DIR)/public.sdk/source/vst/vsteditcontroller.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_vstinitiids.o: $(VST3SDK_DIR)/public.sdk/source/vst/vstinitiids.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_vstparameters.o: $(VST3SDK_DIR)/public.sdk/source/vst/vstparameters.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/vst3_vstnoteexpressiontypes.o: $(VST3SDK_DIR)/public.sdk/source/vst/vstnoteexpressiontypes.cpp | $(NATIVE_BUILD)
	$(CXX) $(VST3_VENDOR_CXXFLAGS) -c $< -o $@

$(NATIVE_BUILD)/test_native.o: $(NATIVE_DIR)/tests/test_native.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_gui.h | $(NATIVE_BUILD)
	$(CC) $(NILAMP_NATIVE_CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BIN)/test_native: $(NATIVE_BUILD)/test_native.o $(NATIVE_DSP_TEST_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/test_gui_edit.o: $(NATIVE_DIR)/tests/test_gui_edit.c $(NATIVE_DIR)/src/nilamp_gui_edit.h $(NATIVE_DIR)/src/nilamp_gui_input.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BIN)/test_gui_edit: $(NATIVE_BUILD)/test_gui_edit.o $(NATIVE_BUILD)/nilamp_gui_edit.o $(NATIVE_BUILD)/nilamp_gui_input.o | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/bench_native.o: $(NATIVE_DIR)/tests/bench_native.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BIN)/bench_native: $(NATIVE_BUILD)/bench_native.o $(NATIVE_DSP_TEST_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/bench_ysfx_perf.o: $(NATIVE_DIR)/tests/bench_ysfx_perf.c $(NATIVE_DIR)/tests/bench_perf_common.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h $(YSFX_ROOT)/include/ysfx.h | $(NATIVE_BUILD)
	$(CC) $(YSFX_CFLAGS) -c $< -o $@

$(NATIVE_BIN)/bench_ysfx_perf: $(NATIVE_BUILD)/bench_ysfx_perf.o $(NATIVE_DSP_OBJS) $(YSFX_LIB) | $(NATIVE_BIN)
	$(CXX) $(LDFLAGS) $^ $(YSFX_LDLIBS) -o $@

$(NATIVE_BUILD)/bench_clap_perf.o: $(NATIVE_DIR)/tests/bench_clap_perf.c $(NATIVE_DIR)/tests/bench_perf_common.h $(NATIVE_DIR)/src/nilamp_dsp.h $(CLAP_INCLUDE)/clap/clap.h | $(NATIVE_BUILD)
	$(CC) $(CLAP_CFLAGS) -c $< -o $@

$(NATIVE_BIN)/bench_clap_perf: $(NATIVE_BUILD)/bench_clap_perf.o $(NATIVE_DSP_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) $(DL_LDLIBS) -o $@

$(NATIVE_BUILD)/bench_vst3_perf.o: $(NATIVE_DIR)/tests/bench_vst3_perf.mm $(NATIVE_DIR)/tests/bench_perf_common.h $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_host.h | $(NATIVE_BUILD)
ifeq ($(UNAME_S),Linux)
	$(CXX) $(TEST_VST3_CXXFLAGS) -x c++ -c $< -o $@
else
	$(OBJCXX) $(TEST_VST3_CXXFLAGS) -c $< -o $@
endif

$(NATIVE_BIN)/bench_vst3_perf: $(NATIVE_BUILD)/bench_vst3_perf.o $(NATIVE_OBJS) $(NATIVE_BUILD)/vst3_baseiids.o $(NATIVE_BUILD)/vst3_coreiids.o $(NATIVE_BUILD)/vst3_funknown.o $(NATIVE_BUILD)/vst3_vstinitiids.o | $(NATIVE_BIN)
ifeq ($(UNAME_S),Linux)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) $(DL_LDLIBS) -o $@
else
	$(OBJCXX) $(LDFLAGS) $^ $(LDLIBS) -framework CoreFoundation -o $@
endif

$(NATIVE_BUILD)/test_clap_load.o: $(NATIVE_DIR)/tests/test_clap_load.c $(NATIVE_DIR)/src/nilamp_dsp.h $(CLAP_INCLUDE)/clap/clap.h | $(NATIVE_BUILD)
	$(CC) $(TEST_CLAP_CFLAGS) -c $< -o $@

$(NATIVE_BIN)/test_clap_load: $(NATIVE_BUILD)/test_clap_load.o $(NATIVE_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) $(DL_LDLIBS) -o $@

$(NATIVE_BUILD)/test_vst3_load.o: $(NATIVE_DIR)/tests/test_vst3_load.mm $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_gui.h $(NATIVE_DIR)/src/nilamp_host.h $(NATIVE_DIR)/src/nilamp_vst3_keys.h | $(NATIVE_BUILD)
ifeq ($(UNAME_S),Linux)
	$(CXX) $(TEST_VST3_CXXFLAGS) -x c++ -c $< -o $@
else
	$(OBJCXX) $(TEST_VST3_CXXFLAGS) -c $< -o $@
endif

$(NATIVE_BIN)/test_vst3_load: $(NATIVE_BUILD)/test_vst3_load.o $(NATIVE_OBJS) $(NATIVE_BUILD)/vst3_baseiids.o $(NATIVE_BUILD)/vst3_coreiids.o $(NATIVE_BUILD)/vst3_funknown.o $(NATIVE_BUILD)/vst3_commoniids.o $(NATIVE_BUILD)/vst3_vstinitiids.o | $(NATIVE_BIN)
ifeq ($(UNAME_S),Linux)
	$(CXX) $(LDFLAGS) $^ $(LDLIBS) $(DL_LDLIBS) -o $@
else
	$(OBJCXX) $(LDFLAGS) $^ $(LDLIBS) -framework CoreFoundation -o $@
endif

$(NATIVE_BUILD)/render_loaded_clap.o: $(NATIVE_DIR)/src/render_loaded_clap.c $(NATIVE_DIR)/src/nilamp_dsp.h $(CLAP_INCLUDE)/clap/clap.h | $(NATIVE_BUILD)
	$(CC) $(CLAP_CFLAGS) -c $< -o $@

$(NATIVE_BIN)/render_loaded_clap: $(NATIVE_BUILD)/render_loaded_clap.o | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) $(DL_LDLIBS) -o $@

native-loaded-clap-diagnose: $(NATIVE_BIN)/render_loaded_clap $(NATIVE_BIN)/nilamp_render $(CLAP_PLUGIN)
	$(PYTHON) tools/clap_validate/render_loaded_clap.py \
	    --plugin $(CLAP_PLUGIN) \
	    --render $(NATIVE_BIN)/nilamp_render \
	    --driver $(NATIVE_BIN)/render_loaded_clap

ifeq ($(YSFX_AVAILABLE),0)
$(YSFX_BUILD)/CMakeCache.txt:
	@echo "ysfx checkout not found at $(YSFX_ROOT)" >&2
	@echo "Set YSFX_ROOT to a checkout with include/ysfx.h and thirdparty/dr_libs/dr_wav.h" >&2
	@false
else
$(YSFX_BUILD)/CMakeCache.txt: | $(NATIVE_BUILD)
	test -f $(YSFX_ROOT)/include/ysfx.h
	test -f $(YSFX_ROOT)/thirdparty/dr_libs/dr_wav.h
	$(CMAKE) -S $(YSFX_ROOT) -B $(YSFX_BUILD) -DCMAKE_BUILD_TYPE=Release -DYSFX_GFX=OFF -DYSFX_PLUGIN=OFF -DYSFX_TESTS=OFF -DYSFX_TOOLS=OFF
endif

$(YSFX_LIB): $(YSFX_BUILD)/CMakeCache.txt FORCE
	$(CMAKE) --build $(YSFX_BUILD) --target ysfx

$(NATIVE_BUILD)/ysfx_render.o: $(NATIVE_DIR)/src/ysfx_render.c $(YSFX_ROOT)/include/ysfx.h | $(NATIVE_BUILD)
	$(CC) $(YSFX_CFLAGS) -c $< -o $@

$(NATIVE_BIN)/ysfx_render: $(NATIVE_BUILD)/ysfx_render.o $(YSFX_LIB) | $(NATIVE_BIN)
	$(CXX) $(LDFLAGS) $^ $(YSFX_LDLIBS) -o $@

clean-native:
	rm -rf $(NATIVE_BUILD) $(NATIVE_BIN)

FORCE:
