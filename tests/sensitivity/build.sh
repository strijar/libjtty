#!/bin/sh
set -eu
reference=${WSJTX_SOURCE:-../wsjtx-3.2.0-rc1}
reference=$(cd "$reference" && pwd)
project=$(pwd)
compiler=${FC:-gfortran}
mkdir -p build/sensitivity
cd build/sensitivity
for file in lib/fftw3mod.f90 \
    lib/77bit/packjt77_schema.f90 lib/77bit/packjt77_grammar.f90 lib/77bit/packjt77.f90 \
    lib/jtty/jtty_source_codec.f90 lib/jtty/jtty_mod.f90 \
    lib/jtty/jtty_tbcc_code_profile.f90 lib/jtty/tbcc.f90 lib/jtty/jtty_fec_mod.f90 \
    lib/jtty/jtty_tbcc_list_decoder.f90 lib/jtty/jtty_tbcc_decoder.f90 \
    lib/jtty/jtty_payload_correlators.f90 lib/jtty/jtty_mdecode.f90 \
    lib/jtty/rjtty_sub.f90 lib/jtty/ana64a.f90 lib/jtty/gen_syncwave.f90 \
    lib/jtty/jtty_peakup.f90 lib/jtty/subtract_jtty.f90 lib/jtty/gen_jttywave.f90 \
    lib/gfsk_pulse.f90 lib/four2a.f90 lib/twkfreq.f90 lib/db.f90 lib/chkcall.f90; do
    "$compiler" -O2 -ffree-line-length-none -ffunction-sections -fdata-sections \
        -I"$reference/lib" -c "$reference/$file"
done
"$compiler" -O2 -c "$project/tests/sensitivity/reference_rx.f90"
${CC:-cc} -O2 -std=c11 -Wall -Wextra -Wpedantic -I"$project/include" \
    -c "$project/tests/sensitivity/compare.c" -o compare.o
"$compiler" -Wl,--gc-sections ./*.o "$project/build/libjtty.a" -lfftw3f -lfftw3f_threads -lm -o compare
