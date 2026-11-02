# SP-1 Reverb: Dedicated Early Digital Reverb for Teenage Engineering SP-1
#
#   make test       Host checks: block independence, frequency response, ceilings, clicks
#   make firmware   sp1verb.bin (Zephyr v4.3.1 + Zephyr SDK 0.17.4; see scripts/build-firmware.sh)
#   make bench      Cortex-M4 cycle estimate for the reverb engine (unicorn + capstone)

CC ?= cc
CFLAGS := -O2 -std=gnu11 -Wall -Wextra -fno-math-errno -ffp-contract=off

test: build/fx_test
	mkdir -p build/test
	./build/fx_test build/test

build/fx_test: test/fx_test.c fx/fx.c fx/fx.h fx/fx_math.h
	mkdir -p build
	$(CC) $(CFLAGS) -o $@ test/fx_test.c fx/fx.c -lm

firmware:
	./scripts/build-firmware.sh

bench:
	python3 tools/bench_m4.py -v

clean:
	rm -rf build

.PHONY: test firmware bench clean
