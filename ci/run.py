"""Execute one required CI configuration, with build/test stages kept separate."""
import argparse
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import sys
import time

from common import run, sha256, write_json
from config import CONFIGS, case_id, parallel_cases

ROOT = Path(__file__).resolve().parents[1]


def commands(profile, output, env):
    make = ["make", f"CC={env.get('CC', 'cc')}", f"PYTHON={sys.executable}"]
    for key in ("CFLAGS", "LDLIBS", "LIBOMP_PREFIX", "MPICC", "MPIRUN"):
        if key in env:
            make.append(f"{key}={env[key]}")
    build = [*make, "-j2"]
    test = [*make, "-j1"]
    return {
        "environment": [sys.executable, "ci/environment.py"],
        "ci-regressions": [sys.executable, "-m", "unittest", "discover", "-s", "ci", "-p", "test_*.py", "-v"],
        "build-serial": [*build, "test_build"],
        "build-macos": [*build, "test_build", "test_build_omp", "build/ci/probe_omp"],
        "build-parallel": [*build, "dqmc", "test_build_omp", "test_build_mpi", "test_build_hybrid",
                           "build/ci/probe_omp", "build/ci/probe_mpi", "build/ci/probe_hybrid"],
        "test-serial": [*test, "test"],
        "test-omp": [*test, "test_omp"],
        "test-mpi": [*test, "test_mpi"],
        "test-hybrid": [*test, "test_hybrid"],
        "legacy-tempering": [*test, "test_tempering_default"],
        "legacy-boundary": [*test, "test_bc_default"],
        "saved-data": [sys.executable, "ci/verify_validation_data.py", "--output", str(output / "saved-data")],
        "parallel-configurations": [sys.executable, "tests/test_parallel_configurations.py", "--profile", profile,
                                    "--output", str(output / "parallel")],
    }


def binary_evidence(output, env):
    evidence = {}
    for name in ("dqmc", "dqmc_omp", "dqmc_mpi", "dqmc_hybrid"):
        binary = ROOT / name
        if not binary.exists():
            continue
        command = ["otool", "-L", str(binary)] if platform.system() == "Darwin" else ["ldd", str(binary)]
        run(command, ROOT, output / "linkage" / name, env=env)
        linkage = (output / "linkage" / name / "stdout.txt").read_text()
        resolved = [str(Path(name).resolve()) for name in re.findall(r"=> (\S+)", linkage)]
        if platform.system() == "Linux" and not any("openblas-pthread" in name for name in resolved):
            raise ValueError(f"Expected the pthread OpenBLAS backend: {name}")
        if platform.system() == "Darwin" and "Accelerate.framework" not in linkage:
            raise ValueError(f"Expected Accelerate: {name}")
        evidence[name] = sha256(binary)
    write_json(output / "binaries.json", evidence)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", choices=CONFIGS)
    parser.add_argument("--output", type=Path, default=ROOT / "ci-results")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output / "result.json").exists():
        parser.error("Use a fresh output directory; previous results are retained")
    source = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    env = dict(os.environ, CI_PROFILE=args.profile, OMP_NUM_THREADS="2", OMP_DYNAMIC="FALSE",
               OPENBLAS_NUM_THREADS="1", VECLIB_MAXIMUM_THREADS="1", MKL_NUM_THREADS="1", BLIS_NUM_THREADS="1",
               PYTHONDONTWRITEBYTECODE="1", CI_FAILURE_DIR=str(output / "failed-tests"))
    env["PATH"] = str(Path(sys.executable).parent) + os.pathsep + env.get("PATH", "")
    if args.profile == "linux-parallel":
        env["MPIRUN"] = str(ROOT / "ci/mpi-launcher.sh")
    # Make command-line variables also reach the historical sub-makes.
    steps = commands(args.profile, output, env)
    result = {"config": args.profile, "source_sha": source,
              "pr_head_sha": os.environ.get("PR_HEAD_SHA", source),
              "status": "failure", "stages": [], "parallel_cases": []}
    start = time.monotonic()
    try:
        for stage in CONFIGS[args.profile]:
            print(f"START {args.profile}: {stage}", flush=True)
            item = {"id": stage, "status": "failure"}
            result["stages"].append(item)
            run(steps[stage], ROOT, output / stage, timeout=1200, env=env)
            if stage.startswith("build-"):
                binary_evidence(output, env)
            if stage == "parallel-configurations":
                completed = json.loads((output / "parallel/results.json").read_text())["completed"]
                if completed != [case_id(case) for case in parallel_cases(args.profile)]:
                    raise ValueError("Missing parallel configurations")
                result["parallel_cases"] = completed
            item["status"] = "pass"
            print(f"PASS {args.profile}: {stage}", flush=True)
        result["status"] = "pass"
    except (Exception, KeyboardInterrupt) as exc:
        result["error"] = str(exc)
        print(f"FAIL: {exc}", file=sys.stderr)
    finally:
        result["elapsed_seconds"] = time.monotonic() - start
        write_json(output / "result.json", result)
    return 0 if result["status"] == "pass" else 1


if __name__ == "__main__":
    sys.exit(main())
