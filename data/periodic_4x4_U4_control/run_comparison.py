#!/usr/bin/env python3
"""Matched P/P control of the frozen AP/P calculation. Never overwrite an existing run."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

HERE = Path(__file__).resolve().parent
BINARY = HERE.parents[1] / 'dqmc'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def cases():
    for beta in (4, 8, 16):
        for dtau in ('0.1', '0.05', '0.025'):
            for series in range(4):
                # Deliberately retain the AP/P seed recipe for matched-seed pairs.
                key = f'app-4x4-U4-20261003-b{beta}-dt{dtau}-r{series}'
                seed = int.from_bytes(hashlib.sha256(key.encode()).digest()[:8], 'big')
                name = f'b{beta}/dt{dtau}/r{series:02d}'
                yield dict(name=name, beta=beta, dtau=dtau, series=series, seed=seed)


def input_text(case):
    return '\n'.join([
        'lattice=square', 'Lx=4', 'Ly=4', 'bc_x=periodic', 'bc_y=periodic',
        't=-1', 'U=4', f'dtau={case["dtau"]}', f'beta_list={case["beta"]}',
        f'seed={case["seed"]}', 'nwarm=5000', 'nmeas=20000', 'nbin=20',
        'stab=4', 'nrep=1', 'parallel=serial', 'tempering=none',
        'global_update=none', 'conditional_measure=0', 'field_init=random',
        'sweep_order=forward', 'green_rebuild=combine', 'szz_q=all', 'sperp_q=all',
        'szz_file=szz.dat', 'sperp_file=sperp.dat', 'replica_bin_file=bins.tsv',
        'output_file=observables.dat', ''])


def prepare():
    manifest = HERE / 'registration.json'
    if manifest.exists():
        raise SystemExit('registration already exists; preserve it')
    rows = list(cases())
    assert len({c['seed'] for c in rows}) == 36
    for c in rows:
        d = HERE / 'runs' / c['name']
        d.mkdir(parents=True, exist_ok=False)
        (d/'input.in').write_text(input_text(c))
        c['input_sha256'] = sha(d/'input.in')
    record = dict(created=time.strftime('%Y-%m-%dT%H:%M:%S%z'),
                  source_commit='4384c37c1b9740016bdeea1bdd8f6b48078b051e',
                  source_tree='7a626a411ce0c302d6008540928e2961a1da724c',
                  binary_sha256=sha(BINARY), cases=rows, workers=4,
                  purpose='P/P control with identical AP/P inputs except for bc_x',
                  analysis='Equal-weight independent seed means and SE; preserve paired spin covariance. '
                  'Fit each beta with a+b*dtau^2 and absolute errors; compare beta 8 and 16. '
                  'Also report pooled-bin SE and reblocking diagnostics. No data exclusion or tuning to ED.',
                  limitations='Finite-temperature grand-canonical QMC is not canonical ground-state ED. '
                  'Four seeds give limited precision of the uncertainty estimate. '
                  'No claim of formal validation from a short exploratory calculation.')
    app = HERE.parent / 'antiperiodic_4x4_U4_ed'
    record['app_manifest_sha256'] = sha(app/'SHA256SUMS')
    record['app_registration_sha256'] = sha(app/'registration.json')
    for c in rows:
        baseline = (app/'runs'/c['name']/'input.in').read_text()
        assert input_text(c) == baseline.replace('bc_x=antiperiodic', 'bc_x=periodic')
    record['control_analysis'] = ('Preserve the AP/P analysis and all observations. Compare each BC to its own ED. '
        'Use matched-seed differences, including covariance, for AP/P minus P/P and ED-adjusted residuals. '
        'Report both seed SE and pooled/reblocked-bin diagnostics for both boundaries without choosing a favorable method. '
        'No simulation extension or physical acceptance threshold chosen from control outcomes.')
    manifest.write_text(json.dumps(record, indent=2)+'\n')
    print('Prepared 36 fixed inputs', flush=True)


def run():
    record = json.loads((HERE/'registration.json').read_text())
    assert sha(BINARY) == record['binary_sha256']
    env = dict(os.environ, OMP_NUM_THREADS='1', VECLIB_MAXIMUM_THREADS='1',
               OPENBLAS_NUM_THREADS='1', MKL_NUM_THREADS='1')

    def one(c):
        d = HERE/'runs'/c['name']
        assert sha(d/'input.in') == c['input_sha256']
        if (d/'started.json').exists():
            raise RuntimeError('run already started: '+c['name'])
        start = time.time()
        (d/'started.json').write_text(json.dumps(dict(started=time.strftime('%Y-%m-%dT%H:%M:%S%z'),
            input_sha256=c['input_sha256'], binary_sha256=record['binary_sha256']), indent=2)+'\n')
        with (d/'stdout.txt').open('w') as out, (d/'stderr.txt').open('w') as err:
            rc = subprocess.call(['nice', '-n', '10', str(BINARY), 'input.in'],
                                 cwd=d, env=env, stdout=out, stderr=err)
        result = dict(case=c['name'], exit_code=rc, elapsed_seconds=time.time()-start,
                      finished=time.strftime('%Y-%m-%dT%H:%M:%S%z'))
        result['outputs'] = {p.name: sha(p) for p in sorted(d.iterdir()) if p.is_file()
                             and p.name not in ('input.in', 'started.json')}
        (d/'completed.json').write_text(json.dumps(result, indent=2)+'\n')
        print(json.dumps(result | {'outputs': len(result['outputs'])}), flush=True)
        return rc

    start = time.time()
    with concurrent.futures.ThreadPoolExecutor(max_workers=record['workers']) as pool:
        codes = list(pool.map(one, record['cases']))
    summary = dict(runs=len(codes), failures=sum(c != 0 for c in codes),
                   elapsed_seconds=time.time()-start)
    (HERE/'run_summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print(json.dumps(summary), flush=True)
    return bool(summary['failures'])


if __name__ == '__main__':
    if sys.argv[1:] == ['prepare']:
        prepare()
    elif sys.argv[1:] == ['run']:
        sys.exit(run())
    else:
        raise SystemExit('usage: run_comparison.py prepare|run')
