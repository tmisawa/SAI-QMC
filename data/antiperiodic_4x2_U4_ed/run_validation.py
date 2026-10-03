#!/usr/bin/env python3
"""Pre-registered 4x2 AP/P U=4 validation runs (design of 2026-10-02, section 7).

Run from this directory after building ../../dqmc. Subcommands, in order:
  seeds     write seeds.tsv (96 chain seeds) and check that they are distinct
  smoke     check this file's replica_seed against the C program via bins.tsv seeds
  matrices  write hopping_app.txt (AP/P) and hopping_pp.txt (P/P) from short runs
  inputs    write runs/dt<dtau>/b<beta>/r<series>/input.in from seeds.tsv
  run       verify input/binary/output bindings before skipping completed runs;
            recover interrupted markers or archive incomplete attempts and retry
            the same input, at most 4 runs at a time
Every series is one nrep=1 run with a single beta, so the chain seed equals the
input seed (replica_seed(seed, 0, 0) == seed). analyze_antiperiodic_ed.py checks
each input against expected_input(). hopping_used.txt is git-ignored, so the
recorded digest is what remains for the matrix check after a fresh checkout.
"""
import argparse
import concurrent.futures
import csv
import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import validation_io as vio
import run_completion as completion

HERE = Path(__file__).resolve().parent
DQMC = HERE.parents[1] / 'dqmc'
BETAS = (2.0, 4.0)
DTAUS = (0.1, 0.05, 0.025)
BASES = {0.1: 2026100201, 0.05: 2026100202, 0.025: 2026100203}
NSERIES = 16
WORKERS = 4
LATTICE = ['lattice=square', 'Lx=4', 'Ly=2', 'bc_x=antiperiodic', 'bc_y=periodic',
           't=-1.0', 'U=4']
SETTINGS = ['nwarm=2000', 'nmeas=40000', 'nbin=20', 'stab=4', 'nrep=1',
            'parallel=serial', 'tempering=none', 'global_update=none',
            'conditional_measure=0', 'field_init=random', 'sweep_order=forward',
            'green_rebuild=combine', 'szz_q=all', 'szz_file=szz.dat', 'sperp_q=all',
            'sperp_file=sperp.dat', 'replica_bin_file=bins.tsv',
            'output_file=observables.dat']
MASK64 = (1 << 64) - 1


