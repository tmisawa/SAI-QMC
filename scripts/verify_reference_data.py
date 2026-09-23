#!/usr/bin/env python3
"""Check reference-data checksums and energy/doublon conventions without third-party packages.

Date: 2026-09-08; model: OpenAI GPT-6 (Codex).
This verifies the shipped historical tables, not a new QMC convergence claim.
"""
import csv
import hashlib
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def close(a, b, tol=2e-9):
    if not math.isclose(a, b, rel_tol=tol, abs_tol=tol):
        raise ValueError(f'Inconsistent reference values: {a} != {b}')


def main():
    count = 0
    for line in (ROOT / 'data/SHA256SUMS').read_text().splitlines():
        expected, name = line.split('  ', 1)
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != expected:
            raise ValueError(f'Checksum mismatch: {name}')
        count += 1
    table = ROOT / 'data/benchmark_L468_U4_full_diag_20260626/comparison_qmc_vs_fulldiag.tsv'
    with table.open() as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    if len(rows) != 39 or {int(r['L']) for r in rows} != {4, 6, 8}:
        raise ValueError('Unexpected ED comparison grid')
    for r in rows:
        v = {k: float(x) for k, x in r.items()}
        if not all(math.isfinite(x) for x in v.values()):
            raise ValueError('Non-finite ED comparison entry')
        close(v['T'] * v['beta'], 1)
        close(v['qmc_Ehub_per_site'], v['qmc_Ehub'] / v['L'])
        close(v['ed_Ehub_per_site'], v['ed_Ehub'] / v['L'])
        close(v['diff_Ehub'], v['qmc_Ehub'] - v['ed_Ehub'])
        close(v['diff_Ehub_per_site'], v['diff_Ehub'] / v['L'])
        close(v['sign'], 1)
    p = ROOT / 'data/L6_U4_mu2_dtau_ed_comparison/qmc_L6_U4_energy_doublon_dtau_extrap_vs_ED.dat'
    lines = p.read_text().splitlines()
    names = lines[0].lstrip('# ').split()
    trotter = [dict(zip(names, map(float, l.split()))) for l in lines[1:] if l.strip() and not l.startswith('#')]
    for r in trotter:
        close(r['T'] * r['beta'], 1)
        close(r['ED_ntot'], 6)
        close(r['E_extrap_minus_ED'], r['E_extrap'] - r['ED_E_hub'])
        close(r['D_extrap_minus_ED'], r['D_extrap'] - r['ED_doublon'])
    print(f'PASS: {count} data checksums, {len(rows)} ED comparison rows, {len(trotter)} Trotter rows')


if __name__ == '__main__':
    main()
