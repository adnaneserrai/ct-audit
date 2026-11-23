# ct-audit: build and run everything with `make all` (run inside Linux/WSL).
ifeq ($(origin CC),default)
CC := gcc
endif
OPT    ?= -O2
CORE   ?= 3
N      ?= 300000
SEEDS  ?= 1 2 3
PQ      = third_party/PQClean
KEM     = $(PQ)/crypto_kem/ml-kem-768/clean
DSA     = $(PQ)/crypto_sign/ml-dsa-65/clean
TAG     = $(CC)$(subst -,_,$(OPT))
BUILD   = build/$(TAG)
CFLAGS  = $(OPT) -Wall -Wno-unused-function -Isrc -I$(PQ)/common
BINS    = $(BUILD)/ct_targets $(BUILD)/ct_mlkem $(BUILD)/ct_extra

.PHONY: all build run matrix ctgrind mldsa-iters aes-probe plots clean
all: matrix ctgrind mldsa-iters aes-probe plots

build: $(BINS)

$(BUILD)/ct_targets: src/targets.c src/ctaudit.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ $< -lm

$(BUILD)/ct_mlkem: src/target_mlkem.c src/ctaudit.h $(wildcard $(KEM)/*.c) $(PQ)/common/fips202.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(KEM) -o $@ src/target_mlkem.c $(KEM)/*.c $(PQ)/common/fips202.c -lm

$(BUILD)/ct_extra: src/target_extra.c src/ctaudit.h $(wildcard $(DSA)/*.c) $(PQ)/common/fips202.c $(PQ)/common/aes.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -Wno-unused-result -I$(DSA) -o $@ src/target_extra.c $(DSA)/*.c $(PQ)/common/fips202.c $(PQ)/common/aes.c -lm

# one configuration, first seed only, results on stdout
run: build
	@for b in $(BINS); do taskset -c $(CORE) $$b $(N) - $(firstword $(SEEDS)); done

# full compiler x optimisation matrix, several seeds per cell
matrix:
	@mkdir -p results
	python3 scripts/run_matrix.py --core $(CORE) --n $(N) --seeds $(SEEDS)

# valgrind-based constant-time check (secret data marked uninitialised)
ctgrind: build/ctgrind_kem build/ctgrind_mac
	@mkdir -p results
	valgrind -q build/ctgrind_kem > results/ctgrind_kem.txt 2>&1; cat results/ctgrind_kem.txt
	valgrind -q build/ctgrind_mac > results/ctgrind_mac.txt 2>&1; cat results/ctgrind_mac.txt

build/ctgrind_kem: tools/ctgrind_kem.c $(wildcard $(KEM)/*.c)
	@mkdir -p build
	gcc -O1 -g -I$(PQ)/common -I$(KEM) -o $@ tools/ctgrind_kem.c $(KEM)/*.c $(PQ)/common/fips202.c

build/ctgrind_mac: tools/ctgrind_mac.c
	@mkdir -p build
	gcc -O1 -g -I$(PQ)/common -o $@ tools/ctgrind_mac.c $(PQ)/common/fips202.c

# ML-DSA-65 signing: iterations of the rejection loop per message
mldsa-iters:
	@mkdir -p build results
	gcc -O2 -Isrc -I$(PQ)/common -I$(DSA) -o build/mldsa_iters tools/mldsa_iters.c $(DSA)/*.c $(PQ)/common/fips202.c -Wl,--wrap=PQCLEAN_MLDSA65_CLEAN_polyvecl_uniform_gamma1 -lm
	taskset -c $(CORE) build/mldsa_iters 20000 | tee results/mldsa_iters.txt

# follow-up experiments on the S-box AES observation
aes-probe:
	@mkdir -p results
	sh scripts/aes_probe.sh $(CORE) 1000000 | tee results/aes_probe.txt

plots: build
	@mkdir -p results
	taskset -c $(CORE) $(BUILD)/ct_targets 250000 results/trace_targets.csv 1 > /dev/null
	python3 scripts/plot.py

clean:
	rm -rf build
