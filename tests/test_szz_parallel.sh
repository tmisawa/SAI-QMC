#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run_dir=$(mktemp -d /tmp/afqmc-szz-parallel.XXXXXX)
trap 'rm -rf "$run_dir"' EXIT HUP INT TERM

for mode in omp mpi2 mpi4 hybrid; do
    mkdir "$run_dir/$mode"
done

(cd "$run_dir/omp" && OMP_NUM_THREADS=2 \
    "$repo_dir/dqmc_omp" \
    "$repo_dir/input/1d_L4_U0_szz_all_omp.txt" > stdout.txt)
(cd "$run_dir/mpi2" && mpirun -np 2 \
    "$repo_dir/dqmc_mpi" \
    "$repo_dir/input/1d_L4_U0_szz_all_mpi.txt" > stdout.txt)
(cd "$run_dir/mpi4" && mpirun -np 4 \
    "$repo_dir/dqmc_mpi" \
    "$repo_dir/input/1d_L4_U0_szz_all_mpi.txt" > stdout.txt)
(cd "$run_dir/hybrid" && OMP_NUM_THREADS=2 mpirun -np 2 \
    "$repo_dir/dqmc_hybrid" \
    "$repo_dir/input/1d_L4_U0_szz_all_hybrid.txt" > stdout.txt)

for mode in omp mpi2 mpi4 hybrid; do
    awk '!/^#/' \
        "$run_dir/$mode/szz.dat" > "$run_dir/$mode/data.dat"
    test "$(wc -l < "$run_dir/$mode/data.dat" | tr -d ' ')" = 4
done

diff -u "$run_dir/omp/data.dat" "$run_dir/mpi2/data.dat"
diff -u "$run_dir/mpi2/data.dat" "$run_dir/mpi4/data.dat"
diff -u "$run_dir/mpi2/data.dat" "$run_dir/hybrid/data.dat"
test "$(tail -n 1 "$run_dir/omp/stdout.txt")" = \
     "$(tail -n 1 "$run_dir/mpi2/stdout.txt")"
test "$(tail -n 1 "$run_dir/mpi2/stdout.txt")" = \
     "$(tail -n 1 "$run_dir/mpi4/stdout.txt")"
test "$(tail -n 1 "$run_dir/mpi2/stdout.txt")" = \
     "$(tail -n 1 "$run_dir/hybrid/stdout.txt")"

mkdir "$run_dir/selected"
printf '%s\n' \
    'lattice=chain' 'Lx=4' 'Ly=1' 'pbc=1' 't=-1' 'U=0' \
    'dtau=0.1' 'beta_list=2' 'nwarm=2' 'nmeas=8' 'nbin=2' \
    'stab=2' 'parallel=mpi' 'nrep=3' 'szz_q=3:0,0:0' \
    'szz_file=selected.dat' 'seed=17' > "$run_dir/selected/input.txt"
(cd "$run_dir/selected" && mpirun -np 4 \
    "$repo_dir/dqmc_mpi" input.txt > stdout.txt)
test "$(awk '!/^#/ {n++} END {print n+0}' \
    "$run_dir/selected/selected.dat")" = 2

echo OK
