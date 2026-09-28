# JTTY in C11

An independent JTTY encoder and decoder based on the local
`../wsjtx-3.2.0-rc1` source tree. It implements the current **STRUCT30** grammar,
including all receive atom types, text packing profiles, CRC/FEC, and audio
transmission and reception.

The library depends only on the C standard library and `libm`. LiquidDSP is
not required: a small correlation demodulator handles the four fixed tones.
Building the library does not require WSJT-X, Qt, C++, Fortran, or FFTW.

## Build and run

```sh
cd jtty
make
make test

./build/jtty pack 'CQ R1CBU CQ'
./build/jtty pack '599 05' rtty-roundup
./build/jtty tones 'CQ R1CBU CQ'
./build/jtty encode cq.wav 'CQ R1CBU CQ' 1000
./build/jtty decode cq.wav 900 1100
```

The build produces `build/libjtty.a` and `build/jtty`.
Link an application with `cc -Iinclude app.c build/libjtty.a -lm`.

Alternatively, use CMake, including for a shared library:

```sh
cmake -S . -B cmake-build -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON
cmake --build cmake-build
ctest --test-dir cmake-build --output-on-failure
```

`encode` writes a PCM16 mono WAV at 12000 Hz with 0.2-second leading and trailing
guard intervals. The frequency specifies the **lowest tone**, not the band center;
the other tones are spaced by 31.25 Hz. WAV amplitude is 0.8 of full scale.
The library modulator produces unit amplitude without additional silence.

`decode` accepts PCM16 mono WAV files up to 120 seconds long. The default
lowest-tone search range is 200–3000 Hz. Each output line contains the frame
start time, frequency, synchronization score, 34-bit payload, atom text, and
an `[EOM]` marker when applicable.
CLI exit codes: 0 for success, 1 for an error or no decoded frames, and 2 for
invalid arguments. Quote message strings in the shell.

## API

The public interface is [include/jtty.h](include/jtty.h). For integration,
complete examples, buffer ownership, return values, and individual functions,
see [API.md](API.md).

All buffers except internal work arrays belong to the caller. The library has
no mutable global state, so independent calls can run in separate threads.
Callbacks run synchronously, and the frame pointer is valid only during the
callback.

To transmit text, call `jtty_pack`, encode each payload with `jtty_encode`, then
pass the combined tone sequence to `jtty_modulate`. To receive audio, call
`jtty_receive` with a completed buffer of real `float` samples at 12000 Hz.

`jtty_pack` returns the frame count, 0 for empty text, or -1 on error. Its input
is a NUL-terminated string; only the first 80 bytes are considered before
normalization. Normalization folds ASCII to uppercase, removes excess spaces,
converts `~` to a space, and replaces unsupported bytes with `#`. UTF-8 is not
transliterated. `JTTY_RTTY_ROUNDUP` additionally canonicalizes serials following
`599`: `599 05` becomes `599 005`. Expansion beyond 80 characters is an error.

The typed interface consists of `jtty_atom_pack`, `jtty_atom_unpack`, and
`jtty_atom_render`:

| kind | subtype | role | value | value2 | text |
| --- | --- | --- | --- | --- | --- |
| `JTTY_CALL` | six `JTTY_CALL_*` actions | — | — | — | Standard callsign |
| `JTTY_NUM` | `JTTY_SERIAL` … `JTTY_GENERIC_NUMBER` | field/full | Number | — | — |
| `JTTY_LOC` | `JTTY_STATE_PROVINCE` … `JTTY_ADMIN_CODE` | field/full | — | — | 2–3 base36 characters |
| `JTTY_PAIR` | `JTTY_ZONE_LOC` | — | CQ zone | — | 2–3 base36 characters |
| `JTTY_PAIR` | `JTTY_CLASS_SECTION` | — | Count 1–32 | Section 1–86 | Class A–F |
| `JTTY_TIME` | — | field/full | Serial 0–16383 | UTC minutes 0–1439 | — |
| `JTTY_CONTROL` | `JTTY_AGN` … `JTTY_OK_QUERY` | — | — | — | — |
| `JTTY_GRID` | — | field/full | — | — | AA00–RR99 |
| `JTTY_TEXT5` | — | — | — | — | Up to 5 characters |

Zero-initialize atoms: `jtty_atom a = {0};`. Unused fields are ignored.
`role` is `JTTY_FIELD_ONLY` or `JTTY_FULL_EXCHANGE`; full exchanges add `599 `.
Section indices match WSJT-X; `jtty_section(index)` returns the name.
Pack the final atom with `eom=1` and all preceding atoms with `eom=0`.

`jtty_encode` accepts raw 34-bit payloads and adds CRC, FEC, and synchronization.
This low-level function permits invalid source grammar for channel testing;
normal transmission should use payloads produced by the source packing APIs.
`jtty_decode` accepts `scores[46][4]`, with larger values indicating more likely
tones. It verifies the circular trellis path, CRC, reserved-zero bit, and grammar.
On failure it clears the payload when both argument pointers are valid.

`jtty_receive` searches for frames and returns their count, or -1 on error.
Results are chronological. It considers at most 128 acquisition candidates per
call and accepts at most 120 seconds of input. Peak memory scales with duration
and search bandwidth: approximately 90 MB for 120 seconds over 200–3000 Hz.
The `sync` field is a score in [0,1], not an SNR estimate.

Use `jtty_unpack` to assemble text from a known sequence of frames. It inserts
a space after structured atoms, preserves TEXT5 characters, and removes trailing
spaces. Output is limited to 80 characters. An incomplete sequence is allowed;
EOM before the last array element is an error. Associating frames with stations
or sessions is the application's responsibility: the protocol has no frame
number or start-of-message flag, so EOM does not prove that all preceding atoms
were received. The CLI prints individual frames without merging stations.

## Modules

| File | Purpose |
| --- | --- |
| `registry.c` | Alphabet, control phrases, and sections |
| `callsign.c` | Standard call28 callsigns |
| `atom.c` | Bit grammar, validation, and canonical rendering |
| `text.c` | Normalization, recognition, and minimum-frame packing by dynamic programming |
| `crc.c` | CRC-12 |
| `fec.c` | TBCC K=10, Gray mapping, and wrap-around Viterbi |
| `modulator.c` | Continuous-phase 4-GFSK, BT=2, and amplitude envelope |
| `synchronizer.c` | Time/frequency search for the 13-symbol synchronization sequence |
| `demodulator.c` | Four-tone correlators and synchronization refinement |
| `receiver.c` | Receive pipeline, duplicate suppression, and callbacks |
| `tools/wav.c`, `tools/main.c` | WAV and CLI support; not part of the library |

## Validation and limitations

`make test` checks WSJT-X payload and FEC-tone vectors, 71 frame/atom selection
examples, field boundaries, random valid words, text round-trips, correction
of three erroneous tones, noisy reception with unknown timing and frequency,
two simultaneous stations, silence, noise-only input, and the WAV/CLI interface.

```sh
make format                 # Uses the existing .clang-format
make format-check
make sanitize               # Clang, ASan + UBSan
make build/benchmark
./build/benchmark 100        # Deterministic AWGN test; SNR in 2500 Hz
```

Saved `./build/benchmark 100` results: [tests/awgn.csv](tests/awgn.csv).
Reception was 100/100 at each level from −4 to −12 dB, 74/100 at −14 dB,
and 4/100 at −16 dB. No incorrect payloads were observed in these 700 trials.
This is one fixed seed, white noise, and a 950–1050 Hz search window; the result
does not establish guaranteed sensitivity.

The default formatter is `clang-format-18`; override it with `CLANG_FORMAT`.
The system `clang-format-10` cannot read the supplied configuration. The
`.clang-format` file has not been changed.

LeakSanitizer may fail to start under ptrace. In that environment,
`ASAN_OPTIONS=detect_leaks=0 make sanitize` was used: ASan and UBSan run,
but that invocation does not verify memory leaks.

An additional test compares directly against the original Fortran:

```sh
make interop
# Or: WSJTX_SOURCE=/path/to/wsjtx FC=gfortran make interop
```

It builds a separate WSJT-X reference generator, compares tones and waveforms,
and decodes original audio. Fortran remains outside the library. This test
passed with GNU Fortran 9.4.0: tones matched exactly for all nine reference
frames, and the C receiver recovered all original payloads. Relative waveform
RMS differences were 0.0129–0.0486%. This test uses individual noiseless frames
at a 1000 Hz lowest tone; it does not compare receiver sensitivity.
Details: [tests/interop-results.md](tests/interop-results.md).

A paired sensitivity comparison also ran against the original WSJT-X receiver:
500 frames per SNR level, 5000 signal trials, and 1000 noise-only controls.
The C receiver's approximate thresholds are −14.6 dB for 50% decoding and
−13.2 dB for 90%, with SNR measured in 2500 Hz. WSJT-X reached −16.0 and −14.3 dB.
Conditions, confidence intervals, and the reproducible test harness:
[tests/sensitivity/README.md](tests/sensitivity/README.md).

The receiver uses noncoherent correlators and up to four WAVA passes. It does
not implement the WSJT-X coherent list decoder, signal subtraction, frequency
drift or sample-clock tracking, or persistent streaming state. It decodes
completed audio buffers; synthetic results do not establish equal sensitivity
or interference rejection to WSJT-X. Streaming SDR applications need external
buffering and sample-rate conversion.

The format and research sources are described in [PROTOCOL.md](PROTOCOL.md).
License: GPL-3.0; see [COPYING](COPYING). Provenance: [NOTICE](NOTICE).
