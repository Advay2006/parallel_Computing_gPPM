CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11
SRC     = src/gf.c src/matrix.c src/sd_code.c src/codec.c src/main.c
BIN     = sd_baseline

$(BIN): $(SRC)
	$(CC) $(CFLAGS) -o $@ $(SRC)

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(BIN)

.PHONY: run clean
