CC ?= cc
CXX ?= c++
OBJC ?= $(CC)
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
PUGL_INCLUDE := third_party/pugl/include
PUGL_SRC := third_party/pugl/src
SOKOL_INCLUDE := third_party/sokol
NUKLEAR_INCLUDE := third_party/nuklear
YSFX_ROOT ?= $(HOME)/src/ysfx
YSFX_BUILD := $(NATIVE_BUILD)/ysfx
YSFX_LIB := $(YSFX_BUILD)/libysfx.a
CLAP_INSTALL_DIR_DEFAULT := $(HOME)/.clap
AMP_MODELS := models/amps/keller_twd_dlx_ii.kdl
CLAP_NAME_C := $(strip $(shell $(PYTHON) tools/gen_amp_models.py --print-clap-name-c $(AMP_MODELS)))
CLAP_BUNDLE := $(strip $(shell $(PYTHON) tools/gen_amp_models.py --print-clap-filename $(AMP_MODELS)))
ifeq ($(CLAP_NAME_C),)
$(error failed to read CLAP descriptor name from $(AMP_MODELS))
endif
ifeq ($(CLAP_BUNDLE),)
$(error failed to read CLAP bundle filename from $(AMP_MODELS))
endif
CLAP_PLUGIN := $(NATIVE_BIN)/$(CLAP_BUNDLE)

CFLAGS ?= -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -I$(NATIVE_DIR)/src -I$(NATIVE_GENERATED)
CLAP_CFLAGS := $(CFLAGS) -I$(CLAP_INCLUDE)
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
NILAMP_ENABLE_CLAP_GUI ?= $(if $(filter Linux Darwin,$(UNAME_S)),1,0)

ifeq ($(UNAME_S),Linux)
DL_LDLIBS := -ldl
GUI_LDLIBS := -lX11 -lXrandr -lXcursor -lXext -lGL -ldl
endif

ifeq ($(UNAME_S),Darwin)
CLAP_INSTALL_DIR_DEFAULT := $(HOME)/Library/Audio/Plug-Ins/CLAP
PLUGIN_LDFLAGS := -dynamiclib -Wl,-install_name,@rpath/$(CLAP_BUNDLE)
GUI_LDLIBS := -framework Cocoa -framework CoreVideo -framework OpenGL
CLAP_INSTALL_CODESIGN := $(CODESIGN) --force --sign -
endif

CLAP_INSTALL_DIR ?= $(CLAP_INSTALL_DIR_DEFAULT)

CLAP_PLUGIN_CFLAGS := $(CLAP_CFLAGS) -DNILAMP_ENABLE_CLAP_GUI=$(NILAMP_ENABLE_CLAP_GUI) '-DNILAMP_CLAP_NAME=$(CLAP_NAME_C)'
TEST_CLAP_CFLAGS := $(CLAP_CFLAGS) -DNILAMP_EXPECT_CLAP_GUI=$(NILAMP_ENABLE_CLAP_GUI) '-DNILAMP_EXPECT_CLAP_NAME=$(CLAP_NAME_C)'
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

NATIVE_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.o \
	$(NATIVE_BUILD)/nilamp_tables.o

NATIVE_PIC_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.pic.o \
	$(NATIVE_BUILD)/nilamp_tables.pic.o

NATIVE_GUI_OBJS := \
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
endif

ifeq ($(UNAME_S),Darwin)
NATIVE_GUI_OBJS += \
	$(NATIVE_BUILD)/pugl_mac.pic.o \
	$(NATIVE_BUILD)/pugl_mac_gl.pic.o
endif

ifeq ($(NILAMP_ENABLE_CLAP_GUI),0)
NATIVE_GUI_OBJS :=
endif

NATIVE_TARGETS := \
	$(NATIVE_BIN)/nilamp_render \
	$(NATIVE_BIN)/nilamp_taps_render \
	$(NATIVE_BIN)/test_native \
	$(NATIVE_BIN)/test_clap_load \
	$(CLAP_PLUGIN)

ifeq ($(YSFX_AVAILABLE),1)
NATIVE_TARGETS += $(NATIVE_BIN)/ysfx_render
endif

.PHONY: all native native-test native-bench native-host-test native-reaper-host-test native-jsfx-test native-loaded-clap-diagnose install-clap-user setup-python clean-native FORCE

all: native

native: $(NATIVE_TARGETS)

