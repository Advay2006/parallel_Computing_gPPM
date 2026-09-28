CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
CPPFLAGS = -Isrc
OMPFLAGS ?= -fopenmp
PYTHON  ?= python3
CORE    = src/gf.c src/matrix.c src/sd_code.c src/codec.c
PPM_CORE = $(CORE) src/ppm.c
COEFF   = src/coefficients.c
HEADERS = src/gf.h src/matrix.h src/sd_code.h src/codec.h src/ppm.h src/coefficients.h
BUILD_DIR = build
RESULTS_DIR = results
PLOTS_DIR = $(RESULTS_DIR)/plots
BIN     = $(BUILD_DIR)/sd_baseline
SWEEP   = $(BUILD_DIR)/sd_sweep
BENCH   = $(BUILD_DIR)/sd_benchmark
PPM_SWEEP = $(BUILD_DIR)/ppm_sweep
PPM_BENCH = $(BUILD_DIR)/ppm_benchmark
TEST_GF = $(BUILD_DIR)/test_gf
TEST_SD = $(BUILD_DIR)/test_sd
TEST_COEFF = $(BUILD_DIR)/test_coefficients
TEST_PPM = $(BUILD_DIR)/test_ppm

all: $(BIN) $(SWEEP) $(BENCH) $(PPM_SWEEP) $(PPM_BENCH)

$(BUILD_DIR) $(RESULTS_DIR):
	mkdir -p $@

$(BIN): $(CORE) src/main.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) src/main.c

$(SWEEP): $(CORE) $(COEFF) src/sweep.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) $(COEFF) src/sweep.c

$(BENCH): $(CORE) $(COEFF) src/benchmark.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) $(COEFF) src/benchmark.c

$(PPM_SWEEP): $(PPM_CORE) $(COEFF) src/ppm_sweep.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OMPFLAGS) -o $@ $(PPM_CORE) $(COEFF) src/ppm_sweep.c

$(PPM_BENCH): $(PPM_CORE) $(COEFF) src/ppm_benchmark.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OMPFLAGS) -o $@ $(PPM_CORE) $(COEFF) src/ppm_benchmark.c

$(TEST_GF): src/gf.c tests/test_gf.c src/gf.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/gf.c tests/test_gf.c

$(TEST_SD): $(CORE) tests/test_sd.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) tests/test_sd.c

$(TEST_COEFF): $(COEFF) tests/test_coefficients.c src/coefficients.h | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(COEFF) tests/test_coefficients.c

$(TEST_PPM): $(PPM_CORE) $(COEFF) tests/test_ppm.c $(HEADERS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OMPFLAGS) -o $@ $(PPM_CORE) $(COEFF) tests/test_ppm.c

run: $(BIN)
	./$(BIN)

sweep: $(SWEEP) | $(RESULTS_DIR)
	./$(SWEEP) data/FAST-Coefficients.txt $(RESULTS_DIR)/sweep_results.csv

benchmark: $(BENCH) | $(RESULTS_DIR)
	./$(BENCH) data/FAST-Coefficients.txt $(RESULTS_DIR)/benchmark_results.csv 10

ppm-sweep: $(PPM_SWEEP) | $(RESULTS_DIR)
	./$(PPM_SWEEP) data/FAST-Coefficients.txt $(RESULTS_DIR)/ppm_sweep_results.csv

ppm-benchmark: $(PPM_BENCH) | $(RESULTS_DIR)
	./$(PPM_BENCH) data/FAST-Coefficients.txt $(RESULTS_DIR)/ppm_benchmark_results.csv 10

plots: scripts/plot_milestone2.py | $(RESULTS_DIR)
	$(PYTHON) scripts/plot_milestone2.py --results-dir $(RESULTS_DIR) --output-dir $(PLOTS_DIR)

ppm-test: $(TEST_PPM)
	./$(TEST_PPM)

test: $(TEST_GF) $(TEST_SD) $(TEST_COEFF) $(TEST_PPM)
	./$(TEST_GF)
	./$(TEST_SD)
	./$(TEST_COEFF)
	./$(TEST_PPM)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run sweep benchmark ppm-sweep ppm-benchmark plots ppm-test test clean
