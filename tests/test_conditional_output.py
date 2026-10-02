"""CLI compatibility, conditional bin validation, and cross-mode equality."""
import argparse
import math
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
BASE = """lattice=chain
Lx=4
pbc=1
U=8
nwarm=20
nmeas=80
nbin=4
stab=4
nrep=3
seed=2026093007
green_rebuild=centered
replica_bin_file=bins.tsv
szz_q=all
sperp_q=all
spin_consistency_file=spin.dat
"""


def rows(path):
    return [line for line in path.read_text().splitlines() if line and not line.startswith("#")]


def run(folder, text, mode="serial", binary=None):
    folder.mkdir()
    (folder / "input.in").write_text(text + f"parallel={mode}\n")
    command = [str(binary or ROOT / ("dqmc" if mode == "serial" else f"dqmc_{mode}")), "input.in"]
    if mode in ("mpi", "hybrid"):
        command = shlex.split(os.environ.get("MPIRUN", "mpirun")) + ["-n", "2"] + command
    env = dict(os.environ, OMP_NUM_THREADS="2", VECLIB_MAXIMUM_THREADS="1", OPENBLAS_NUM_THREADS="1")
    result = subprocess.run(command, cwd=folder, env=env, capture_output=True, text=True)
    assert result.returncode == 0, (command, result.stderr)
    (folder / "stdout.txt").write_text(result.stdout)
    return folder


def close_rows(a, b, integers=()):
    assert len(a) == len(b)
    for x, y in zip(a, b):
        xx, yy = x.split(), y.split()
        assert len(xx) == len(yy)
        for i, (v, w) in enumerate(zip(xx, yy)):
            if i in integers:
                assert v == w, (i, v, w)
            else:
                assert math.isclose(float(v), float(w), abs_tol=1e-12, rel_tol=1e-12), (i, v, w)


def same_legacy(a, b, cross_mode=False):
    for name in ["observables.dat", "szz.dat", "sperp.dat", "spin.dat"]:
        if cross_mode:
            close_rows(rows(a / name), rows(b / name))
        else:
            assert rows(a / name) == rows(b / name), name
    old, new = rows(a / "bins.tsv"), rows(b / "bins.tsv")
    assert len(old) == len(new)
    if cross_mode:
        close_rows(["\t".join(r.split("\t")[:20]) for r in old],
                   ["\t".join(r.split("\t")[:20]) for r in new],
                   integers=(0, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15, 16))
    else:
        assert all(x.split("\t")[:20] == y.split("\t")[:20] for x, y in zip(old, new))
    if (a / "pt.tsv").exists():
        assert [r for r in rows(a / "pt.tsv") if not r.startswith("cost")] == [
            r for r in rows(b / "pt.tsv") if not r.startswith("cost")]


