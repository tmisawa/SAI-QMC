#!/bin/sh
# Production binaries must ignore every failure-injection environment variable.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
bin="${1:-$root/dqmc}"
mode="${2:-serial}"
MPIRUN="${MPIRUN:-mpirun}"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/input.in" <<INPUT
lattice=chain
Lx=4
pbc=1
U=4
dtau=0.1
beta_list=1
nwarm=1
nmeas=4
nbin=2
nrep=3
parallel=$mode
seed=81
global_update=site
global_interval=1
replica_bin_file=bins.tsv
INPUT
run() {
  case "$mode" in
    mpi|hybrid) "$MPIRUN" -n 3 "$bin" input.in ;;
    *) "$bin" input.in ;;
  esac
}
(cd "$tmp" && OMP_NUM_THREADS=2 run > reference.out)
cp "$tmp/bins.tsv" "$tmp/reference.bins"
for hook in AFQMC_TEST_GLOBAL_FAIL_AT AFQMC_TEST_GLOBAL_FAIL_BETA AFQMC_TEST_BIN_WRITE_FAIL AFQMC_TEST_BIN_CLOSE_FAIL; do
  (
    cd "$tmp"
    unset AFQMC_TEST_GLOBAL_FAIL_AT AFQMC_TEST_GLOBAL_FAIL_BETA AFQMC_TEST_BIN_WRITE_FAIL AFQMC_TEST_BIN_CLOSE_FAIL
    export OMP_NUM_THREADS=2
    case "$hook" in
      AFQMC_TEST_GLOBAL_FAIL_AT) export AFQMC_TEST_GLOBAL_FAIL_AT=2 ;;
      AFQMC_TEST_GLOBAL_FAIL_BETA) export AFQMC_TEST_GLOBAL_FAIL_AT=2 AFQMC_TEST_GLOBAL_FAIL_BETA=0 ;;
      *) export "$hook=1" ;;
    esac
    run > actual.out
  )
  cmp "$tmp/reference.out" "$tmp/actual.out"
  cmp "$tmp/reference.bins" "$tmp/bins.tsv"
done
printf 'OK production hook isolation (%s)\n' "$mode"