install-clap-user: $(CLAP_PLUGIN)
	mkdir -p $(CLAP_INSTALL_DIR)
	cp -f $< $(CLAP_INSTALL_DIR)/$(CLAP_BUNDLE)
	$(if $(CLAP_INSTALL_CODESIGN),$(CLAP_INSTALL_CODESIGN) $(CLAP_INSTALL_DIR)/$(CLAP_BUNDLE))

native-test: $(NATIVE_BIN)/test_native $(NATIVE_BIN)/test_clap_load $(CLAP_PLUGIN)
	$(NATIVE_BIN)/test_native
	$(NATIVE_BIN)/test_clap_load $(CLAP_PLUGIN)

native-bench: $(NATIVE_BIN)/bench_native
	$(NATIVE_BIN)/bench_native

native-host-test: native-test
	$(PYTHON) tools/clap_validate/validate_clap.py --plugin $(CLAP_PLUGIN)

native-reaper-host-test: $(CLAP_PLUGIN)
	$(PYTHON) tools/clap_validate/validate_reaper_clap.py --plugin $<

native-jsfx-test: $(NATIVE_BIN)/nilamp_render $(NATIVE_BIN)/nilamp_taps_render $(NATIVE_BIN)/ysfx_render
	$(PYTHON) -m tools.jsfx_render.stage_jsfx
	$(PYTHON) tools/abx_compare.py --preset sine --rms-threshold-db -16
	$(PYTHON) tools/compare_taps.py --preset sine
	$(PYTHON) tools/low_input_regression.py --require-jsfx

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

$(NATIVE_BUILD)/nilamp_dsp.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) $(NATIVE_MODELS_INC) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_tables.pic.o: $(NATIVE_TABLES_C) $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $(NATIVE_TABLES_C) -o $@

$(NATIVE_BUILD)/nilamp_dsp.pic.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) $(NATIVE_MODELS_INC) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_dsp_test.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) $(NATIVE_MODELS_INC) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BUILD)/nilamp_render.o: $(NATIVE_DIR)/src/nilamp_render.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BIN)/nilamp_render: $(NATIVE_BUILD)/nilamp_render.o $(NATIVE_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/nilamp_taps_render.o: $(NATIVE_DIR)/src/nilamp_render.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_TAPS_RENDER -c $< -o $@

$(NATIVE_BIN)/nilamp_taps_render: $(NATIVE_BUILD)/nilamp_taps_render.o $(NATIVE_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/nilamp_clap.o: $(NATIVE_DIR)/src/nilamp_clap.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h $(NATIVE_DIR)/src/nilamp_gui.h $(CLAP_INCLUDE)/clap/clap.h | $(NATIVE_BUILD)
	$(CC) $(CLAP_PLUGIN_CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_gui.pic.o: $(NATIVE_DIR)/src/nilamp_gui.c $(NATIVE_DIR)/src/nilamp_gui.h $(NATIVE_FONT_H) | $(NATIVE_BUILD)
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

$(CLAP_PLUGIN): $(NATIVE_BUILD)/nilamp_clap.o $(NATIVE_PIC_OBJS) $(NATIVE_GUI_OBJS) Makefile | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $(PLUGIN_LDFLAGS) $(filter-out Makefile,$^) $(LDLIBS) $(GUI_LDLIBS) -o $@

$(NATIVE_BUILD)/test_native.o: $(NATIVE_DIR)/tests/test_native.c $(NATIVE_DIR)/src/nilamp_dsp.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BIN)/test_native: $(NATIVE_BUILD)/test_native.o $(NATIVE_BUILD)/nilamp_dsp_test.o $(NATIVE_BUILD)/nilamp_tables.o | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/bench_native.o: $(NATIVE_DIR)/tests/bench_native.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BIN)/bench_native: $(NATIVE_BUILD)/bench_native.o $(NATIVE_BUILD)/nilamp_dsp_test.o $(NATIVE_BUILD)/nilamp_tables.o | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/test_clap_load.o: $(NATIVE_DIR)/tests/test_clap_load.c $(NATIVE_DIR)/src/nilamp_dsp.h $(CLAP_INCLUDE)/clap/clap.h | $(NATIVE_BUILD)
	$(CC) $(TEST_CLAP_CFLAGS) -c $< -o $@

$(NATIVE_BIN)/test_clap_load: $(NATIVE_BUILD)/test_clap_load.o $(NATIVE_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) $(DL_LDLIBS) -o $@

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
