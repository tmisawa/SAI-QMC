#!/usr/bin/env python3
"""Pre-registered analysis of the 4x2 AP/P U=4 validation (design of 2026-10-02, 7.3).

  python3 analyze_antiperiodic_ed.py --self-test   # extrapolation fixtures (needs numpy)
  python3 analyze_antiperiodic_ed.py               # standard library only

The second form reads runs/, hopping_app.txt, ed_app.json and ed_pp.json, prints a
Markdown report, and writes analysis_output.md and summary.json. Each run's matrix is
checked through runs/.../hopping_used.sha256 (and hopping_used.txt when present).
Exit status: 0 PASS, 1 FAIL, 2 HOLD, 3 invalid or incomplete data.

Each observable is averaged over the 16 independent series of every (dtau, beta);
s_i is the standard error of that mean (ddof=1). The dtau -> 0 value a comes from a
weighted least-squares line in x = dtau^2 with W = diag(1/s_i^2), C = (X^T W X)^-1 and
sigma_a = sqrt(C_00), never rescaled by the residual chi^2. z = (a - ED) / sigma_a.
  P1  E/N, D, Szz(Q), Sperp(Q)/2 versus the AP/P ED: |z| < 3.5 (8 comparisons)
  P2  Szz(q), Sperp(q)/2 for all 8 q versus the AP/P ED: |z| < 4 (32 comparisons)
  N1  D, Szz(1,1), Szz(1,0) at beta=4 versus the P/P ED: |z| > 5
HOLD when any fit used by P1, P2 or N1 has p < 0.001, a non-finite value, s_i <= 0 or a
singular normal matrix, or when either spin file and bins.tsv disagree at q=Q. Points are never
dropped. The thresholds allow for the number of comparisons but, with s_i estimated
from 16 series, are not an exact family-wise error guarantee.
"""
import json
import math
import sys
from pathlib import Path

import run_validation as rv
import validation_io as vio
import run_completion as completion

HERE = Path(__file__).resolve().parent
NSITE = 8
QS = [(mx, my) for my in range(2) for mx in range(4)]  # order of szz_q=all
Q = (2, 1)
P1_LIMIT, P2_LIMIT, N1_LIMIT, P_FIT_MIN = 3.5, 4.0, 5.0, 1e-3
P1_OBS = ['E/N', 'D', 'Szz(Q)', 'Sperp(Q)/2']
N1_OBS = ['D', 'Szz(1,1)', 'Szz(1,0)']


DataError = vio.DataError


def wls_line(x, y, s):
    """Fits y = a + b x with absolute standard errors s; no residual rescaling.

    Returns {'status': 'ok', a, b, C, sigma_a, chi2, p} or {'status': 'hold', reason}.
    p is the upper tail of chi^2 with one degree of freedom (three points)."""
    values = list(x) + list(y) + list(s)
    if (len(x) != 3 or len(y) != 3 or len(s) != 3 or
            not all(math.isfinite(v) for v in values) or any(v <= 0.0 for v in s)):
        return {'status': 'hold', 'reason': 'non-finite input or s <= 0'}
    w = [1.0 / (v * v) for v in s]
    s0 = sum(w)
    s1 = sum(wi * xi for wi, xi in zip(w, x))
    s2 = sum(wi * xi * xi for wi, xi in zip(w, x))
    sy = sum(wi * yi for wi, yi in zip(w, y))
    sxy = sum(wi * xi * yi for wi, xi, yi in zip(w, x, y))
    det = s0 * s2 - s1 * s1
    if not det > 1e-12 * s0 * s2:
        return {'status': 'hold', 'reason': 'singular normal matrix'}
    a = (s2 * sy - s1 * sxy) / det
    b = (s0 * sxy - s1 * sy) / det
    C = [[s2 / det, -s1 / det], [-s1 / det, s0 / det]]
    chi2 = sum(((yi - a - b * xi) / si) ** 2 for xi, yi, si in zip(x, y, s))
    out = {'status': 'ok', 'a': a, 'b': b, 'C': C, 'sigma_a': math.sqrt(C[0][0]),
           'chi2': chi2, 'p': math.erfc(math.sqrt(chi2 / 2.0))}
    if not all(math.isfinite(out[k]) for k in ('a', 'b', 'sigma_a', 'chi2', 'p')):
        return {'status': 'hold', 'reason': 'non-finite fit result'}
    return out


