CC ?= cc

NATIVE_DIR := native
NATIVE_BUILD := $(NATIVE_DIR)/build
NATIVE_BIN := $(NATIVE_DIR)/bin
NATIVE_GENERATED := $(NATIVE_DIR)/generated
CLAP_INCLUDE := third_party/clap/include

CFLAGS ?= -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -I$(NATIVE_DIR)/src -I$(NATIVE_GENERATED)
CLAP_CFLAGS := $(CFLAGS) -I$(CLAP_INCLUDE)
LDFLAGS ?=
LDLIBS ?= -lm

NATIVE_TABLES_C := $(NATIVE_GENERATED)/nilamp_tables.c
NATIVE_TABLES_H := $(NATIVE_GENERATED)/nilamp_tables.h

NATIVE_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.o \
	$(NATIVE_BUILD)/nilamp_tables.o

NATIVE_PIC_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.pic.o \
	$(NATIVE_BUILD)/nilamp_tables.pic.o

.PHONY: all native native-test native-bench native-host-test clean-native

all: native

native: $(NATIVE_BIN)/nilamp_render $(NATIVE_BIN)/nilamp_taps_render $(NATIVE_BIN)/test_native $(NATIVE_BIN)/nilamp.clap

native-test: $(NATIVE_BIN)/test_native $(NATIVE_BIN)/test_clap_load $(NATIVE_BIN)/nilamp.clap
	$(NATIVE_BIN)/test_native
	$(NATIVE_BIN)/test_clap_load $(NATIVE_BIN)/nilamp.clap

native-bench: $(NATIVE_BIN)/bench_native
	$(NATIVE_BIN)/bench_native

native-host-test: $(NATIVE_BIN)/nilamp.clap
	python3 tools/clap_validate/validate_reaper_clap.py --plugin $<

$(NATIVE_BUILD) $(NATIVE_BIN):
	mkdir -p $@

$(NATIVE_BUILD)/nilamp_tables.o: $(NATIVE_TABLES_C) $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $(NATIVE_TABLES_C) -o $@

$(NATIVE_BUILD)/nilamp_dsp.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_tables.pic.o: $(NATIVE_TABLES_C) $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $(NATIVE_TABLES_C) -o $@

$(NATIVE_BUILD)/nilamp_dsp.pic.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

$(NATIVE_BUILD)/nilamp_dsp_test.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
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

clean-native:
	rm -rf $(NATIVE_BUILD) $(NATIVE_BIN)
