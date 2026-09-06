CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
CORE    = src/gf.c src/matrix.c src/sd_code.c src/codec.c
BIN     = sd_baseline
SWEEP   = sd_sweep

all: $(BIN) $(SWEEP)

$(BIN): $(CORE) src/main.c
	$(CC) $(CFLAGS) -o $@ $(CORE) src/main.c

$(SWEEP): $(CORE) src/sweep.c
	$(CC) $(CFLAGS) -o $@ $(CORE) src/sweep.c

run: $(BIN)
	./$(BIN)

sweep: $(SWEEP)
	./$(SWEEP) data/FAST-Coefficients.txt sweep_results.csv

clean:
	rm -f $(BIN) $(SWEEP) sweep_results.csv

.PHONY: all run sweep clean
