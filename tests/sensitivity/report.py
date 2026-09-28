#!/usr/bin/env python3
"""Summarize paired AWGN trials; Python standard library only."""
import csv
import json
import math
from pathlib import Path

root = Path(__file__).resolve().parent
rows = list(csv.DictReader((root / 'paired.csv').open()))
rows.sort(key=lambda r: float(r['snr_db']))


def wilson(k, n):
    p, z = k / n, 1.959963984540054
    den = 1 + z*z/n
    mid = (p + z*z/(2*n))/den
    half = z*math.sqrt(p*(1-p)/n + z*z/(4*n*n))/den
    return max(0, mid-half), min(1, mid+half)


def crossing(key, target):
    for left, right in zip(rows, rows[1:]):
        p0, p1 = int(left[key])/int(left['trials']), int(right[key])/int(right['trials'])
        if p0 <= target <= p1 and p1 > p0:
            x0, x1 = float(left['snr_db']), float(right['snr_db'])
            return x0 + (target-p0)*(x1-x0)/(p1-p0)
    return None


summary = {}
for key, name in [('c_correct', 'C'), ('wsjtx_correct', 'WSJT-X')]:
    summary[name] = {'snr50_db': crossing(key, 0.5), 'snr90_db': crossing(key, 0.9)}
(root / 'thresholds.json').write_text(json.dumps(summary, indent=2) + '\n')
lines = ['# SNR C C_low C_high WSJT WSJT_low WSJT_high']
table = ['| SNR, dB | C: received frames (95% CI) | WSJT-X: received frames (95% CI) |',
         '| ---: | ---: | ---: |']
for row in rows:
    n = int(row['trials'])
    values, rendered = [float(row['snr_db'])], []
    for key in ['c_correct', 'wsjtx_correct']:
        k = int(row[key])
        low, high = wilson(k, n)
        values.extend([k/n, low, high])
        rendered.append(f'{k}/{n} = {100*k/n:.1f}% ({100*low:.1f}–{100*high:.1f}%)')
    lines.append(' '.join(f'{v:.7g}' for v in values))
    table.append(f"| {row['snr_db']} | {' | '.join(rendered)} |")
(root / 'curve.dat').write_text('\n'.join(lines) + '\n')
(root / 'rates.md').write_text('\n'.join(table) + '\n')
print(json.dumps(summary, indent=2))
print('Wrong output counts:', {k: sum(int(r[k]) for r in rows) for k in ['c_wrong', 'wsjtx_wrong']})
