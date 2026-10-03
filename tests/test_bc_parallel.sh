#!/bin/sh
# An antiperiodic U=4 run agrees across serial / OpenMP / MPI / hybrid builds:
# the whole replica-bin file and the Szz, Sperp and stdout data rows.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
# shellcheck source=tests/test_cleanup.sh
. "$root/tests/test_cleanup.sh"
trap 'test_cleanup "$tmp"' EXIT
MPIRUN="${MPIRUN:-mpirun}"
write_input() { # parallel
  cat > "$tmp/input.in" <<EOT
lattice=square
Lx=4
Ly=2
bc_x=antiperiodic
t=-1.0
U=4
dtau=0.1
beta_list=1
nwarm=20
nmeas=40
nbin=4
stab=4
nrep=4
seed=20261004
replica_bin_file=bins.tsv
szz_q=all
sperp_q=all
parallel=$1
EOT
}
save() { # mode
  cp "$tmp/bins.tsv" "$tmp/bins.$1"
  grep -v '^#' "$tmp/szz.dat" > "$tmp/szz.$1"
  grep -v '^#' "$tmp/sperp.dat" > "$tmp/sperp.$1"
  grep -v '^#' "$tmp/out.$1" > "$tmp/row.$1"
}
fail=0
write_input serial; (cd "$tmp" && "$root/dqmc" input.in > out.serial); save serial
write_input omp; (cd "$tmp" && OMP_NUM_THREADS=2 "$root/dqmc_omp" input.in > out.omp); save omp
write_input mpi; (cd "$tmp" && "$MPIRUN" -n 2 "$root/dqmc_mpi" input.in > out.mpi); save mpi
write_input hybrid; (cd "$tmp" && OMP_NUM_THREADS=2 "$MPIRUN" -n 2 "$root/dqmc_hybrid" input.in > out.hybrid); save hybrid
[ "$(grep -c . "$tmp/row.serial")" -eq 1 ] || { echo "FAIL serial stdout rows"; fail=1; }
[ "$(grep -c . "$tmp/szz.serial")" -eq 8 ] || { echo "FAIL serial Szz rows"; fail=1; }
grep -q '^# lattice=square Lx=4 Ly=2 n=8 bc_x=antiperiodic bc_y=periodic ' "$tmp/bins.serial" ||
  { echo "FAIL bins boundary header"; fail=1; }
for mode in omp mpi hybrid; do
  for kind in bins szz sperp row; do
    cmp -s "$tmp/$kind.serial" "$tmp/$kind.$mode" || { echo "FAIL $kind $mode"; fail=1; }
  done
done
[ "$fail" -eq 0 ] && echo OK
exit "$fail"
