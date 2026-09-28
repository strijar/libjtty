#!/bin/sh
set -eu
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT HUP INT TERM
./build/jtty encode "$work/test.wav" 'CQ R1CBU CQ' 1001.7 > "$work/encode.txt"
./build/jtty decode "$work/test.wav" 900 1100 > "$work/decode.txt"
grep -q 'CQ R1CBU CQ \[EOM\]' "$work/decode.txt"
./build/jtty pack '599 0123' rtty-roundup > "$work/pack.txt"
grep -q '20007B009' "$work/pack.txt"
printf 'RIFF\000\000\000\000WAVE' > "$work/bad.wav"
if ./build/jtty decode "$work/bad.wav" 900 1100 2>/dev/null; then exit 1; fi
if ./build/jtty pack 'HELLO' invalid 2>/dev/null; then exit 1; fi
printf 'CLI tests passed.\n'