def close(u, v, rel=1e-9, tiny=0.0):
    return abs(u - v) <= max(rel * max(abs(u), abs(v)), tiny)


def self_test():
    import numpy as np
    x = [0.01, 0.0025, 0.000625]
    failures = []

    def check(ok, what):
        print(('ok   ' if ok else 'FAIL ') + what)
        if not ok:
            failures.append(what)

    s = [0.001] * 3
    sx, sxx = sum(x), sum(v * v for v in x)
    sigma_ref = 0.001 * math.sqrt(sxx / (3.0 * sxx - sx * sx))
    f = wls_line(x, [0.25 - 1.5 * v for v in x], s)
    check(f['status'] == 'ok' and close(f['a'], 0.25, 1e-12) and close(f['b'], -1.5, 1e-9),
          'exact line: a and b recovered')
    check(f['chi2'] < 1e-18, 'exact line: chi2 = 0')
    check(close(f['sigma_a'], sigma_ref, 1e-12), 'exact line: sigma_a = s sqrt(Sxx/(3Sxx-Sx^2))')
    check(close(sigma_ref, 8.498e-4, 1e-3), f'sigma_a = {sigma_ref:.4e} (about 8.50e-4)')
    g = wls_line(x, [0.002 + 1e-9, 0.002 - 2e-9, 0.002 + 1e-9], s)
    check(g['status'] == 'ok' and close(g['sigma_a'], sigma_ref, 1e-12),
          'nearly collinear points keep the input-error sigma_a (no rescaling)')
    y3, s3 = [0.002, 0.004, 0.001], [0.001, 0.0015, 0.002]
    h = wls_line(x, y3, s3)
    coef, cov = np.polyfit(x, y3, 1, w=[1.0 / v for v in s3], cov='unscaled')
    check(close(h['a'], coef[1]) and close(h['b'], coef[0]), 'a, b equal polyfit(w=1/s)')
    check(close(h['C'][0][0], cov[1][1]) and close(h['C'][1][1], cov[0][0]) and
          close(h['C'][0][1], cov[0][1]), "C equals polyfit(cov='unscaled')")
    chi2 = sum(((yy - coef[1] - coef[0] * xx) / ss) ** 2 for xx, yy, ss in zip(x, y3, s3))
    check(close(h['chi2'], chi2) and close(h['p'], math.erfc(math.sqrt(chi2 / 2.0))),
          'chi2 and p equal a direct computation')
    check(wls_line(x, y3, [0.001, 0.0, 0.002])['status'] == 'hold', 'hold when s <= 0')
    check(wls_line(x, [0.002, float('nan'), 0.001], s3)['status'] == 'hold', 'hold on NaN')
    check(wls_line([0.01] * 3, y3, s3)['status'] == 'hold', 'hold on a singular normal matrix')
    print('SELF-TEST ' + ('PASSED' if not failures else f'FAILED ({len(failures)})'))
    return 0 if not failures else 1


def ed_reference(path, beta):
    row = vio.read_ed(path)[beta]
    szz = {(e['mx'], e['my']): e['value'] for e in row['Szz']}
    ref = {'E/N': row['E_hub_per_site'], 'D': row['doublon_per_site'],
           'Szz(Q)': szz[Q], 'Sperp(Q)/2': szz[Q]}
    for mx, my in QS:
        ref[f'Szz({mx},{my})'] = ref[f'Sperp({mx},{my})/2'] = szz[(mx, my)]
    return ref


