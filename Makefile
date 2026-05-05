CC ?= cc

NATIVE_DIR := native
NATIVE_BUILD := $(NATIVE_DIR)/build
NATIVE_BIN := $(NATIVE_DIR)/bin
NATIVE_GENERATED := $(NATIVE_DIR)/generated

CFLAGS ?= -std=c11 -O3 -Wall -Wextra -Wpedantic -Werror -I$(NATIVE_DIR)/src -I$(NATIVE_GENERATED)
LDFLAGS ?=
LDLIBS ?= -lm

NATIVE_TABLES_C := $(NATIVE_GENERATED)/nilamp_tables.c
NATIVE_TABLES_H := $(NATIVE_GENERATED)/nilamp_tables.h

NATIVE_OBJS := \
	$(NATIVE_BUILD)/nilamp_dsp.o \
	$(NATIVE_BUILD)/nilamp_tables.o

.PHONY: all native native-test clean-native

all: native

native: $(NATIVE_BIN)/nilamp_render $(NATIVE_BIN)/nilamp_taps_render $(NATIVE_BIN)/test_native

native-test: $(NATIVE_BIN)/test_native
	$(NATIVE_BIN)/test_native

$(NATIVE_BUILD) $(NATIVE_BIN):
	mkdir -p $@

$(NATIVE_BUILD)/nilamp_tables.o: $(NATIVE_TABLES_C) $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $(NATIVE_TABLES_C) -o $@

$(NATIVE_BUILD)/nilamp_dsp.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BUILD)/nilamp_dsp_test.o: $(NATIVE_DIR)/src/nilamp_dsp.c $(NATIVE_DIR)/src/nilamp_dsp.h $(NATIVE_TABLES_H) | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BUILD)/nilamp_render.o: $(NATIVE_DIR)/src/nilamp_render.c $(NATIVE_DIR)/src/nilamp_dsp.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(NATIVE_BIN)/nilamp_render: $(NATIVE_BUILD)/nilamp_render.o $(NATIVE_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/nilamp_taps_render.o: $(NATIVE_DIR)/src/nilamp_render.c $(NATIVE_DIR)/src/nilamp_dsp.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_TAPS_RENDER -c $< -o $@

$(NATIVE_BIN)/nilamp_taps_render: $(NATIVE_BUILD)/nilamp_taps_render.o $(NATIVE_OBJS) | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

$(NATIVE_BUILD)/test_native.o: $(NATIVE_DIR)/tests/test_native.c $(NATIVE_DIR)/src/nilamp_dsp.h | $(NATIVE_BUILD)
	$(CC) $(CFLAGS) -DNILAMP_ENABLE_TEST_API -c $< -o $@

$(NATIVE_BIN)/test_native: $(NATIVE_BUILD)/test_native.o $(NATIVE_BUILD)/nilamp_dsp_test.o $(NATIVE_BUILD)/nilamp_tables.o | $(NATIVE_BIN)
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

clean-native:
	rm -rf $(NATIVE_BUILD) $(NATIVE_BIN)
