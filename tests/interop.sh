#!/bin/sh
set -eu
# Run from the jtty directory, with a Fortran compiler available.
reference=${WSJTX_SOURCE:-../wsjtx-3.2.0-rc1}
reference=$(cd "$reference" && pwd)
compiler=${FC:-gfortran}
if ! command -v "$compiler" >/dev/null 2>&1; then
    printf 'Fortran compiler unavailable: %s (set FC).\n' "$compiler" >&2
    exit 77
fi
mkdir -p build/interop
project=$(pwd)
cd build/interop
"$compiler" -O2 -ffree-line-length-none \
    "$reference/lib/jtty/jtty_tbcc_code_profile.f90" \
    "$reference/lib/jtty/tbcc.f90" \
    "$reference/lib/gfsk_pulse.f90" \
    "$reference/lib/jtty/gen_jttywave.f90" \
    "$project/tests/reference.f90" -o oracle
for word in 026F78D41 20007B009 2001BA019 00B790D29 082C58029 204E42D39 000000049 04A198049 11395558D; do
    ./oracle "$word" reference.f32 > reference.tones
    ../test_interop "$word" reference.tones reference.f32
done
