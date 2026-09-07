CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
CPPFLAGS = -Isrc
CORE    = src/gf.c src/matrix.c src/sd_code.c src/codec.c
COEFF   = src/coefficients.c
HEADERS = src/gf.h src/matrix.h src/sd_code.h src/codec.h src/coefficients.h
BIN     = sd_baseline
SWEEP   = sd_sweep
BENCH   = sd_benchmark
TEST_GF = test_gf
TEST_SD = test_sd
TEST_COEFF = test_coefficients

all: $(BIN) $(SWEEP) $(BENCH)

$(BIN): $(CORE) src/main.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) src/main.c

$(SWEEP): $(CORE) $(COEFF) src/sweep.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) $(COEFF) src/sweep.c

$(BENCH): $(CORE) $(COEFF) src/benchmark.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) $(COEFF) src/benchmark.c

$(TEST_GF): src/gf.c tests/test_gf.c src/gf.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ src/gf.c tests/test_gf.c

$(TEST_SD): $(CORE) tests/test_sd.c $(HEADERS)
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(CORE) tests/test_sd.c

$(TEST_COEFF): $(COEFF) tests/test_coefficients.c src/coefficients.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -o $@ $(COEFF) tests/test_coefficients.c

run: $(BIN)
	./$(BIN)

sweep: $(SWEEP)
	./$(SWEEP) data/FAST-Coefficients.txt sweep_results.csv

benchmark: $(BENCH)
	./$(BENCH) data/FAST-Coefficients.txt benchmark_results.csv 10

test: $(TEST_GF) $(TEST_SD) $(TEST_COEFF)
	./$(TEST_GF)
	./$(TEST_SD)
	./$(TEST_COEFF)

clean:
	rm -f $(BIN) $(SWEEP) $(BENCH) $(TEST_GF) $(TEST_SD) $(TEST_COEFF) \
		sweep_results.csv benchmark_results.csv

.PHONY: all run sweep benchmark test clean
