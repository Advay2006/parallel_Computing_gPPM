CC      = gcc
NVCC    ?= $(or $(shell command -v nvcc 2>/dev/null),$(firstword $(wildcard /usr/local/cuda*/bin/nvcc)),nvcc)
CFLAGS  = -O2 -Wall -Wextra -std=c11
OMPFLAGS ?= -fopenmp
PYTHON  ?= python3
M1_DIR  = src/milestone1
M2_DIR  = src/milestone2
M3_DIR  = src/milestone3
GPU_DIR = src/milestone_gpu
M1_CORE = $(M1_DIR)/gf.c $(M1_DIR)/matrix.c $(M1_DIR)/sd_code.c $(M1_DIR)/codec.c
M2_CORE = $(M2_DIR)/gf.c $(M2_DIR)/matrix.c $(M2_DIR)/sd_code.c $(M2_DIR)/codec.c $(M2_DIR)/ppm.c
M3_CORE = $(M3_DIR)/gf.c $(M3_DIR)/matrix.c $(M3_DIR)/sd_code.c $(M3_DIR)/codec.c $(M3_DIR)/ppm.c
M1_COEFF = $(M1_DIR)/coefficients.c
M2_COEFF = $(M2_DIR)/coefficients.c
M3_COEFF = $(M3_DIR)/coefficients.c
M1_HEADERS = $(M1_DIR)/gf.h $(M1_DIR)/matrix.h $(M1_DIR)/sd_code.h $(M1_DIR)/codec.h $(M1_DIR)/coefficients.h
M2_HEADERS = $(M2_DIR)/gf.h $(M2_DIR)/matrix.h $(M2_DIR)/sd_code.h $(M2_DIR)/codec.h $(M2_DIR)/ppm.h $(M2_DIR)/coefficients.h
M3_HEADERS = $(M3_DIR)/gf.h $(M3_DIR)/matrix.h $(M3_DIR)/sd_code.h $(M3_DIR)/codec.h $(M3_DIR)/ppm.h $(M3_DIR)/coefficients.h
BUILD_DIR = build
RESULTS_DIR = results
PLOTS_DIR = $(RESULTS_DIR)/plots
BIN     = $(BUILD_DIR)/sd_baseline
SWEEP   = $(BUILD_DIR)/sd_sweep
BENCH   = $(BUILD_DIR)/sd_benchmark
PPM_SWEEP = $(BUILD_DIR)/ppm_sweep
PPM_BENCH = $(BUILD_DIR)/ppm_benchmark
M3_SWEEP = $(BUILD_DIR)/m3_sweep
M3_BENCH = $(BUILD_DIR)/m3_benchmark
M3_BENCH_SCALAR = $(BUILD_DIR)/m3_benchmark_scalar
TEST_GF = $(BUILD_DIR)/test_gf
TEST_SD = $(BUILD_DIR)/test_sd
TEST_COEFF = $(BUILD_DIR)/test_coefficients
TEST_PPM = $(BUILD_DIR)/test_ppm
TEST_M3 = $(BUILD_DIR)/test_m3
GPU_OBJ_DIR = $(BUILD_DIR)/milestone_gpu
GPU_RESULTS_DIR = $(RESULTS_DIR)/milestone_gpu
GPU_BENCH = $(BUILD_DIR)/gpu_benchmark
GPU_C_SOURCES = $(GPU_DIR)/gf.c $(GPU_DIR)/matrix.c $(GPU_DIR)/sd_code.c \
	$(GPU_DIR)/codec.c $(GPU_DIR)/ppm.c $(GPU_DIR)/coefficients.c \
	$(GPU_DIR)/gpu_ppm.c $(GPU_DIR)/gpu_benchmark.c
