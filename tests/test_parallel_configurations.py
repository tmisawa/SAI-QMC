"""Short deterministic CLI comparisons across the PR rank/thread configurations."""
import argparse
import difflib
import json
import math
import os
from pathlib import Path
import re
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "ci"))
from common import run, write_json  # noqa: E402
from config import case_id, parallel_cases  # noqa: E402


def input_text(feature, replicas, mode):
    text = f"""lattice=square
Lx=4
Ly=2
t=-1
U=4
nwarm=10
nmeas=40
nbin=4
stab=4
nrep={replicas}
seed=20261004
parallel={mode}
replica_log=none
replica_bin_file=bins.tsv
szz_q=all
sperp_q=all
spin_consistency_file=spin.dat
"""
    text += "bc_x=antiperiodic\nbc_y=periodic\n" if feature == "app" else "pbc=1\n"
    if feature in ("pt", "conditional"):
        text += "beta_list=0.5,0.75,1\ntempering=dtau_ladder\ntempering_ltr=20\ntempering_interval=1\ntempering_file=pt.tsv\n"
    else:
        text += "dtau=0.1\nbeta_list=1\n"
    if feature == "global":
        text += "global_update=site\nglobal_interval=5\nglobal_site_select=polarized\nglobal_site_power=2\nglobal_site_diag_file=diag.tsv\n"
    if feature == "conditional":
        text += "conditional_measure=1\n"
    return text


def normalized(path):
    """Ignore only named mode/rank metadata and the PT timing field."""
    lines = []
    for line in path.read_text().splitlines():
        if not line.startswith("#") and re.search(r"(?:^|\s)[+-]?(?:nan|inf(?:inity)?)(?:\s|$)", line, re.I):
            raise ValueError("Non-finite solver output")
        if line.startswith("# tempering solver_elapsed_seconds="):
            if not re.fullmatch(r"# tempering solver_elapsed_seconds=\d+\.\d+ nranks=\d+", line):
                raise ValueError("Malformed elapsed-time metadata")
            continue
        if line.startswith("cost\t"):
            fields = line.split("\t")
            seconds = float(fields[4])
            if not math.isfinite(seconds) or seconds < 0:
                raise ValueError("Non-finite/negative PT cost")
            fields[4] = "<seconds>"
            line = "\t".join(fields)
        if line.startswith("#"):
            line = re.sub(r"\bparallel=(serial|omp|mpi|hybrid)\b", "parallel=<mode>", line)
            line = re.sub(r"\bnranks=\d+\b", "nranks=<ranks>", line)
            # Serial/OMP headers omit the MPI rank field altogether.
            line = line.replace(" nranks=<ranks>", "")
        lines.append(line)
    return lines


def compare_files(left, right, output):
    a, b = normalized(left), normalized(right)
    if a != b:
        Path(output).write_text("\n".join(difflib.unified_diff(a, b, fromfile="serial", tofile="parallel")) + "\n")
        raise ValueError(f"Parallel output differs: {right.name}")


def execute(case, folder, env):
    feature, replicas, mode, ranks, threads = case
    folder.mkdir(parents=True)
    (folder / "input.in").write_text(input_text(feature, replicas, mode))
    binary = ROOT / ("dqmc" if mode == "serial" else f"dqmc_{mode}")
    command = [binary, "input.in"]
    if mode in ("mpi", "hybrid"):
        command = [env.get("MPIRUN", "mpirun"), "-n", str(ranks), "sh", ROOT / "ci/rank-exit.sh", *command]
    result = run(command, folder, folder, env=dict(env, OMP_NUM_THREADS=str(threads)))
    if mode in ("mpi", "hybrid"):
        for rank in range(ranks):
            if (folder / f"rank.{rank}.exit").read_text().strip() != "0":
                raise ValueError(f"Failed rank {rank}: {case_id(case)}")
        (folder / "stdout.txt").write_bytes((folder / "rank.0.stdout").read_bytes())
        if f"nranks={ranks}" not in (folder / "stdout.txt").read_text():
            raise ValueError("Solver rank metadata does not match request")
    bins = [line for line in (folder / "bins.tsv").read_text().splitlines() if line and not line.startswith("#")]
    if len(bins) != replicas * 4 * (3 if feature in ("pt", "conditional") else 1):
        raise ValueError("Missing replica/slot/bin rows")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", choices=("linux-parallel", "macos"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cases = parallel_cases(args.profile)
    env = dict(os.environ, OPENBLAS_NUM_THREADS="1", VECLIB_MAXIMUM_THREADS="1",
               MKL_NUM_THREADS="1", BLIS_NUM_THREADS="1", OMP_DYNAMIC="FALSE")
    probes = {}
    completed = []
    baselines = {}
    for case in cases:
        feature, replicas, mode, ranks, threads = case
        key = f"{mode}-r{ranks}-t{threads}"
        if key not in probes:
            probe_dir = output / "probes" / key
            command = [ROOT / f"build/ci/probe_{mode}"]
            if mode in ("mpi", "hybrid"):
                command = [env.get("MPIRUN", "mpirun"), "-n", str(ranks), *command]
            run(command, ROOT, probe_dir, env=dict(env, OMP_NUM_THREADS=str(threads)))
            observed = [json.loads(line) for line in (probe_dir / "stdout.txt").read_text().splitlines()]
            expected = [{"rank": r, "ranks": ranks, "threads": threads} for r in range(ranks)]
            if sorted(observed, key=lambda row: row["rank"]) != expected:
                raise ValueError(f"Actual team differs from requested configuration: {key}")
            probes[key] = observed
        base_key = (feature, replicas)
        if base_key not in baselines:
            baseline = output / f"{feature}-n{replicas}-serial"
            execute((feature, replicas, "serial", 1, 1), baseline, env)
            baselines[base_key] = baseline
        identifier = case_id(case)
        folder = output / identifier
        result = execute(case, folder, env)
        files = ["stdout.txt", "observables.dat", "bins.tsv", "szz.dat", "sperp.dat", "spin.dat", "hopping_used.txt"]
        if feature in ("pt", "conditional"):
            files.append("pt.tsv")
        if feature == "global":
            files.append("diag.tsv")
        for name in files:
            compare_files(baselines[base_key] / name, folder / name, folder / f"{name}.diff")
        completed.append(identifier)
        write_json(output / "results.json", {"profile": args.profile, "completed": completed, "probes": probes})
        print(f"PASS {identifier} ({result['elapsed_seconds']:.2f}s)", flush=True)
    if completed != [case_id(case) for case in cases]:
        raise ValueError("Missing planned parallel case")


if __name__ == "__main__":
    main()