def splitmix64_value(x):
    x = (x + 0x9E3779B97F4A7C15) & MASK64
    x = ((x ^ (x >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
    x = ((x ^ (x >> 27)) * 0x94D049BB133111EB) & MASK64
    return (x ^ (x >> 31)) & MASK64


def replica_seed(base_seed, beta_index, replica_id):
    """Bit-exact port of replica_seed() in src/replica.c."""
    base_seed &= MASK64
    legacy = (base_seed + 1000 * beta_index) & MASK64
    if replica_id == 0:
        return legacy
    x = base_seed
    x ^= (0xD1B54A32D192ED03 * ((beta_index + 1) & 0xFFFFFFFF)) & MASK64
    x ^= (0xABC98388FB8FAC03 * ((replica_id + 1) & 0xFFFFFFFF)) & MASK64
    mixed = splitmix64_value(x)
    if mixed == legacy:
        mixed = splitmix64_value(mixed)
    return mixed


def seed_table():
    """(dtau, beta, beta_index, series, base, seed) for all 96 series."""
    return [(dtau, beta, b, r, BASES[dtau], replica_seed(BASES[dtau], b, r))
            for dtau in DTAUS for b, beta in enumerate(BETAS) for r in range(NSERIES)]


def run_dir(dtau, beta, series):
    return HERE / 'runs' / f'dt{dtau:g}' / f'b{beta:g}' / f'r{series:02d}'


def expected_input(dtau, beta, seed):
    return '\n'.join(LATTICE + [f'dtau={dtau:g}', f'beta_list={beta:g}', f'seed={seed}'] +
                     SETTINGS) + '\n'


def run_dqmc(directory, dqmc):
    """Runs dqmc in directory with BLAS on one thread at nice 10; returns the exit code."""
    env = dict(os.environ, OMP_NUM_THREADS='1', VECLIB_MAXIMUM_THREADS='1',
               OPENBLAS_NUM_THREADS='1', MKL_NUM_THREADS='1')
    with open(directory / 'stdout.txt', 'w') as out, open(directory / 'stderr.txt', 'w') as err:
        rc = subprocess.call(['nice', '-n', '10', str(dqmc), 'input.in'], cwd=directory,
                             stdout=out, stderr=err, env=env)
    completion.atomic_text(directory / 'exit_code.txt', f'{rc}\n')
    return rc


def cmd_seeds(_args):
    rows = seed_table()
    seeds = [row[5] for row in rows]
    if len(set(seeds)) != len(seeds):
        sys.exit('FAIL: chain seeds are not distinct')
    with open(HERE / 'seeds.tsv', 'w', newline='') as f:
        w = csv.writer(f, delimiter='\t', lineterminator='\n')
        w.writerow(['dtau', 'beta', 'beta_index', 'series', 'base', 'seed'])
        for dtau, beta, b, r, base, seed in rows:
            w.writerow([f'{dtau:g}', f'{beta:g}', b, r, base, seed])
    print(f'seeds.tsv: {len(rows)} distinct chain seeds')


def cmd_smoke(args):
    bad = 0
    for dtau in DTAUS:
        d = HERE / 'smoke' / f'dt{dtau:g}'
        d.mkdir(parents=True, exist_ok=True)
        text = '\n'.join(LATTICE + [f'dtau={dtau:g}', 'beta_list=2,4', f'seed={BASES[dtau]}',
                                    'nwarm=0', 'nmeas=2', 'nbin=2', 'nrep=16',
                                    'parallel=serial', 'replica_bin_file=bins.tsv']) + '\n'
        (d / 'input.in').write_text(text)
        if run_dqmc(d, args.dqmc) != 0:
            sys.exit(f'FAIL: smoke run for dtau={dtau:g} exited nonzero')
        with open(d / 'bins.tsv') as f:
            rows = [line.split('\t') for line in f if not line.startswith('#')]
        got = {(int(r[0]), int(r[4])): int(r[5]) for r in rows}
        want = {(b, r): replica_seed(BASES[dtau], b, r)
                for b in range(len(BETAS)) for r in range(NSERIES)}
        if got != want:
            bad += 1
            print(f'FAIL: dtau={dtau:g} seeds differ from the C program')
    if bad:
        sys.exit(1)
    print('smoke: replica_seed matches the C program for all 3 bases x 2 betas x 16 series')


def sha256_file(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def cmd_matrices(args):
    """The reference matrices come from the program itself, before any production run."""
    short = ['t=-1.0', 'U=4', 'dtau=0.1', 'beta_list=2', 'nwarm=0', 'nmeas=2', 'nbin=2',
             'seed=1']
    for name, lattice in (('app', LATTICE[:5]),
                          ('pp', ['lattice=square', 'Lx=4', 'Ly=2', 'pbc=1'])):
        d = HERE / f'matrix_{name}'
        d.mkdir(exist_ok=True)
        (d / 'input.in').write_text('\n'.join(lattice + short) + '\n')
        if run_dqmc(d, args.dqmc) != 0:
            sys.exit(f'FAIL: {name} matrix run exited nonzero')
        (HERE / f'hopping_{name}.txt').write_bytes((d / 'hopping_used.txt').read_bytes())
    print('hopping_app.txt and hopping_pp.txt written from short runs')


def cmd_inputs(_args):
    count = 0
    with open(HERE / 'seeds.tsv') as f:
        for row in csv.DictReader(f, delimiter='\t'):
            dtau, beta, series = float(row['dtau']), float(row['beta']), int(row['series'])
            seed = int(row['seed'])
            if seed != replica_seed(BASES[dtau], int(row['beta_index']), series):
                sys.exit(f'FAIL: seeds.tsv row {row} does not match replica_seed')
            d = run_dir(dtau, beta, series)
            text = expected_input(dtau, beta, seed)
            if (d / 'input.in').exists():
                if (d / 'input.in').read_text() != text:
                    sys.exit(f'FAIL: {d}/input.in exists with different content')
            else:
                d.mkdir(parents=True, exist_ok=True)
                (d / 'input.in').write_text(text)
            count += 1
    print(f'inputs: {count} run directories ready')


def cmd_run(args):
    digest = sha256_file(args.dqmc)
    matrix_digest = sha256_file(HERE / 'hopping_app.txt')
    todo, recovered, skipped = [], [], []
    for dtau, beta, _b, r, _base, seed in seed_table():
        d = run_dir(dtau, beta, r)
        vio.require((d/'input.in').read_text() == expected_input(dtau, beta, seed),
                    str(d.relative_to(HERE))+': input differs from registration')
        expected = completion.context(d, digest)
        action = completion.prepare(d, expected, dtau, beta, seed, matrix_digest)
        if action == 'run':
            todo.append((d, expected, dtau, beta, seed))
        elif action == 'recovered':
            recovered.append(str(d.relative_to(HERE)))
        else:
            skipped.append(str(d.relative_to(HERE)))

    def one(item):
        d, expected, dtau, beta, seed = item
        try:
            completion.write_record(d/'started.json', {'context': expected})
            rc = run_dqmc(d, args.dqmc)
            if rc == 0:
                actual = sha256_file(d/'hopping_used.txt')
                vio.require(actual == matrix_digest, d.name+': actual matrix differs')
                completion.atomic_text(d/'hopping_used.sha256', actual+'\n')
                completion.finish(d, expected, dtau, beta, seed, matrix_digest)
            return rc
        except (OSError, ValueError) as exc:
            completion.atomic_text(d/'driver_error.txt',
                                   str(exc).replace(str(HERE), 'data/antiperiodic_4x2_U4_ed')+'\n')
            return 1

    start = time.strftime('%Y-%m-%dT%H:%M:%S%z')
    t0 = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(max_workers=WORKERS) as pool:
        codes = list(pool.map(one, todo))
    elapsed = time.monotonic() - t0
    failed = [str(item[0].relative_to(HERE)) for item, c in zip(todo, codes) if c != 0]
    with open(HERE/'run_log.jsonl', 'a') as f:
        f.write(json.dumps(dict(start=start, runs=len(todo), failed=failed,
                                recovered=recovered, skipped=skipped,
                                elapsed_seconds=round(elapsed, 1), workers=WORKERS,
                                dqmc_sha256=digest))+'\n')
    print(f'run: {len(todo)} runs, {len(failed)} failed, {elapsed:.0f} s; '
          f'{len(recovered)} recovered, {len(skipped)} verified skips')
    if failed:
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('command', choices=['seeds', 'smoke', 'matrices', 'inputs', 'run'])
    parser.add_argument('--dqmc', type=Path, default=DQMC, help='dqmc binary (default ../../dqmc)')
    args = parser.parse_args()
    args.dqmc = args.dqmc.resolve()
    try:
        dict(seeds=cmd_seeds, smoke=cmd_smoke, matrices=cmd_matrices, inputs=cmd_inputs,
             run=cmd_run)[args.command](args)
    except (vio.DataError, OSError) as exc:
        parser.exit(1, 'ERROR: '+str(exc).replace(str(HERE), 'data/antiperiodic_4x2_U4_ed')+'\n')


if __name__ == '__main__':
    main()