GPU_HEADERS = $(wildcard $(GPU_DIR)/*.h)
GPU_C_OBJECTS = $(patsubst $(GPU_DIR)/%.c,$(GPU_OBJ_DIR)/%.o,$(GPU_C_SOURCES))
GPU_CUDA_OBJECT = $(GPU_OBJ_DIR)/gpu_backend.o
GPU_TEST = $(BUILD_DIR)/test_gpu
GPU_CORE_OBJECTS = $(filter-out $(GPU_OBJ_DIR)/gpu_benchmark.o,$(GPU_C_OBJECTS))
M4_DIR = src/milestone4
M4_RESULTS_DIR = $(RESULTS_DIR)/milestone4
M4_BENCH = $(BUILD_DIR)/m4_benchmark
M4_CPU = $(BUILD_DIR)/m4_benchmark_cpu
M4_PROFILE_CPU = $(BUILD_DIR)/m4_profile_cpu
CUDA_ROOT = $(abspath $(dir $(NVCC))/..)

all: $(BIN) $(SWEEP) $(BENCH) $(PPM_SWEEP) $(PPM_BENCH) $(M3_SWEEP) $(M3_BENCH) $(M3_BENCH_SCALAR)

$(BUILD_DIR) $(RESULTS_DIR):
	mkdir -p $@

$(GPU_OBJ_DIR) $(GPU_RESULTS_DIR):
	mkdir -p $@

$(M4_RESULTS_DIR):
	mkdir -p $@

$(M4_CPU): $(M4_DIR)/benchmark.c $(M3_CORE) $(M3_COEFF) $(M3_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OMPFLAGS) -I$(M3_DIR) -o $@ $(M4_DIR)/benchmark.c $(M3_CORE) $(M3_COEFF)

$(M4_PROFILE_CPU): $(M4_DIR)/benchmark.c $(M4_DIR)/profile_cpu.c $(M3_CORE) $(M3_COEFF) $(M3_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -g $(OMPFLAGS) -DM4_CALLGRIND -Wl,--wrap=gf_cost_c -I$(M3_DIR) -o $@ $(M4_DIR)/benchmark.c $(M4_DIR)/profile_cpu.c $(M3_CORE) $(M3_COEFF)

$(GPU_OBJ_DIR)/m4_benchmark.o: $(M4_DIR)/benchmark.c $(GPU_HEADERS) | $(GPU_OBJ_DIR)
	$(CC) $(CFLAGS) $(OMPFLAGS) -DM4_WITH_CUDA -I$(GPU_DIR) -I$(CUDA_ROOT)/include -c $< -o $@

$(M4_BENCH): $(GPU_CORE_OBJECTS) $(GPU_CUDA_OBJECT) $(GPU_OBJ_DIR)/m4_benchmark.o
	$(NVCC) -o $@ $^ -Xcompiler -fopenmp

m4-benchmark: $(M4_BENCH) | $(M4_RESULTS_DIR)
	$(PYTHON) scripts/run_milestone4.py

m4-analyze:
	$(PYTHON) scripts/analyze_milestone4.py

m4-profile: $(M4_BENCH) $(M4_PROFILE_CPU) | $(M4_RESULTS_DIR)
	$(PYTHON) scripts/profile_milestone4.py

m4-package:
	$(PYTHON) scripts/package_milestone4.py

$(BIN): $(M1_CORE) $(M1_DIR)/main.c $(M1_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M1_DIR) $(CFLAGS) -o $@ $(M1_CORE) $(M1_DIR)/main.c

$(SWEEP): $(M1_CORE) $(M1_COEFF) $(M1_DIR)/sweep.c $(M1_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M1_DIR) $(CFLAGS) -o $@ $(M1_CORE) $(M1_COEFF) $(M1_DIR)/sweep.c

$(BENCH): $(M1_CORE) $(M1_COEFF) $(M1_DIR)/benchmark.c $(M1_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M1_DIR) $(CFLAGS) -o $@ $(M1_CORE) $(M1_COEFF) $(M1_DIR)/benchmark.c

$(PPM_SWEEP): $(M2_CORE) $(M2_COEFF) $(M2_DIR)/ppm_sweep.c $(M2_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M2_DIR) $(CFLAGS) $(OMPFLAGS) -o $@ $(M2_CORE) $(M2_COEFF) $(M2_DIR)/ppm_sweep.c

$(PPM_BENCH): $(M2_CORE) $(M2_COEFF) $(M2_DIR)/ppm_benchmark.c $(M2_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M2_DIR) $(CFLAGS) $(OMPFLAGS) -o $@ $(M2_CORE) $(M2_COEFF) $(M2_DIR)/ppm_benchmark.c

$(M3_SWEEP): $(M3_CORE) $(M3_COEFF) $(M3_DIR)/ppm_sweep.c $(M3_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M3_DIR) $(CFLAGS) $(OMPFLAGS) -o $@ $(M3_CORE) $(M3_COEFF) $(M3_DIR)/ppm_sweep.c

$(M3_BENCH): $(M3_CORE) $(M3_COEFF) $(M3_DIR)/ppm_benchmark.c $(M3_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M3_DIR) $(CFLAGS) $(OMPFLAGS) -o $@ $(M3_CORE) $(M3_COEFF) $(M3_DIR)/ppm_benchmark.c

$(M3_BENCH_SCALAR): $(M3_CORE) $(M3_COEFF) $(M3_DIR)/ppm_benchmark.c $(M3_HEADERS) | $(BUILD_DIR)
	$(CC) -DM3_DISABLE_SIMD -I$(M3_DIR) $(CFLAGS) $(OMPFLAGS) -o $@ $(M3_CORE) $(M3_COEFF) $(M3_DIR)/ppm_benchmark.c

$(TEST_GF): $(M1_DIR)/gf.c tests/test_gf.c $(M1_DIR)/gf.h | $(BUILD_DIR)
	$(CC) -I$(M1_DIR) $(CFLAGS) -o $@ $(M1_DIR)/gf.c tests/test_gf.c

$(TEST_SD): $(M1_CORE) tests/test_sd.c $(M1_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M1_DIR) $(CFLAGS) -o $@ $(M1_CORE) tests/test_sd.c

$(TEST_COEFF): $(M1_COEFF) tests/test_coefficients.c $(M1_DIR)/coefficients.h | $(BUILD_DIR)
	$(CC) -I$(M1_DIR) $(CFLAGS) -o $@ $(M1_COEFF) tests/test_coefficients.c

$(TEST_PPM): $(M2_CORE) $(M2_COEFF) tests/test_ppm.c $(M2_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M2_DIR) $(CFLAGS) $(OMPFLAGS) -o $@ $(M2_CORE) $(M2_COEFF) tests/test_ppm.c

$(TEST_M3): $(M3_CORE) $(M3_COEFF) tests/test_milestone3.c $(M3_HEADERS) | $(BUILD_DIR)
	$(CC) -I$(M3_DIR) $(CFLAGS) $(OMPFLAGS) -o $@ $(M3_CORE) $(M3_COEFF) tests/test_milestone3.c

$(GPU_OBJ_DIR)/%.o: $(GPU_DIR)/%.c $(GPU_HEADERS) | $(GPU_OBJ_DIR)
	$(CC) -I$(GPU_DIR) $(CFLAGS) $(OMPFLAGS) -c $< -o $@

$(GPU_CUDA_OBJECT): $(GPU_DIR)/gpu_backend.cu $(GPU_DIR)/gpu_backend.h | $(GPU_OBJ_DIR)
	$(NVCC) -I$(GPU_DIR) -O2 -std=c++14 -c $< -o $@

$(GPU_BENCH): $(GPU_C_OBJECTS) $(GPU_CUDA_OBJECT) | $(BUILD_DIR)
	$(NVCC) -o $@ $(GPU_C_OBJECTS) $(GPU_CUDA_OBJECT) -Xcompiler -fopenmp

$(GPU_OBJ_DIR)/test_gpu.o: tests/test_gpu.c $(GPU_HEADERS) | $(GPU_OBJ_DIR)
	$(CC) -I$(GPU_DIR) $(CFLAGS) $(OMPFLAGS) -c $< -o $@

$(GPU_TEST): $(GPU_CORE_OBJECTS) $(GPU_CUDA_OBJECT) $(GPU_OBJ_DIR)/test_gpu.o
	$(NVCC) -o $@ $^ -Xcompiler -fopenmp

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

m3-sweep: $(M3_SWEEP) | $(RESULTS_DIR)
	./$(M3_SWEEP) data/FAST-Coefficients.txt $(RESULTS_DIR)/m3_sweep_results.csv

m3-benchmark: $(M3_BENCH) $(M3_BENCH_SCALAR) | $(RESULTS_DIR)
	./$(M3_BENCH_SCALAR) data/FAST-Coefficients.txt $(RESULTS_DIR)/m3_benchmark_scalar.csv 10
	./$(M3_BENCH) data/FAST-Coefficients.txt $(RESULTS_DIR)/m3_benchmark_simd_layers.csv 10 compare

m3-plots: scripts/plot_milestone3.py $(RESULTS_DIR)/m3_benchmark_scalar.csv $(RESULTS_DIR)/m3_benchmark_simd_layers.csv
	$(PYTHON) scripts/plot_milestone3.py --results-dir $(RESULTS_DIR) --output-dir $(RESULTS_DIR)/milestone3

m3-test: $(TEST_M3)
	./$(TEST_M3)

gpu-test: $(GPU_TEST) $(GPU_BENCH) | $(GPU_RESULTS_DIR)
	./$(GPU_TEST)
	./$(GPU_BENCH) data/FAST-Coefficients.txt $(GPU_RESULTS_DIR)/gpu_smoke.csv 1

gpu-benchmark: $(GPU_BENCH) | $(GPU_RESULTS_DIR)
	./$(GPU_BENCH) data/FAST-Coefficients.txt $(GPU_RESULTS_DIR)/gpu_benchmark.csv 10

gpu-plots: scripts/plot_milestone_gpu.py $(GPU_RESULTS_DIR)/gpu_benchmark.csv
	$(PYTHON) scripts/plot_milestone_gpu.py --results $(GPU_RESULTS_DIR)/gpu_benchmark.csv --output-dir $(GPU_RESULTS_DIR)

plots: scripts/plot_milestone2.py | $(RESULTS_DIR)
	$(PYTHON) scripts/plot_milestone2.py --results-dir $(RESULTS_DIR) --output-dir $(PLOTS_DIR)

ppm-test: $(TEST_PPM)
	./$(TEST_PPM)

test: $(TEST_GF) $(TEST_SD) $(TEST_COEFF) $(TEST_PPM) $(TEST_M3)
	./$(TEST_GF)
	./$(TEST_SD)
	./$(TEST_COEFF)
	./$(TEST_PPM)
	./$(TEST_M3)

# Evaluation: thread scaling, ablation and paper reproductions (results/evaluation/).
EVAL_DIR = src/evaluation
EVAL_SRC = $(EVAL_DIR)/eval_bench.c
M2_NOGF = $(M2_DIR)/matrix.c $(M2_DIR)/sd_code.c $(M2_DIR)/codec.c $(M2_DIR)/ppm.c $(M2_COEFF)
EVAL_BINS = $(BUILD_DIR)/eval_m2_scalar $(BUILD_DIR)/eval_m2_simd $(BUILD_DIR)/eval_m3_scalar $(BUILD_DIR)/eval_m3_simd

$(BUILD_DIR)/eval_m2_scalar: $(EVAL_SRC) $(M2_CORE) $(M2_COEFF) $(M2_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OMPFLAGS) -DEVAL_M2 -DEVAL_BUILD='"m2_scalar"' -I$(M2_DIR) -o $@ $(EVAL_SRC) $(M2_CORE) $(M2_COEFF)

# M2's fixed PPM schedule linked against M3's SIMD region arithmetic (M3 gf.h is a superset).
$(BUILD_DIR)/eval_m2_simd: $(EVAL_SRC) $(M2_NOGF) $(M3_DIR)/gf.c $(M2_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OMPFLAGS) -DEVAL_M2 -DEVAL_BUILD='"m2_simd"' -I$(M2_DIR) -o $@ $(EVAL_SRC) $(M2_NOGF) $(M3_DIR)/gf.c

$(BUILD_DIR)/eval_m3_scalar: $(EVAL_SRC) $(M3_CORE) $(M3_COEFF) $(M3_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OMPFLAGS) -DM3_DISABLE_SIMD -DEVAL_BUILD='"m3_scalar"' -I$(M3_DIR) -o $@ $(EVAL_SRC) $(M3_CORE) $(M3_COEFF)

$(BUILD_DIR)/eval_m3_simd: $(EVAL_SRC) $(M3_CORE) $(M3_COEFF) $(M3_HEADERS) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OMPFLAGS) -DEVAL_BUILD='"m3_simd"' -I$(M3_DIR) -o $@ $(EVAL_SRC) $(M3_CORE) $(M3_COEFF)

eval-build: $(EVAL_BINS)

eval-benchmark: $(EVAL_BINS)
	$(PYTHON) scripts/run_evaluation.py

eval-analyze:
	$(PYTHON) scripts/analyze_evaluation.py

clean:
	rm -rf $(BUILD_DIR)

.PHONY: all run sweep benchmark ppm-sweep ppm-benchmark m3-sweep m3-benchmark m3-plots gpu-test gpu-benchmark gpu-plots plots ppm-test m3-test test m4-benchmark m4-profile m4-analyze m4-package eval-build eval-benchmark eval-analyze clean
