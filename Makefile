CC ?= cc
AR ?= ar
CLANG_FORMAT ?= clang-format-18
CPPFLAGS += -Iinclude -Isrc
CFLAGS ?= -O2 -g
CFLAGS += -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
LDLIBS += -lm
LIBSRC = src/registry.c src/callsign.c src/atom.c src/text.c src/crc.c src/fec.c src/modulator.c src/demodulator.c src/synchronizer.c src/receiver.c
OBJ = $(LIBSRC:src/%.c=build/%.o)

.PHONY: all test clean format format-check sanitize

all: build/libjtty.a build/jtty

build:
	mkdir -p $@

build/%.o: src/%.c include/jtty.h src/internal.h | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

build/libjtty.a: $(OBJ)
	$(AR) rcs $@ $^

build/jtty: tools/main.c tools/wav.c tools/wav.h build/libjtty.a
	$(CC) $(CPPFLAGS) $(CFLAGS) tools/main.c tools/wav.c build/libjtty.a $(LDLIBS) -o $@

build/test_%: tests/test_%.c build/libjtty.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libjtty.a $(LDLIBS) -o $@

test: build/test_jtty build/test_reference build/jtty
	./build/test_jtty
	./build/test_reference
	./tests/cli.sh

format:
	$(CLANG_FORMAT) -i include/*.h src/*.h src/*.c tools/*.h tools/*.c tests/*.c tests/sensitivity/*.c

format-check:
	$(CLANG_FORMAT) --dry-run --Werror include/*.h src/*.h src/*.c tools/*.h tools/*.c tests/*.c tests/sensitivity/*.c

sanitize:
	$(MAKE) clean
	$(MAKE) CC=clang CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer' test

clean:
	rm -rf build

.PHONY: interop

interop: build/test_interop
	./tests/interop.sh

build/benchmark: tools/benchmark.c build/libjtty.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $< build/libjtty.a $(LDLIBS) -o $@

.PHONY: sensitivity-build

sensitivity-build: build/libjtty.a
	./tests/sensitivity/build.sh
