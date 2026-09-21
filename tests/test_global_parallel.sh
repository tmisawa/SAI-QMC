#!/bin/sh
# spec 7.10: serial / OMP / MPI / hybrid agree for global_update=site.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
MPIRUN="${MPIRUN:-mpirun}"
write_input() { # parallel nrep
  cat > "$tmp/input.in" <<EOF
lattice=square
Lx=2
Ly=4
pbc=1
t=-1.0
U=4
dtau=0.1
beta_list=1
nwarm=10
nmeas=40
nbin=4
stab=4
nrep=$2
seed=77
parallel=$1
sweep_order=alternating
green_rebuild=combine
szz_q=all
szz_file=szz.tsv
sperp_q=all
sperp_file=sperp.tsv
global_update=site
global_interval=5
replica_bin_file=bins.tsv
EOF
}
data() { grep -v '^#' "$1"; }
fail=0
for nrep in 5 2; do   # 5: uneven split over 3 ranks; 2: nrep < nranks
  write_input serial "$nrep"; (cd "$tmp" && "$root/dqmc" input.in > out.serial)
  data "$tmp/bins.tsv" > "$tmp/bins.serial"; data "$tmp/out.serial" > "$tmp/row.serial"
  write_input omp "$nrep"; (cd "$tmp" && OMP_NUM_THREADS=2 "$root/dqmc_omp" input.in > out.omp)
  data "$tmp/bins.tsv" | cmp -s - "$tmp/bins.serial" || { echo "FAIL bins omp nrep=$nrep"; fail=1; }
  write_input mpi "$nrep"; (cd "$tmp" && "$MPIRUN" -n 3 "$root/dqmc_mpi" input.in > out.mpi)
  cp "$tmp/bins.tsv" "$tmp/bins.mpi.full"
  write_input hybrid "$nrep"; (cd "$tmp" && OMP_NUM_THREADS=2 "$MPIRUN" -n 3 "$root/dqmc_hybrid" input.in > out.hybrid)
  for mode in omp mpi hybrid; do
    data "$tmp/out.$mode" > "$tmp/row.$mode"
    cmp -s "$tmp/row.serial" "$tmp/row.$mode" || { echo "FAIL stdout row $mode nrep=$nrep"; fail=1; }
  done
  data "$tmp/bins.mpi.full" | cmp -s - "$tmp/bins.serial" || { echo "FAIL bins mpi nrep=$nrep"; fail=1; }
  data "$tmp/bins.tsv" | cmp -s - "$tmp/bins.serial" || { echo "FAIL bins hybrid nrep=$nrep"; fail=1; }
  grep -q 'parallel=mpi .*nranks=3' "$tmp/out.mpi" || { echo "FAIL mpi header"; fail=1; }
  grep -q 'parallel=hybrid .*nranks=3' "$tmp/out.hybrid" || { echo "FAIL hybrid header"; fail=1; }
done
# Record each rank's exit before returning success to the launcher, so the first
# failed rank cannot cause mpirun to kill the remaining ranks before observation.
cat > "$tmp/rank_exit.sh" <<'EOF'
#!/bin/sh
rank="${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-${PMIX_RANK:-}}}"
[ -n "$rank" ] || exit 2
rc=0
"$@" > "rank.$rank.out" 2> "rank.$rank.err" || rc=$?
printf '%s\n' "$rc" > "rank.$rank.exit"
exit 0
EOF
# failure propagation: every rank must exit nonzero, none may hang in a collective (I6)
expect_fail() { # description command...   (runs in $tmp; 60 s limit)
  what="$1"; shift
  rm -f "$tmp"/rank.*.exit
  python3 - "$tmp" "$what" "$@" <<'PY' || fail=1
import os, signal, subprocess, sys
from pathlib import Path
work, what, command = Path(sys.argv[1]), sys.argv[2], sys.argv[3:]
p = subprocess.Popen(command, cwd=work, start_new_session=True,
                     stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
try:
    rc = p.wait(timeout=60)
except subprocess.TimeoutExpired:
    os.killpg(p.pid, signal.SIGKILL)
    p.wait()
    raise SystemExit("FAIL collective hung: " + what)
if rc != 0:
    raise SystemExit("FAIL rank-exit wrapper/launcher: " + what)
codes = [(work / f"rank.{r}.exit").read_text().strip() if (work / f"rank.{r}.exit").exists() else "missing" for r in range(3)]
if codes != ["1"] * 3:
    raise SystemExit(f"FAIL rank exit codes {codes}: {what}")
if what.startswith("numerical"):
    errors = "\n".join((work / f"rank.{r}.err").read_text() for r in range(3))
    for replica in range(5):
        marker = f"TEST_GLOBAL_FAIL beta_index=0 replica_id={replica} sweep=15 pass_rc=1 status=1"
        if marker not in errors:
            raise SystemExit("FAIL numerical hook not exercised: " + marker)
PY
}
for mode in mpi hybrid; do
  bin="$root/dqmc_$mode"
  write_input "$mode" 5
  sed -i.bak 's#^replica_bin_file=.*#replica_bin_file=/nonexistent_dir/bins.tsv#' "$tmp/input.in"
  expect_fail "open ($mode)" env OMP_NUM_THREADS=2 "$MPIRUN" -n 3 sh "$tmp/rank_exit.sh" "$bin" input.in
  write_input "$mode" 5
  expect_fail "write ($mode)" env OMP_NUM_THREADS=2 AFQMC_TEST_BIN_WRITE_FAIL=1 "$MPIRUN" -n 3 sh "$tmp/rank_exit.sh" "$bin" input.in
  expect_fail "close ($mode)" env OMP_NUM_THREADS=2 AFQMC_TEST_BIN_CLOSE_FAIL=1 "$MPIRUN" -n 3 sh "$tmp/rank_exit.sh" "$bin" input.in
  expect_fail "numerical ($mode)" env OMP_NUM_THREADS=2 AFQMC_TEST_GLOBAL_FAIL_AT=15 "$MPIRUN" -n 3 sh "$tmp/rank_exit.sh" "$bin" input.in
done
[ "$fail" -eq 0 ] && echo OK || exit 1
