#!/bin/sh
# Run from jtty. No receiver code is modified by this experiment.
set -eu
make sensitivity-build
./build/sensitivity/compare 500 -10 -19 0x8c754ea1 > tests/sensitivity/paired.csv
./build/sensitivity/compare 1000 0 0 0xf8098231 --noise > tests/sensitivity/noise.csv
python3 tests/sensitivity/report.py
gnuplot tests/sensitivity/plot.gp