def validate(folder):
    lines = (folder / "bins.tsv").read_text().splitlines()
    header = next(r.removeprefix("# columns: ") for r in lines if r.startswith("# columns:"))
    assert header.split("\t")[-4:] == ["conditional_count", "sum_D_cond", "sum_K_cond", "sum_Ehub_cond"]
    for line in rows(folder / "bins.tsv"):
        r = dict(zip(header.split("\t"), line.split("\t"), strict=True))
        assert int(r["conditional_count"]) == int(r["count"]) == 20
        d, k, e = [float(r[q]) for q in ["sum_D_cond", "sum_K_cond", "sum_Ehub_cond"]]
        assert all(math.isfinite(x) for x in [d, k, e])
        assert math.isclose(e, k + 32 * d, abs_tol=1e-11, rel_tol=1e-12)
        dt = float(r["beta_effective"]) / int(r["Ltr"])
        assert -0.5 / math.expm1(4 * dt) - 1e-10 <= d / 20 <= 0.5 / (math.exp(4 * dt) + 1) + 1e-10


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--parallel", action="store_true")
    parser.add_argument("--baseline", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="qmc-conditional-") as tmp:
        root = Path(tmp)
        for pt in [False, True]:
            for alternating in [False, True]:
                key = f"{'pt' if pt else 'fixed'}-{'alt' if alternating else 'forward'}"
                text = BASE + ("sweep_order=alternating\nglobal_update=site\nglobal_interval=3\n" if alternating else "sweep_order=forward\n")
                text += ("beta_list=0.5,0.75,1\ntempering=dtau_ladder\ntempering_ltr=20\ntempering_file=pt.tsv\n" if pt else "beta_list=0.5,1\ndtau=0.05\n")
                off = run(root / (key + "-off"), text)
                zero = run(root / (key + "-zero"), text + "conditional_measure=0\n")
                assert (off / "bins.tsv").read_bytes() == (zero / "bins.tsv").read_bytes()
                on = run(root / (key + "-on"), text + "conditional_measure=1\n")
                same_legacy(off, on)
                validate(on)
                command = [sys.executable, str(ROOT / "scripts/analyze_conditional_bins.py"),
                           str(on / "bins.tsv"), "--expect-replicas", "3",
                           "--expect-slots", "3" if pt else "2"]
                analyzed = subprocess.run(command, capture_output=True, text=True)
                assert analyzed.returncode == 0, analyzed.stderr
                stats = json.loads(analyzed.stdout)
                assert stats["independent_replicas"] == 3 and stats["all_bins_retained"]
                # A missing final bin must be rejected rather than silently pooled.
                bad = on / "missing-bin.tsv"
                bad.write_text("\n".join((on / "bins.tsv").read_text().splitlines()[:-1]) + "\n")
                command[2] = str(bad)
                assert subprocess.run(command, capture_output=True).returncode != 0
                for column_key, value in [("conditional_count", "19"), ("sum_D_cond", "nan"), ("sum_sign_Szz_Q", "nan")]:
                    lines = (on / "bins.tsv").read_text().splitlines()
                    columns = next(r.removeprefix("# columns: ").split() for r in lines if r.startswith("# columns:"))
                    at = next(i for i, r in enumerate(lines) if r and not r.startswith("#"))
                    row = lines[at].split("\t")
                    row[columns.index(column_key)] = value
                    lines[at] = "\t".join(row)
                    bad.write_text("\n".join(lines) + "\n")
                    assert subprocess.run(command, capture_output=True).returncode != 0
                if args.baseline:
                    baseline = run(root / (key + "-baseline"), text, binary=args.baseline.resolve())
                    same_legacy(baseline, off)
                    assert (baseline / "bins.tsv").read_bytes() == (off / "bins.tsv").read_bytes()
                if args.parallel and alternating:
                    for mode in ["omp", "mpi", "hybrid"]:
                        other = run(root / (key + "-" + mode), text + "conditional_measure=1\n", mode)
                        mode_off = run(root / (key + "-" + mode + "-off"), text, mode)
                        same_legacy(mode_off, other)
                        same_legacy(on, other, cross_mode=True)
                        close_rows(rows(on / "bins.tsv"), rows(other / "bins.tsv"),
                                   integers=(0, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15, 16, 20))
                print(key, "OK", flush=True)
        if args.parallel:
            empty_text = text.replace("nrep=3\n", "nrep=1\n") + "conditional_measure=1\n"
            one = run(root / "one-ladder-serial", empty_text)
            empty_rank = run(root / "one-ladder-mpi", empty_text, "mpi")
            same_legacy(one, empty_rank, cross_mode=True)
            close_rows(rows(one / "bins.tsv"), rows(empty_rank / "bins.tsv"),
                       integers=(0, 3, 4, 5, 6, 7, 8, 9, 10, 13, 14, 15, 16, 20))
            validate(empty_rank)
        for index, extra in enumerate(["conditional_measure=2\n", "conditional_measure=-1\n", "conditional_measure=nan\n", "conditional_measure=1\nreplica_bin_file=\n", "conditional_measure=1\nreplica_bin_file=none\n"]):
            folder = root / f"invalid-{index}"
            folder.mkdir()
            (folder / "input.in").write_text(BASE + "dtau=0.1\nbeta_list=1\n" + extra)
            result = subprocess.run([str(ROOT / "dqmc"), "input.in"], cwd=folder, capture_output=True)
            assert result.returncode != 0, extra
        # No bin output at all must be rejected too.
        folder = root / "missing-bin"
        folder.mkdir()
        (folder / "input.in").write_text(BASE.replace("replica_bin_file=bins.tsv\n", "") + "conditional_measure=1\n")
        assert subprocess.run([str(ROOT / "dqmc"), "input.in"], cwd=folder, capture_output=True).returncode != 0
    print("CONDITIONAL OUTPUT CHECKS PASSED")


if __name__ == "__main__":
    main()
