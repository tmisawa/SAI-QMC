#!/bin/sh
# serial / OMP / MPI / hybrid tempering runs agree: bins.tsv, tempering_file (except the
# non-deterministic cost rows) and the stdout data rows.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
MPIRUN="${MPIRUN:-mpirun}"
write_input() { # parallel
  cat > "$tmp/input.in" <<EOT
lattice=chain
Lx=4
pbc=1
t=-1.0
U=4
beta_list=0.5,0.75,1.0
tempering=dtau_ladder
tempering_ltr=20
tempering_interval=1
tempering_file=pt.tsv
nwarm=20
nmeas=40
nbin=4
stab=4
nrep=4
seed=4242
replica_bin_file=bins.tsv
parallel=$1
EOT
}
save() { # mode
  cp "$tmp/bins.tsv" "$tmp/bins.$1"
  grep -v '^cost' "$tmp/pt.tsv" > "$tmp/pt.$1"
  grep -v '^#' "$tmp/out.$1" > "$tmp/row.$1"
}
fail=0
write_input serial; (cd "$tmp" && "$root/dqmc" input.in > out.serial); save serial
write_input omp; (cd "$tmp" && OMP_NUM_THREADS=2 "$root/dqmc_omp" input.in > out.omp); save omp
write_input mpi; (cd "$tmp" && "$MPIRUN" -n 2 "$root/dqmc_mpi" input.in > out.mpi); save mpi
write_input hybrid; (cd "$tmp" && OMP_NUM_THREADS=2 "$MPIRUN" -n 2 "$root/dqmc_hybrid" input.in > out.hybrid); save hybrid
[ "$(grep -c . "$tmp/row.serial")" -eq 3 ] || { echo "FAIL serial stdout rows"; fail=1; }
for mode in omp mpi hybrid; do
  cmp -s "$tmp/bins.serial" "$tmp/bins.$mode" || { echo "FAIL bins $mode"; fail=1; }
  cmp -s "$tmp/pt.serial" "$tmp/pt.$mode" || { echo "FAIL tempering_file $mode"; fail=1; }
  cmp -s "$tmp/row.serial" "$tmp/row.$mode" || { echo "FAIL stdout rows $mode"; fail=1; }
  tail -1 "$tmp/out.$mode" | grep -q '^# tempering solver_elapsed_seconds=' || { echo "FAIL solver_elapsed $mode"; fail=1; }
done
grep -q 'parallel=mpi .*nranks=2' "$tmp/out.mpi" || { echo "FAIL mpi header"; fail=1; }
tail -1 "$tmp/out.mpi" | grep -q ' nranks=2$' || { echo "FAIL mpi solver_elapsed nranks"; fail=1; }
[ "$fail" -eq 0 ] && echo OK
exit "$fail"
