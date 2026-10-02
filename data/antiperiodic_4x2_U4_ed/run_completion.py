"""Atomic completion records for the pre-registered runs (standard library only).

Numerical HOLD results are complete observations, not a reason to rerun a seed.
Only missing or structurally invalid artifacts trigger an archived retry.
"""
import hashlib
import json
import os
from pathlib import Path

import validation_io as vio

OUTPUTS = ('exit_code.txt', 'bins.tsv', 'szz.dat', 'sperp.dat',
           'observables.dat', 'stdout.txt', 'stderr.txt', 'hopping_used.sha256')


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def atomic_text(path, text):
    path = Path(path)
    temporary = path.with_name('.'+path.name+'.tmp')
    try:
        with temporary.open('w') as f:
            f.write(text)
            f.flush()
            os.fsync(f.fileno())
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def write_record(path, data):
    atomic_text(path, json.dumps(data, indent=2, allow_nan=False)+'\n')


def context(directory, binary_digest):
    return dict(input_sha256=sha256(directory/'input.in'), dqmc_sha256=binary_digest)


def bound_context(directory, expected):
    for name in ('started.json', 'complete.json'):
        path = directory/name
        if path.exists():
            try:
                old = json.loads(path.read_text())
            except (ValueError, OSError) as exc:
                raise vio.DataError(directory.name+': unreadable '+name) from exc
            vio.require(isinstance(old, dict) and old.get('context') == expected,
                        directory.name+': binary/input mismatch in '+name)


def check_outputs(directory, dtau, beta, seed, matrix_digest):
    d = directory
    for name in OUTPUTS:
        vio.require((d/name).is_file(), d.name+': missing '+name)
        if name != 'stderr.txt':
            vio.require((d/name).stat().st_size > 0, d.name+': empty '+name)
    vio.require((d/'hopping_used.sha256').read_text().strip() == matrix_digest,
                d.name+': wrong matrix digest')
    if (d/'hopping_used.txt').exists():
        vio.require(sha256(d/'hopping_used.txt') == matrix_digest, d.name+': wrong actual matrix')
    try:
        _, holds = vio.series_values(d, dtau, beta, seed)
    except vio.NumericHold as exc:
        holds = [str(exc)]
    return holds


def finish(directory, expected, dtau, beta, seed, matrix_digest):
    holds = check_outputs(directory, dtau, beta, seed, matrix_digest)
    bound_context(directory, expected)
    record = dict(context=expected, outputs={name: sha256(directory/name) for name in OUTPUTS},
                  numerical_holds=holds)
    write_record(directory/'complete.json', record)


def verify(directory, expected, dtau, beta, seed, matrix_digest):
    bound_context(directory, expected)
    for name in ('started.json', 'complete.json'):
        vio.require((directory/name).is_file(), directory.name+': missing '+name)
    record = json.loads((directory/'complete.json').read_text())
    vio.require(record.get('outputs') == {name: sha256(directory/name) for name in OUTPUTS},
                directory.name+': completed output checksums differ')
    check_outputs(directory, dtau, beta, seed, matrix_digest)


def archive_attempt(directory):
    """Preserve every previous artifact; keep only the fixed input in place."""
    children = [p for p in directory.iterdir() if p.name not in ('input.in', 'attempts')]
    if not children:
        return
    archive = directory/'attempts'
    archive.mkdir(exist_ok=True)
    n = 1
    while (archive/f'{n:04d}').exists():
        n += 1
    destination = archive/f'{n:04d}'
    destination.mkdir()
    for path in children:
        path.rename(destination/path.name)


def prepare(directory, expected, dtau, beta, seed, matrix_digest):
    """Return skip/recovered/run. Mismatched provenance is an explicit error."""
    bound_context(directory, expected)
    if (directory/'complete.json').exists():
        try:
            recovered_digest = False
            raw, digest = directory/'hopping_used.txt', directory/'hopping_used.sha256'
            if not digest.exists() and raw.is_file() and sha256(raw) == matrix_digest:
                atomic_text(digest, sha256(raw)+'\n')
                recovered_digest = True
            verify(directory, expected, dtau, beta, seed, matrix_digest)
            return 'recovered' if recovered_digest else 'skip'
        except (vio.DataError, OSError):
            archive_attempt(directory)
            return 'run'
    # Interrupted after successful solver exit, before the final atomic marker.
    if (directory/'started.json').is_file():
        try:
            raw = directory/'hopping_used.txt'
            digest = directory/'hopping_used.sha256'
            if not digest.exists():
                vio.require(raw.is_file() and sha256(raw) == matrix_digest,
                            directory.name+': cannot recover matrix digest')
                atomic_text(digest, sha256(raw)+'\n')
            finish(directory, expected, dtau, beta, seed, matrix_digest)
            return 'recovered'
        except (vio.DataError, OSError):
            pass
    archive_attempt(directory)
    return 'run'
