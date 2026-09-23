#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run_dir=$(mktemp -d /tmp/afqmc-sperp-parallel.XXXXXX)
trap 'rm -rf "$run_dir"' EXIT HUP INT TERM

for mode in serial omp mpi2 mpi4 hybrid; do
    mkdir "$run_dir/$mode"
done

write_input() {
    mode=$1
    parallel=$2
    printf '%s\n' \
        'lattice=chain' 'Lx=4' 'Ly=1' 'pbc=1' 't=-1' 'U=0' \
        'dtau=0.1' 'beta_list=2' 'nwarm=2' 'nmeas=8' 'nbin=2' \
        'stab=2' "parallel=$parallel" 'nrep=3' 'seed=17' \
        'szz_q=all' 'szz_file=szz.dat' \
        'sperp_q=all' 'sperp_file=sperp.dat' \
        'spin_consistency_file=spin_consistency.dat' \
        > "$run_dir/$mode/input.txt"
}

write_input serial serial
write_input omp omp
write_input mpi2 mpi
write_input mpi4 mpi
write_input hybrid hybrid

(cd "$run_dir/serial" && "$repo_dir/dqmc" input.txt > stdout.txt)
(cd "$run_dir/omp" && OMP_NUM_THREADS=2 \
    "$repo_dir/dqmc_omp" input.txt > stdout.txt)
(cd "$run_dir/mpi2" && mpirun -np 2 \
    "$repo_dir/dqmc_mpi" input.txt > stdout.txt)
(cd "$run_dir/mpi4" && mpirun -np 4 \
    "$repo_dir/dqmc_mpi" input.txt > stdout.txt)
(cd "$run_dir/hybrid" && OMP_NUM_THREADS=2 mpirun -np 2 \
    "$repo_dir/dqmc_hybrid" input.txt > stdout.txt)

for mode in serial omp mpi2 mpi4 hybrid; do
    for observable in szz sperp spin_consistency; do
        awk '!/^#/' \
            "$run_dir/$mode/$observable.dat" \
            > "$run_dir/$mode/$observable-data.dat"
        test "$(wc -l < "$run_dir/$mode/$observable-data.dat" | tr -d ' ')" = 4
    done
done

for mode in omp mpi2 mpi4 hybrid; do
    diff -u "$run_dir/serial/szz-data.dat" "$run_dir/$mode/szz-data.dat"
    diff -u "$run_dir/serial/sperp-data.dat" "$run_dir/$mode/sperp-data.dat"
    diff -u "$run_dir/serial/spin_consistency-data.dat" \
        "$run_dir/$mode/spin_consistency-data.dat"
    test "$(tail -n 1 "$run_dir/serial/stdout.txt")" = \
         "$(tail -n 1 "$run_dir/$mode/stdout.txt")"
done

mkdir "$run_dir/different_q"
printf '%s\n' \
    'lattice=chain' 'Lx=4' 'Ly=1' 'pbc=1' 't=-1' 'U=0' \
    'dtau=0.1' 'beta_list=2' 'nwarm=2' 'nmeas=8' 'nbin=2' \
    'stab=2' 'parallel=mpi' 'nrep=3' 'seed=17' \
    'szz_q=3:0,0:0' 'szz_file=szz-selected.dat' \
    'sperp_q=all' 'sperp_file=sperp-all.dat' \
    > "$run_dir/different_q/input.txt"
(cd "$run_dir/different_q" && mpirun -np 4 \
    "$repo_dir/dqmc_mpi" input.txt > stdout.txt)
test "$(awk '!/^#/ {n++} END {print n+0}' \
    "$run_dir/different_q/szz-selected.dat")" = 2
test "$(awk '!/^#/ {n++} END {print n+0}' \
    "$run_dir/different_q/sperp-all.dat")" = 4

echo OK
