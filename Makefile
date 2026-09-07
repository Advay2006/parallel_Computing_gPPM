CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
CPPFLAGS = -Isrc
CORE    = src/gf.c src/matrix.c src/sd_code.c src/codec.c
COEFF   = src/coefficients.c
HEADERS = src/gf.h src/matrix.h src/sd_code.h src/codec.h src/coefficients.h
BUILD_DIR = build
RESULTS_DIR = results
BIN     = $(BUILD_DIR)/sd_baseline
SWEEP   = $(BUILD_DIR)/sd_sweep
BENCH   = $(BUILD_DIR)/sd_benchmark
TEST_GF = $(BUILD_DIR)/test_gf
TEST_SD = $(BUILD_DIR)/test_sd
TEST_COEFF = $(BUILD_DIR)/test_coefficients

all: $(BIN) $(SWEEP) $(BENCH)

$(BUILD_DIR) $(RESULTS_DIR):
	mkdir -p $@

$(BIN): $(CORE) src/main.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) src/main.c

$(SWEEP): $(CORE) $(COEFF) src/sweep.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) $(COEFF) src/sweep.c

$(BENCH): $(CORE) $(COEFF) src/benchmark.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) $(COEFF) src/benchmark.c

$(TEST_GF): src/gf.c tests/test_gf.c src/gf.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/gf.c tests/test_gf.c

$(TEST_SD): $(CORE) tests/test_sd.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) tests/test_sd.c

$(TEST_COEFF): $(COEFF) tests/test_coefficients.c src/coefficients.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(COEFF) tests/test_coefficients.c

run: $(BIN)
	./$(BIN)

sweep: $(SWEEP) | $(RESULTS_DIR)
	./$(SWEEP) data/FAST-Coefficients.txt $(RESULTS_DIR)/sweep_results.csv

benchmark: $(BENCH) | $(RESULTS_DIR)
	./$(BENCH) data/FAST-Coefficients.txt $(RESULTS_DIR)/benchmark_results.csv 10

test: $(TEST_GF) $(TEST_SD) $(TEST_COEFF)
	./$(TEST_GF)
	./$(TEST_SD)
	./$(TEST_COEFF)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run sweep benchmark test clean
