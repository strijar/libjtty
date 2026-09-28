# Streaming receiver validation

Measured on 2026-09-28 in the development environment, using the default Makefile
build (`cc -O2`). The baseline executables were built from the pre-change source
before replacing the offline receiver. These results are local regression checks,
not a hardware-independent throughput or sensitivity guarantee.

## Allocation and packet boundaries

`make test` includes `test_stream`, which wraps malloc/calloc/realloc/free in the
static build. It checks exactly one allocation at receiver creation and none
during process, flush or reset, including successful frame callbacks. Packet
sizes 1, 47, 48, 137, 256, 4096, a full recording and deterministic random sizes
produce identical payloads, times, frequencies, sync scores and candidate counters.

Coverage includes audio-ring wraparound, DFT reconstruction after 65536 samples,
frames at stream start/end and acquisition interval boundaries, repeated payloads,
a stream longer than 120 seconds, incomplete-frame flush, reset/reuse, rejected
input without partial consumption, reentrancy guards, candidate overflow and
budget exhaustion. Existing tests also cover overlapping stations and AWGN.

All Makefile tests and static ASan/UBSan CMake tests passed. Shared-library CMake
tests passed; allocator wrapping is enabled only for the static CMake build.
The complete example in API.md compiled and recovered both expected frames.
The original Fortran interop test could not run because the local WSJT-X source
tree was unavailable. Its updated C test and sensitivity harness compile.

## Synthetic AWGN comparison

Run: `build/benchmark 300` for each implementation, using the same deterministic
PRNG and 950–1050 Hz search band. Streaming configuration: capacity 128, budget
128 per 256 ms. There are 300 trials at each SNR, 2100 total per implementation.

| SNR in 2500 Hz (dB) | Baseline correct | Streaming correct | False frames, each |
| --- | ---: | ---: | ---: |
| -4 | 300 | 300 | 0 |
| -6 | 300 | 300 | 0 |
| -8 | 300 | 300 | 0 |
| -10 | 300 | 300 | 0 |
| -12 | 299 | 299 | 0 |
| -14 | 227 | 227 | 0 |
| -16 | 16 | 16 | 0 |

Counts match at every SNR; this does not prove equal behavior for all signals.
Online peak selection and fixed per-interval budgets differ from global offline
peak ranking. Narrow-band short-recording runtime was essentially unchanged in
this run (11.70 s baseline, 11.85 s streaming).

## Long, broad-band recording

Generate a WAV for `R1CBU 599 123` at 1000 Hz with the CLI, then concatenate its
PCM data 25 times with a matching WAV header. The input is 104.4 seconds long,
contains 50 frames, and includes the CLI's guard intervals. Decode with
200–3000 Hz bounds; streaming capacity/budget are both 128, input chunks are
256 samples. `/usr/bin/time` measured the entire CLI including WAV loading.

| Metric | Baseline | Streaming |
| --- | ---: | ---: |
| Correct frames | 50 | 50 |
| Elapsed seconds | 2.61 | 2.08 |
| Peak RSS (KiB) | 80596 | 7128 |

This is a single timing observation on a clean signal. It is not a worst-case
latency test or a statistical performance benchmark. CLI RSS includes its
whole-file WAV input buffer; library context memory remains constant.

`jtty_rx_get_stats` reports these exact context allocation sizes on this 64-bit
build with capacity 128:

| Lowest-tone search band | Context bytes |
| --- | ---: |
| 950–1050 Hz | 300096 |
| 200–3000 Hz | 456488 |

The modulator no longer allocates a frequency history array. Baseline and new
CLI WAV output for the two-frame message above matched byte-for-byte.
