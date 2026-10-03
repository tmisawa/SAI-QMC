---
date: 2026-10-03
datetime: 2026-10-03 14:45 JST
model: OpenAI GPT-6 (Codex)
summary: |
  Four pull-request CI configurations cover compilers, operating systems,
  replica parallelism, historical compatibility and frozen-data analysis.
  Clarified lint platform requirements, download retries and failure evidence retention.
---

# Continuous integration

The workflow in [ci.yml](../.github/workflows/ci.yml) runs when a pull request
to `main` or `develop` is opened, reopened or updated. It has four test jobs:

| Job | Environment | Checks |
| --- | --- | --- |
| `linux-gcc` | Ubuntu 24.04, GCC 13, OpenBLAS pthread, Python 3.10 | Serial suite, two historical comparisons, frozen-data checks and reanalysis |
| `linux-clang` | Ubuntu 24.04, Clang 18, OpenBLAS pthread, Python 3.12 | Workflow/shell lint and serial suite |
| `linux-parallel` | Ubuntu 24.04, GCC 13, Open MPI, OpenBLAS pthread, Python 3.12 | OpenMP, MPI and hybrid suites; 44 short parallel comparisons |
| `macos` | macOS 15 arm64, Apple Clang, Accelerate, libomp, Python 3.12 | Serial/OpenMP suites; 12 short parallel comparisons |

There are no scheduled runs or separate push-triggered runs. Documentation-only
PRs use the same checks. A new update cancels older runs of that PR. Jobs use
read-only repository permissions, and actions are pinned to full commit hashes.

## Parallel coverage and resources

The Linux comparison driver uses OpenMP with 1, 2 and 4 threads; MPI with 1, 2
and 4 ranks; and hybrid rank/thread pairs (1,2), (2,1) and (2,2). macOS uses
1 and 2 OpenMP threads. Probes built with the corresponding compiler flags
check the actual MPI world size and OpenMP team size before solver comparisons.
Each feature covers five replicas, plus idle-thread and empty-rank cases.
The features are AP/P boundaries, P/P polarized global updates, parallel
tempering, and conditional measurements with tempering. Independent ladders,
not slots within a ladder, are distributed in the tempering cases.

All BLAS thread counts are fixed to one. Up to four jobs run concurrently;
each job builds with `make -j2` and executes tests sequentially with `make -j1`.
The existing three-rank/two-thread global-update regression is an intentional,
short oversubscription case. It is a correctness test, not a performance test.
Existing MPI C unit tests still run with one rank: they do not initialize MPI,
and some share fixed temporary filenames. Multiple-rank coverage uses the CLI.

`MPIRUN` accepts one executable path, not a command string with extra arguments.
The CI [launcher](../ci/mpi-launcher.sh) requires Open MPI, allows the existing
oversubscription case and disables process binding. `CI_MPIEXEC` can select an
Open MPI launcher; other MPI implementations remain outside this CI matrix.

## Numerical acceptance

Within one job, serial and parallel outputs are compared exactly, except for
named mode/rank metadata and PT timing values. Existing regression tolerances
remain in their original tests. Across compilers, operating systems and BLAS
implementations, the suites check their own physical identities and numerical
tolerances; CI does not require identical Monte Carlo trajectories across jobs.

The GCC job checks 1,958 manifest entries: 1,174 historical entries, 12 AP/P
4x2 preregistration files, 382 AP/P 4x4 files and 390 P/P 4x4 files. It runs the
11 analysis self-tests and 17 registered analysis regressions, and regenerates
22 numerical/report files from saved observations. It launches no production
QMC runs or ED calculations. Every analysis starts from an independent copy;
the paired comparison sees the original AP/P snapshot even if another Python
version changes floating-point formatting in regenerated output.

Checksums, schemas, identifiers, seeds, counts and verdicts match exactly.
Derived floating-point values use `rtol=1e-10`, `atol=1e-12`; non-finite values
fail. The rounded 4x2 report must also match. The 4x4 integrity checks preserve
the published spin-convergence limitations; a green CI does not establish new
physical convergence.

The GCC job fetches full history for the boundary (`a614a8f`) and tempering
(`3215eee`) compatibility tests. Both old and current builds use the same
compiler and link flags. The older global-default target refers to an object
absent from the public history and is excluded explicitly. Slow tests,
sanitizers, alternative MPI implementations and additional platforms are also
outside this initial PR matrix.

## Reproduce locally

Use a clean checkout with the matching dependencies. Set the compiler and BLAS
link flags explicitly; `PYTHON` selects the interpreter used by the CI driver.
Its executable directory is prepended to PATH so older shell tests also use
the selected interpreter as `python3`.
For example, on macOS with libomp installed:

```sh
CC=clang LIBOMP_PREFIX="$(brew --prefix libomp)" sh ci/run.sh macos
```

On Ubuntu with GCC 13, pthread OpenBLAS and Open MPI installed:

```sh
export CC=gcc-13 OMPI_CC=gcc-13
blas_dir="/usr/lib/$(gcc-13 -print-multiarch)/openblas-pthread"
export LDLIBS="-L$blas_dir -Wl,-rpath,$blas_dir -lopenblas -lm"
python3 -m pip install -r ci/requirements.txt
sh ci/run.sh linux-gcc --output ci-results/gcc
sh ci/run.sh linux-parallel --output ci-results/parallel
```

Use a separate clean checkout for Clang, or clean the build before changing
compiler flags. Results are kept under `ci-results/` and ignored by Git. Supply
a fresh `--output` directory for each run. The standalone data checker and CI
helper regressions can also run without building the solver:

```sh
python3 ci/verify_validation_data.py --output ci-results/data
python3 -m unittest discover -s ci -p 'test_*.py' -v
```

`sh ci/lint.sh` is for Linux x86_64 only: it downloads the Linux amd64
actionlint binary and requires `sha256sum` and ShellCheck.

## Results and required check

Each job records source and PR-head revisions, selected environment versions,
binary hashes/linkage, commands, durations, exit codes and logs. The short
parallel cases also retain inputs, outputs and rank exit codes. Selected legacy
and parallel shell tests preserve temporary outputs on failure when
`CI_FAILURE_DIR` is set, using a separate `<script>.XXXXXX` directory for each
failed invocation. If archiving fails, the original temporary outputs and test
exit code are retained, and their location is reported in the log.
Artifacts are retained for 14 days. Command timeouts
terminate local process groups; failed stochastic tests are not retried with
new seeds. Only the actionlint download in `ci/lint.sh` has explicit retry
handling (`curl --retry 2`) in these CI scripts.

The final check is **`ci-gate`**. It requires all four jobs, the expected stages
and the full case lists to succeed for the same source and PR-head revisions.
Failure, cancellation, skipped/missing jobs and missing cases cannot pass it.
After the first hosted run, a maintainer can select `ci-gate` as the required
status check in branch protection. Adding this workflow does not itself change
repository protection settings.

The initial elapsed-time goal is 15 minutes excluding queueing, to be measured
on hosted runners. Job timeouts are upper bounds (30 minutes for serial Linux,
45 for parallel Linux/macOS), not runtime predictions.