def analyze():
    hop = rv.sha256_file(HERE / 'hopping_app.txt')
    series = {}
    inconsistent = []
    binary_expected = None
    for dtau, beta, _b, r, _base, seed in rv.seed_table():
        d = rv.run_dir(dtau, beta, r)
        if (d / 'input.in').read_text() != rv.expected_input(dtau, beta, seed):
            raise DataError(f'{d}/input.in differs from the pre-registered input')
        recorded = (d / 'hopping_used.sha256').read_text().strip()
        if recorded != hop:
            raise DataError(f'{d}: recorded matrix digest differs from hopping_app.txt')
        if (d / 'hopping_used.txt').exists() and rv.sha256_file(d / 'hopping_used.txt') != recorded:
            raise DataError(f'{d}/hopping_used.txt differs from its recorded digest')
        started = json.loads((d / 'started.json').read_text())
        binary = started['context']['dqmc_sha256']
        vio.require(isinstance(binary, str) and len(binary) == 64 and
                    all(c in '0123456789abcdef' for c in binary), 'invalid binary digest')
        if binary_expected is None:
            binary_expected = binary
        vio.require(binary == binary_expected, 'mixed production binary digests')
        completion.verify(d, completion.context(d, binary), dtau, beta, seed, hop)
        v, mismatches = vio.series_values(d, dtau, beta, seed)
        inconsistent.extend(mismatches)
        series.setdefault((dtau, beta), []).append(v)
    names = list(series[(rv.DTAUS[0], rv.BETAS[0])][0])
    rows, holds = [], list(inconsistent)
    for beta in rv.BETAS:
        ed_ap, ed_pp = ed_reference(HERE / 'ed_app.json', beta), ed_reference(HERE / 'ed_pp.json', beta)
        for name in names:
            y, s = [], []
            for dtau in rv.DTAUS:
                vals = [v[name] for v in series[(dtau, beta)]]
                if len(vals) != rv.NSERIES:
                    raise DataError(f'dtau={dtau:g} beta={beta:g}: {len(vals)} series')
                m = sum(vals) / len(vals)
                var = sum((u - m) ** 2 for u in vals) / (len(vals) - 1)
                y.append(m)
                s.append(math.sqrt(var / len(vals)))
            fit = wls_line([dt * dt for dt in rv.DTAUS], y, s)
            role = []
            if name in P1_OBS:
                role.append('P1')
            if name.startswith(('Szz(', 'Sperp(')) and name not in P1_OBS:
                role.append('P2')
            if beta == 4.0 and name in N1_OBS:
                role.append('N1')
            row = {'beta': beta, 'name': name, 'means': y, 'se': s, 'fit': fit,
                   'ed_ap': ed_ap[name], 'ed_pp': ed_pp[name], 'role': role}
            if fit['status'] == 'ok':
                row['z_ap'] = (fit['a'] - ed_ap[name]) / fit['sigma_a']
                row['z_pp'] = (fit['a'] - ed_pp[name]) / fit['sigma_a']
                vio.number(row['z_ap'], f'beta={beta:g} {name}: z AP/P')
                vio.number(row['z_pp'], f'beta={beta:g} {name}: z P/P')
                if fit['p'] < P_FIT_MIN:
                    holds.append(f'beta={beta:g} {name}: fit p={fit["p"]:.2e}')
            else:
                holds.append(f'beta={beta:g} {name}: {fit["reason"]}')
            rows.append(row)
    p1 = [r for r in rows if 'P1' in r['role']]
    p2 = [r for r in rows if 'P2' in r['role']]
    n1 = [r for r in rows if 'N1' in r['role']]

    def meets(r, test):
        return r['fit']['status'] == 'ok' and test(r)

    p1_pass = sum(meets(r, lambda r: abs(r['z_ap']) < P1_LIMIT) for r in p1)
    p2_pass = sum(meets(r, lambda r: abs(r['z_ap']) < P2_LIMIT) for r in p2)
    n1_pass = sum(meets(r, lambda r: abs(r['z_pp']) > N1_LIMIT) for r in n1)
    if len(p1) != 8 or len(p2) != 32 or len(n1) != 3:
        raise DataError('unexpected number of P1/P2/N1 comparisons')
    if holds:
        verdict = 'HOLD'
    elif p1_pass == 8 and p2_pass == 32 and n1_pass == 3:
        verdict = 'PASS'
    else:
        verdict = 'FAIL'
    return rows, holds, verdict, (p1_pass, p2_pass, n1_pass)


