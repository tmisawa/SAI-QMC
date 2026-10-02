#!/usr/bin/env python3
"""Independent checks of the ED references (design of 2026-10-02, 7.2).

1. At U=0 the ED of hopping_app.txt must equal momentum formulas with half-integer
   kx (antiperiodic x) and integer ky (periodic y, whose doubled length-2 bond gives
   2t cos ky): E/N = (2/N) sum_k eps_k f_k, D = 1/4, Szz(q) = (1/2N) sum_k f_k (1 - f_{k+q}).
2. ed_app.json and ed_pp.json are half filled and obey the sum rule
   sum_q Szz(q) = (N - 2 N D) / 4 at every beta.
usage: python3 check_ed_reference.py   (runs ed_finite_t.py at U=0, about 40 s)
"""
import json
import math
import subprocess
import sys
from pathlib import Path

import validation_io as vio

HERE = Path(__file__).resolve().parent
LX, LY, N = 4, 2, 8


def band(kx, ky):
    return (2.0 * -1.0 * math.cos(2.0 * math.pi * (kx + 0.5) / LX) +
            2.0 * -1.0 * math.cos(2.0 * math.pi * ky / LY))


def free_values(beta):
    def f(e):
        return 1.0 / (1.0 + math.exp(beta * e))

    ks = [(kx, ky) for ky in range(LY) for kx in range(LX)]
    energy = 2.0 * sum(band(*k) * f(band(*k)) for k in ks) / N
    szz = {}
    for my in range(LY):
        for mx in range(LX):
            szz[(mx, my)] = sum(f(band(kx, ky)) * (1.0 - f(band((kx + mx) % LX, (ky + my) % LY)))
                                for kx, ky in ks) / (2.0 * N)
    return energy, szz


def main():
    failures = []
    out = subprocess.run([sys.executable, str(HERE / 'ed_finite_t.py'),
                          str(HERE / 'hopping_app.txt'), '4', '2', '0', '2,4'],
                         capture_output=True, text=True, check=True).stdout
    for r in vio.validate_ed(json.loads(out), 0.0, 'U=0 ED').values():
        energy, szz = free_values(r['beta'])
        worst = max([abs(r['E_hub_per_site'] - energy), abs(r['doublon_per_site'] - 0.25)] +
                    [abs(e['value'] - szz[(e['mx'], e['my'])]) for e in r['Szz']])
        print(f"U=0 beta={r['beta']:g}: max |ED - momentum formula| = {worst:.2e}")
        if not math.isfinite(worst) or worst > 1e-10:
            failures.append(f"U=0 beta={r['beta']:g}")
    for name in ('ed_app.json', 'ed_pp.json'):
        for r in vio.read_ed(HERE / name).values():
            rule = 0.25 * (N - 2.0 * N * r['doublon_per_site'])
            total = sum(e['value'] for e in r['Szz'])
            print(f"{name} beta={r['beta']:g}: ntot={r['ntot']:.12f} "
                  f"sum_q Szz - rule = {total - rule:.2e}")
            if (not math.isfinite(total-rule) or abs(r['ntot'] - N) > 1e-8 or
                    abs(total - rule) > 1e-10):
                failures.append(f"{name} beta={r['beta']:g}")
    print('ED REFERENCE CHECKS ' + ('PASSED' if not failures else 'FAILED: ' + ', '.join(failures)))
    return 0 if not failures else 1


if __name__ == '__main__':
    try:
        sys.exit(main())
    except (vio.DataError, vio.NumericHold, OSError, ValueError, TypeError, KeyError) as exc:
        print('ED REFERENCE CHECKS FAILED: '+str(exc).replace(str(HERE), '.'))
        sys.exit(1)
