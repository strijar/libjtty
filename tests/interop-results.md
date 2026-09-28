# Direct comparison with WSJT-X

Run on 2026-09-28 using `make interop`, GNU Fortran 9.4.0, and reference
build flags `-O2 -ffree-line-length-none`. Original source files and their
checksums are listed in `reference.sha256`.

Three checks were performed for each payload:

1. All 59 tones from the C encoder match the original Fortran encoder.
2. Compare 22656 audio samples: one frame, 12000 Hz, lowest tone 1000 Hz,
   BT=2. Error is `sqrt(sum((C - Fortran)^2) / sum(Fortran^2))`, with no
   adjustment of timing, phase, or amplitude.
3. The C receiver searches for the original signal within 980–1020 Hz and
   returns exactly one frame containing the original payload.

All checks passed.

| Payload, hex | Relative RMS error |
| --- | ---: |
| 026F78D41 | 0.000223191 |
| 20007B009 | 0.000203486 |
| 2001BA019 | 0.000199464 |
| 00B790D29 | 0.000264923 |
| 082C58029 | 0.000156413 |
| 204E42D39 | 0.000192861 |
| 000000049 | 0.000485711 |
| 04A198049 | 0.000228030 |
| 11395558D | 0.000129056 |

The C modulator computes phase and the frequency pulse in double precision;
the original Fortran generator uses real*4. Audio samples are not bit-identical,
unlike the tone sequences. The maximum measured relative RMS error is 0.0485711%.

This test covers the original transmitter → C receiver direction for nine
individual noiseless frames. It does not run the original WSJT-X receiver,
so it does not establish reverse-direction interoperability or comparative
sensitivity. The separate receiver comparison is documented in
[sensitivity/README.md](sensitivity/README.md).