def fmt(v, digits=6):
    return 'n/a' if v is None else f'{v:.{digits}f}'


def report(rows, holds, verdict, counts):
    lines = ['# 4x2 AP/P U=4 validation: analysis', '',
             '96 runs (dtau 0.1, 0.05, 0.025 x beta 2, 4 x 16 independent series), all with the',
             'pre-registered inputs. a and sigma_a: weighted least squares in dtau^2, absolute errors.', '',
             '| beta | observable | role | a | sigma_a | ED AP/P | z AP/P | ED P/P | z P/P | chi2 | p |',
             '| ---: | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    for r in rows:
        f = r['fit']
        good = f['status'] == 'ok'
        lines.append('| {:g} | {} | {} | {} | {} | {} | {} | {} | {} | {} | {} |'.format(
            r['beta'], r['name'], ','.join(r['role']), fmt(f.get('a')), fmt(f.get('sigma_a')),
            fmt(r['ed_ap']), fmt(r.get('z_ap'), 2) if good else 'n/a', fmt(r['ed_pp']),
            fmt(r.get('z_pp'), 2) if good else 'n/a', fmt(f.get('chi2'), 3),
            f"{f['p']:.3g}" if good else f['reason']))
    lines += ['', '| beta | observable | dtau=0.1 | dtau=0.05 | dtau=0.025 |',
              '| ---: | --- | ---: | ---: | ---: |']
    for r in rows:
        if 'P1' in r['role'] or 'N1' in r['role']:
            cells = [f'{m:.6f} ± {e:.6f}' for m, e in zip(r['means'], r['se'])]
            lines.append(f"| {r['beta']:g} | {r['name']} | " + ' | '.join(cells) + ' |')
    p1, p2, n1 = counts
    lines += ['', f'P1 {p1}/8 (|z| < {P1_LIMIT}), P2 {p2}/32 (|z| < {P2_LIMIT}), '
                  f'N1 {n1}/3 (|z P/P| > {N1_LIMIT}).']
    if holds:
        lines += ['', 'Hold reasons:'] + [f'- {h}' for h in holds]
    lines += ['', f'verdict: {verdict}']
    return '\n'.join(lines) + '\n'


def finite_json(value):
    if isinstance(value, float) and not math.isfinite(value):
        return None
    if isinstance(value, dict):
        return {k: finite_json(v) for k, v in value.items()}
    if isinstance(value, list):
        return [finite_json(v) for v in value]
    return value


def failure_report(verdict, error, code):
    reason = str(error).replace(str(HERE), 'data/antiperiodic_4x2_U4_ed')
    text = f'# 4x2 AP/P U=4 validation: {verdict}\n\n{reason}\n\nverdict: {verdict}\n'
    # Replace both artifacts, so a previous PASS cannot masquerade as this run.
    completion.write_record(HERE / 'summary.json', {'verdict': verdict, 'reason': reason})
    completion.atomic_text(HERE / 'analysis_output.md', text)
    print(text, end='')
    return code


def main():
    if sys.argv[1:] == ['--self-test']:
        return self_test()
    if sys.argv[1:]:
        sys.exit(__doc__)
    try:
        rows, holds, verdict, counts = analyze()
    except (vio.NumericHold, ArithmeticError) as exc:
        return failure_report('HOLD', exc, 2)
    except (DataError, OSError, KeyError, ValueError, TypeError, IndexError) as exc:
        return failure_report('INVALID', exc, 3)
    text = report(rows, holds, verdict, counts)
    completion.atomic_text(HERE / 'analysis_output.md', text)
    summary = {'verdict': verdict, 'P1_pass': counts[0], 'P2_pass': counts[1],
               'N1_pass': counts[2], 'holds': holds, 'rows': rows}
    completion.write_record(HERE / 'summary.json', finite_json(summary))
    print(text, end='')
    return {'PASS': 0, 'FAIL': 1, 'HOLD': 2}[verdict]


if __name__ == '__main__':
    sys.exit(main())
