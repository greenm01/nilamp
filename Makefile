CC ?= cc
CXX ?= c++
PYTHON ?= python3

NATIVE_DIR := native
NATIVE_BUILD := $(NATIVE_DIR)/build
NATIVE_BIN := $(NATIVE_DIR)/bin
NATIVE_GENERATED := $(NATIVE_DIR)/generated
CLAP_INCLUDE := third_party/clap/include
YSFX_ROOT ?= /home/niltempus/src/ysfx
YSFX_BUILD := $(NATIVE_BUILD)/ysfx
YSFX_LIB := $(YSFX_BUILD)/libysfx.a

CFLAGS ?= -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -I$(NATIVE_DIR)/src -I$(NATIVE_GENERATED)
CLAP_CFLAGS := $(CFLAGS) -I$(CLAP_INCLUDE)
YSFX_CFLAGS := $(CFLAGS) -I$(YSFX_ROOT)/include
LDFLAGS ?=
LDLIBS ?= -lm
YSFX_LDLIBS := -ldl -pthread -lm

NATIVE_TABLES_C := $(NATIVE_GENERATED)/nilamp_tables.c
NATIVE_TABLES_H := $(NATIVE_GENERATED)/nilamp_tables.h
NATIVE_MODELS_INC := $(NATIVE_GENERATED)/nilamp_models.inc
AMP_MODELS := models/amps/keller_twd_dlx_ii.kdl

NATIVE_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.o \
	$(NATIVE_BUILD)/nilamp_tables.o

NATIVE_PIC_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.pic.o \
	$(NATIVE_BUILD)/nilamp_tables.pic.o

.PHONY: all native native-test native-bench native-host-test native-reaper-host-test native-jsfx-test clean-native FORCE

all: native

native: $(NATIVE_BIN)/nilamp_render $(NATIVE_BIN)/nilamp_taps_render $(NATIVE_BIN)/test_native $(NATIVE_BIN)/nilamp.clap $(NATIVE_BIN)/ysfx_render

native-test: $(NATIVE_BIN)/test_native $(NATIVE_BIN)/test_clap_load $(NATIVE_BIN)/nilamp.clap
	$(NATIVE_BIN)/test_native
	$(NATIVE_BIN)/test_clap_load $(NATIVE_BIN)/nilamp.clap

native-bench: $(NATIVE_BIN)/bench_native
	$(NATIVE_BIN)/bench_native

native-host-test: native-test
	python3 tools/clap_validate/validate_clap.py --plugin $(NATIVE_BIN)/nilamp.clap

native-reaper-host-test: $(NATIVE_BIN)/nilamp.clap
	python3 tools/clap_validate/validate_reaper_clap.py --plugin $<

native-jsfx-test: $(NATIVE_BIN)/nilamp_render $(NATIVE_BIN)/nilamp_taps_render $(NATIVE_BIN)/ysfx_render
	python3 -m tools.jsfx_render.stage_jsfx
	python3 tools/abx_compare.py --preset sine --rms-threshold-db -16
	python3 tools/compare_taps.py --preset sine

$(NATIVE_BUILD) $(NATIVE_BIN) $(NATIVE_GENERATED):
	mkdir -p $@

$(NATIVE_MODELS_INC): tools/gen_amp_models.py $(AMP_MODELS) | $(NATIVE_GENERATED)
	$(PYTHON) tools/gen_amp_models.py $@ $(AMP_MODELS)

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

$(NATIVE_BUILD)/nilamp_clap.o: $(NATIVE_DIR)/src/nilamp_clap.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h $(CLAP_INCLUDE)/clap/clap.h | $(NATIVE_BUILD)
	$(CC) $(CLAP_CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BIN)/nilamp.clap: $(NATIVE_BUILD)/nilamp_clap.o $(NATIVE_PIC_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) -shared $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/test_native.o: $(NATIVE_DIR)/tests/test_native.c $(NATIVE_DIR)/src/nilamp_dsp.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BIN)/test_native: $(NATIVE_BUILD)/test_native.o $(NATIVE_BUILD)/nilamp_dsp_test.o $(NATIVE_BUILD)/nilamp_tables.o | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/bench_native.o: $(NATIVE_DIR)/tests/bench_native.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_DIR)/src/nilamp_cpu.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BIN)/bench_native: $(NATIVE_BUILD)/bench_native.o $(NATIVE_BUILD)/nilamp_dsp_test.o $(NATIVE_BUILD)/nilamp_tables.o | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/test_clap_load.o: $(NATIVE_DIR)/tests/test_clap_load.c $(CLAP_INCLUDE)/clap/clap.h | $(NATIVE_BUILD)
	$(CC) $(CLAP_CFLAGS) -c $< -o $@

$(NATIVE_BIN)/test_clap_load: $(NATIVE_BUILD)/test_clap_load.o | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ -ldl -o $@

$(YSFX_BUILD)/CMakeCache.txt: | $(NATIVE_BUILD)
	test -f $(YSFX_ROOT)/include/ysfx.h
	test -f $(YSFX_ROOT)/thirdparty/dr_libs/dr_wav.h
	cmake -S $(YSFX_ROOT) -B $(YSFX_BUILD) -DCMAKE_BUILD_TYPE=Release -DYSFX_GFX=OFF -DYSFX_PLUGIN=OFF -DYSFX_TESTS=OFF -DYSFX_TOOLS=OFF

$(YSFX_LIB): $(YSFX_BUILD)/CMakeCache.txt FORCE
	cmake --build $(YSFX_BUILD) --target ysfx

$(NATIVE_BUILD)/ysfx_render.o: $(NATIVE_DIR)/src/ysfx_render.c $(YSFX_ROOT)/include/ysfx.h | $(NATIVE_BUILD)
	$(CC) $(YSFX_CFLAGS) -c $< -o $@

$(NATIVE_BIN)/ysfx_render: $(NATIVE_BUILD)/ysfx_render.o $(YSFX_LIB) | $(NATIVE_BIN)
	$(CXX) $(LDFLAGS) $^ $(YSFX_LDLIBS) -o $@

clean-native:
	rm -rf $(NATIVE_BUILD) $(NATIVE_BIN)

FORCE:
