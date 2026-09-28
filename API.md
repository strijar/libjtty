# Using libjtty in your project

`libjtty` is a C11 library for the current JTTY STRUCT30 protocol. It converts
text or typed source atoms into channel tones and 12 kHz real audio, and recovers
validated frames from audio. The public header is [include/jtty.h](include/jtty.h).

## Build and link

The library requires the C standard library and `libm`. There is no runtime
initialization or shutdown function, and no dependency on WSJT-X, Fortran,
FFTW, or LiquidDSP. Optional interoperability and sensitivity tests have their
own dependencies, described in [README.md](README.md).

For a standalone static build:

```sh
make -C /path/to/jtty
cc -std=c11 -O2 -I/path/to/jtty/include app.c \
    /path/to/jtty/build/libjtty.a -lm -o app
```

For an existing CMake project, add the source directory and link the `jtty`
target. Its public include directory and math-library dependency propagate to
the application:

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_radio LANGUAGES C)

add_subdirectory(vendor/jtty)
add_executable(my_radio app.c)
target_link_libraries(my_radio PRIVATE jtty)
```

To build and install separately, optionally as a shared library:

```sh
cmake -S /path/to/jtty -B jtty-build \
    -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTING=OFF -DCMAKE_INSTALL_PREFIX=/path/to/prefix
cmake --build jtty-build
cmake --install jtty-build
```

Installation places `jtty.h` under the include directory, the library under
CMake's configured library directory, and the CLI under the binary directory.
For a shared build, configure your application's runtime library search path
as appropriate for your installation. The project does not currently install
a CMake package configuration or a pkg-config file.

The header also supports C++ callers through `extern "C"`. Compile the library
as C and link it from your C++ application normally.

## Units and data representations

| Constant or value | Meaning |
| --- | --- |
| `JTTY_SAMPLE_RATE` | 12000 real audio samples per second |
| `JTTY_SPS` | 384 samples per symbol |
| `JTTY_SYMBOLS` | 59 tones per frame: 13 sync + 46 payload |
| `JTTY_FRAME_SAMPLES` | 22656 samples, or 1.888 seconds |
| `JTTY_MAX_FRAMES` | 16 source atoms per packed message |
| Tone index | Integer 0–3 |
| `f0`, `fmin`, `fmax`, frame frequency | Lowest-tone frequency in Hz |
| Frame time | Seconds relative to the beginning of the supplied audio buffer |
| String output | NUL-terminated ASCII; allocate the documented full capacity |

A source payload is a `uint64_t` with only its low 34 bits used. Bits 33 through
2 hold the 32-bit grammar word, bit 1 is reserved and must be zero, and bit 0
is the end-of-message flag, EOM. Transmission is MSB first. The integer has no
prescribed byte serialization: do not send its native memory representation
as an on-air frame. `jtty_encode` produces the actual tone sequence.

At most 16 frames occupy 944 symbols and 362496 samples, or 30.208 seconds,
without guard intervals.

## Complete transmit/receive example

Save the following as `app.c` and build it with the standalone command above.
It packs a two-atom message, generates audio, and receives that audio using
an unknown-time/frequency search. Replace the in-memory loopback with your
application's audio input/output as needed.

```c
#include <jtty.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

static void on_frame(const jtty_frame *frame, void *user) {
    size_t *delivered = user;
    ++*delivered;
    printf("t=%.3f s f=%.2f Hz payload=%09" PRIX64 " text=%s%s\n", frame->time, frame->frequency, frame->payload, frame->text, frame->eom ? " [EOM]" : "");
    /* Copy *frame here if it must outlive this callback. */
}

int main(void) {
    uint64_t frames[JTTY_MAX_FRAMES];
    uint8_t  tones[JTTY_MAX_FRAMES * JTTY_SYMBOLS];
    char     normalized[81];
    int      n = jtty_pack("R1CBU 599 123", JTTY_UNKNOWN, frames, normalized);
    if (n < 0) {
        fputs("Cannot pack message\n", stderr);
        return 1;
    }
    if (n == 0)
        return 0;

    for (int i = 0; i < n; ++i) {
        if (jtty_encode(frames[i], tones + i * JTTY_SYMBOLS) != 0)
            return 1;
    }

    size_t signal_samples = (size_t) n * JTTY_FRAME_SAMPLES;
    size_t guard_samples = 2400; /* 0.2 seconds on either side. */
    size_t total_samples = signal_samples + 2 * guard_samples;
    float *audio = calloc(total_samples, sizeof(*audio));
    if (!audio)
        return 1;

    if (jtty_modulate(tones, (size_t) n * JTTY_SYMBOLS, 1000.0, audio + guard_samples, signal_samples) != 0) {
        free(audio);
        return 1;
    }
    printf("Transmitting: %s\n", normalized);

    size_t delivered = 0;
    int    received = jtty_receive(audio, total_samples, 950.0, 1050.0, on_frame, &delivered);
    free(audio);
    if (received < 0) {
        fputs("Receive operation failed\n", stderr);
        return 1;
    }
    printf("Received %zu frames\n", delivered);
    return received == n ? 0 : 1;
}
```

A real application should inspect the received payloads and assemble messages;
the final frame-count check above is only a simple loopback check.

## Pack and unpack text

```c
int jtty_pack(const char *text, int profile, uint64_t frames[16], char normalized[81]);
int jtty_unpack(const uint64_t *frames, size_t count, char text[81]);
```

`jtty_pack` requires non-NULL input/output pointers and a valid profile. Allocate
space for all 16 output payloads and 81 normalized characters. It returns:

- 1–16: number of valid payloads written; only those entries may be consumed.
- 0: empty normalized text; `normalized` is an empty string and there are no frames.
- -1: invalid arguments/profile or a normalization/packing error.

Input is NUL-terminated. The first 80 bytes are considered before normalization;
longer input is truncated. ASCII letters become uppercase, leading/trailing
and repeated ASCII spaces are removed, `~` becomes a space, and unsupported
bytes become `#`. Other whitespace bytes are not treated as spaces. Unicode
is not transliterated. The normalized string should be used for transmit
preview and logging.

| Profile | Behavior |
| --- | --- |
| `JTTY_UNKNOWN` | Context-free compact calls, controls, generic numbers, full generic locations, GRID4, and class/section; otherwise TEXT5 |
| `JTTY_FIELD_DAY` | Same automatic text recognition as Unknown; class/section syntax is already self-identifying |
| `JTTY_RTTY_ROUNDUP` | Also recognizes full SERIAL and STATE_PROVINCE exchanges; canonicalizes eligible decimal tokens after `599` to at least three digits |

For example, `599 05` remains unchanged and takes two frames under Unknown;
under RTTY Roundup it becomes `599 005` and takes one frame. Bare `05` is not
inferred to be a serial. Profile normalization that expands the result beyond
80 characters fails instead of truncating the expanded result.

The packer minimizes frame count while preserving normalized text. It sets
EOM only on the final frame. The typed API below is useful when an application's
field semantics are already known.

`jtty_unpack` requires 1–16 payloads in the order of a single message and an
81-byte output buffer. It returns 0 on success or -1 for invalid arguments,
invalid payload grammar, or EOM before the last array element. EOM on the last
frame is optional, allowing display of a partial message. Structured atoms
supply a trailing space; TEXT5 characters are appended verbatim. Final spaces
are removed, and the displayed result is truncated to 80 characters.

Do not pass unrelated frames or consecutive completed messages as one array.
Except for explicitly documented behavior, output buffers should not be used
after an error; functions may have written partial results.

## Work with typed atoms

```c
int         jtty_atom_pack(const jtty_atom *a, int eom, uint64_t *payload);
int         jtty_atom_unpack(uint64_t payload, jtty_atom *a, int *eom);
int         jtty_atom_render(const jtty_atom *a, char text[81]);
const char *jtty_section(int index);
```

The first three functions return 0 on success and -1 on invalid input. Provide
non-NULL arguments and initialize atoms with `jtty_atom a = {0};`. Its fields
are `kind`, `subtype`, `role`, `value`, `value2`, and `char text[14]`.
`text` must contain a NUL terminator, even when it is unused. Unlike the text
packer, the typed API does not uppercase or normalize strings.

`eom` must be exactly 0 or 1. Set it to 1 only for the final atom. Unpacking
validates all reserved fields and assigned ranges; packing validates the fields
used by the selected kind. Unused descriptor fields are ignored.

A complete helper for a typed serial exchange:

```c
#include <jtty.h>

int pack_serial_exchange(int serial, int final_atom, uint64_t *payload) {
    jtty_atom a = { 0 };
    a.kind = JTTY_NUM;
    a.subtype = JTTY_SERIAL;
    a.role = JTTY_FULL_EXCHANGE;
    a.value = serial;
    return jtty_atom_pack(&a, final_atom, payload);
}
```

`pack_serial_exchange(123, 1, &payload)` encodes `599 123` as a SERIAL atom,
with payload `0x20007B009`. Encoding the same literal text under Unknown selects
a GENERIC_NUMBER atom, so equal rendered text need not imply equal payloads.

### Callsigns

Set `kind=JTTY_CALL`, place a valid uppercase standard callsign in `text`, and
choose an action:

| Subtype | Rendering for K1ABC |
| --- | --- |
| `JTTY_CALL_CQ` | `CQ K1ABC CQ` |
| `JTTY_CALL_BARE` | `K1ABC` |
| `JTTY_CALL_TU_CQ` | `TU K1ABC CQ` |
| `JTTY_CALL_TU` | `K1ABC TU` |
| `JTTY_CALL_AGN` | `K1ABC AGN?` |
| `JTTY_CALL_TU_NOW` | `TU NOW K1ABC` |

Only standard call28 forms that round-trip exactly are accepted. Compound
callsigns, hashed calls, special tokens, and Q-prefix calls are rejected by
this typed form; ordinary text packing can carry unsupported forms as TEXT5.

### Numeric exchanges

Set `kind=JTTY_NUM`, `value` to the number, and `role` to `JTTY_FIELD_ONLY` or
`JTTY_FULL_EXCHANGE`. A full exchange prefixes the field with `599 `.

| Subtype | Valid values | Field rendering |
| --- | --- | --- |
| `JTTY_SERIAL` | 0–131071 | At least three decimal digits |
| `JTTY_CQ_ZONE` | 1–40 | At least two digits |
| `JTTY_ITU_ZONE` | 1–90 | At least two digits |
| `JTTY_AGE` | 0–131071 | Decimal |
| `JTTY_POWER` | 0–131071 | Decimal |
| `JTTY_CHECK` | 0–131071 | At least two digits |
| `JTTY_LICENSE_YEAR` | 0–9999 | Four digits |
| `JTTY_GENERIC_NUMBER` | 0–131071 | Decimal |

### Locations, pairs, time, and grids

| Kind | Required fields and constraints |
| --- | --- |
| `JTTY_LOC` | `role`; subtype `JTTY_STATE_PROVINCE`, `JTTY_SECTION`, `JTTY_COUNTRY_PREFIX`, `JTTY_GENERIC_QTH`, or `JTTY_ADMIN_CODE`; `text` is a canonical 2–3 character base36 token |
| `JTTY_PAIR`, subtype `JTTY_ZONE_LOC` | `value`: CQ zone 1–40; `text`: canonical base36 token; renders `599 <zone> <token>` |
| `JTTY_PAIR`, subtype `JTTY_CLASS_SECTION` | `value`: count 1–32; `value2`: section index 1–86; `text`: one character A–F; renders `<count><class> <section>` |
| `JTTY_TIME` | `role`; `value`: serial 0–16383; `value2`: UTC minutes since midnight, 0–1439; renders serial and HHMM |
| `JTTY_GRID` | `role`; `text`: four-character Maidenhead locator AA00–RR99 |

Base36 uses digits and uppercase letters. A three-character token must not
have a leading zero. Location subtype names describe wire semantics; they do
not enforce membership in a geographical registry. CLASS_SECTION does enforce
section index membership. `jtty_section(1)` through `jtty_section(86)` return
pointers to immutable names; invalid indices return NULL. Do not modify or
free these names. To find an index by name, iterate over that range and compare.

### Control phrases and TEXT5

For `JTTY_CONTROL`, choose a subtype; other fields are unused:

| Subtype | Text | Subtype | Text |
| --- | --- | --- | --- |
| `JTTY_AGN` | `AGN?` | `JTTY_GRID_QUERY` | `GRID?` |
| `JTTY_CALL_QUERY` | `CALL?` | `JTTY_RPRT_QUERY` | `RPRT?` |
| `JTTY_AGN_CALL` | `AGN CALL` | `JTTY_QSL_TU` | `QSL TU` |
| `JTTY_NR_QUERY` | `NR?` | `JTTY_TU` | `TU` |
| `JTTY_AGN_NR` | `AGN NR` | `JTTY_QRZ_QUERY` | `QRZ?` |
| `JTTY_EXCH_QUERY` | `EXCH?` | `JTTY_QSO_B4` | `QSO B4` |
| `JTTY_STATE_QUERY` | `STATE?` | `JTTY_WAIT` | `WAIT` |
| `JTTY_SECTION_QUERY` | `SECTION?` | `JTTY_NIL_QUERY` | `NIL?` |
| `JTTY_ZONE_QUERY` | `ZONE?` | `JTTY_OK_QUERY` | `OK?` |

For `JTTY_TEXT5`, put up to five supported characters in `text`. Packing pads
short strings with spaces. `jtty_atom_render` preserves those five characters,
including padding; `jtty_unpack` removes trailing padding from the assembled
message. The exact 64-character alphabet is:

```text
0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ +-./?!"#$%,&*()_'=[]{}<>|:;
```

## Channel coding and custom demodulators

```c
int jtty_encode(uint64_t payload, uint8_t tones[59]);
int jtty_decode(const float scores[46][4], uint64_t *payload);
```

`jtty_encode` writes 59 tones, including the 13-symbol synchronization prefix.
It accepts any value that fits in 34 bits; it does not validate source grammar.
Use payloads from the source codec for ordinary transmission. It returns -1
for a NULL output pointer or a payload wider than 34 bits.

`jtty_decode` receives scores for the 46 payload symbols only: row index is
symbol time, column index is tone 0–3. Larger finite scores mean greater
likelihood. Squared correlation magnitudes are suitable. Keep relative
reliabilities across symbols; the decoder applies a common internal scale.
If you build your own demodulator, acquire the frame and remove the sync prefix
before supplying these scores. This function does not search time or frequency.

Success requires a closed tail-biting path, CRC, reserved-zero bit, and valid
source grammar. It returns 0 with a valid payload, otherwise -1. NaN/Inf scores
and entirely uninformative equal-tone observations are rejected. On failure,
`*payload` is zero when both argument pointers are non-NULL. It allocates no
heap memory, but uses tens of kilobytes of stack for trellis storage.

## Generate audio

```c
int jtty_modulate(const uint8_t *tones, size_t count, double f0, float *audio, size_t capacity);
```

`count` is the number of tones, not frames or samples. It must be 1–944;
each tone must be 0–3. Allocate at least `count * JTTY_SPS` floats and pass
capacity in samples. The function writes exactly that many samples and leaves
any extra capacity untouched. Both pointers must be non-NULL.

`f0` must be finite and satisfy `0 < f0` and `f0 + 93.75 < 6000`. The output is
real audio at 12000 Hz with nominal unit peak amplitude, BT=2 Gaussian frequency
shaping, and amplitude ramps at the beginning and end. The function returns
0 on success or -1 for invalid parameters or allocation failure.

For a multi-frame message, concatenate all encoded tones and call the modulator
once. Separate calls reset phase and apply separate edge ramps. The function
adds no guard silence and does not resample, quantize, play, or save audio.
Apply application gain before conversion to your sound device's PCM format.

## Receive audio and handle frames

```c
int jtty_receive(const float *audio, size_t count, double fmin, double fmax, jtty_frame_callback callback, void *user);
```

Supply completed, real, mono audio at exactly 12000 Hz. For signed PCM16,
convert each sample using `(float)sample / 32768.0f`. For stereo or other sample
rates, select/mix a channel and resample before calling the library. Complex
IQ must first be converted into the expected real audio representation.

Input conditions:

- `audio` must be non-NULL, including for a zero-length buffer.
- `count` is a sample count, at most `120 * JTTY_SAMPLE_RATE` (1440000).
- Samples must be finite with absolute value no greater than `1e6`; normalized
  audio around ±1 is the conventional input.
- Frequencies must be finite and satisfy `0 < fmin <= fmax` and
  `fmax + 93.75 < 6000`. A narrower search window reduces work and memory.

A valid buffer shorter than one complete frame returns 0. Otherwise the function
searches up to 128 candidates, validates recovered payloads, suppresses duplicate
findings, sorts accepted frames by time, and then invokes the callback for each.
It returns the accepted frame count, 0 if none were found, or -1 on invalid input
or allocation failure. `callback` may be NULL when only the count is needed.

Each `jtty_frame` contains:

| Field | Meaning |
| --- | --- |
| `payload` | Validated 34-bit source word |
| `time` | Estimated start time in seconds from this buffer's first sample |
| `frequency` | Estimated lowest-tone frequency in Hz |
| `sync` | Synchronization score in [0,1]; not an SNR estimate |
| `text[81]` | Canonical atom text; TEXT5 may include trailing spaces |
| `eom` | 1 for the sender's final atom, otherwise 0 |

The callback receives the `user` pointer unchanged. It runs synchronously on
the calling thread and returns `void`; there is no callback cancellation
mechanism. The library-owned frame pointer is valid only during the callback.
Copy the entire struct or the fields you need before returning.

## Message assembly and streaming applications

The receiver emits atoms, not complete station conversations. Group frames by
frequency and time continuity in your application before using `jtty_unpack`.
Frames of one contiguous transmission are spaced by 1.888 seconds. Allow for
estimation error and interruptions; do not merge frames solely because their
text looks related. Use EOM to close an assembly, but do not treat it as proof
that every earlier atom was received: the wire format has no sequence number
or start-of-message flag. A callback count alone does not establish completeness.

The receive API has no persistent streaming state. For live audio, maintain an
external buffer and run decoding on a worker thread, not an audio-device
callback: acquisition allocates memory and may take significant time. A moving
window must retain enough overlap to contain a complete frame across window
boundaries. For example, four-second windows advanced by two seconds retain
two seconds of overlap, exceeding the 1.888-second frame length. This is an
integration pattern, not a built-in scheduler or a measured latency guarantee.

Convert relative frame times to absolute sample positions using each window's
origin, and deduplicate frames across overlapping calls. Within-call duplicate
suppression does not persist between calls. Use payload, absolute timing, and
frequency together; identical text may be transmitted legitimately more than
once. Keep device buffering, resampling, frame ownership, and message assembly
under application control.

## Ownership, concurrency, and error handling

Input and output capacities are the caller's responsibility; array dimensions
in C function declarations do not allocate storage. Use separate, non-overlapping
input/output buffers unless explicitly shown otherwise. The library does not
retain caller buffers after a call, return heap objects to free, or modify its
input audio. Internal allocations are released before returning. You own and
free audio buffers allocated by your application.

All functions except `jtty_pack`, `jtty_receive`, and `jtty_section` return 0
on success and -1 on failure. `jtty_pack` and `jtty_receive` return counts;
`jtty_section` returns an immutable pointer or NULL. There is no structured
error-detail API, and callers should not rely on `errno` to classify failures.
In particular, `jtty_decode` uses -1 for both invalid observations and an ordinary
decoding failure; failure to decode a noisy frame is expected.

There is no mutable global state in the C library. Independent calls are safe
in separate threads provided their buffers and callback state are not modified
concurrently without application synchronization. A wide, long acquisition may
use substantial memory: approximately 90 MB for 120 seconds over 200–3000 Hz.
Include trellis stack storage when sizing worker-thread stacks on embedded systems.

For measured AWGN performance and its limitations, see the
[sensitivity report](tests/sensitivity/README.md). The library does not implement
frequency-drift tracking, fading compensation, or the original WSJT-X coherent
list decoder. Protocol details and source provenance are in
[PROTOCOL.md](PROTOCOL.md) and [NOTICE](NOTICE); the license is [GPL-3.0](COPYING).
