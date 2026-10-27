#!/bin/sh
# DQMC runs for the ED comparison of the sign-reweighting implementation.
# Fixed before any result was seen (2026-10-27): nwarm 1000, nmeas 20000,
# nbin 20, nrep 8, dtau 0.05 and 0.025, seeds below; parallel=omp with 8
# threads. usage: run_ed_compare.sh DQMC_OMP_BINARY OUTDIR
set -eu
bin="$1"; out="$2"; here="$(cd "$(dirname "$0")" && pwd)"
mkdir -p "$out"
common='nwarm=1000
nmeas=20000
nbin=20
nrep=8
parallel=omp
sign_file=sign.dat
replica_bin_file=bins.tsv'
run() { # name dtau seed body
  d="$out/$1"; mkdir -p "$d"
  printf '%s\ndtau=%s\nseed=%s\nstab=10\n%s\n' "$4" "$2" "$3" "$common" > "$d/input.in"
  (cd "$d" && /usr/bin/time -p env OMP_NUM_THREADS=8 "$bin" input.in > stdout.txt 2> stderr.txt)
}
for dt in 0.05 0.025; do
  for bc in periodic antiperiodic; do
    run "L4_${bc}_dt$dt" $dt 2026102711 "lattice=chain
Lx=4
bc_x=$bc
t=-1
U=4
mu=0
beta_list=1,2,4,10
szz_q=all"
    for mu in 2 1; do
      run "sq4x2_${bc}_mu${mu}_dt$dt" $dt 2026102712 "lattice=square
Lx=4
Ly=2
bc_x=$bc
bc_y=periodic
t=-1
tprime=-0.2
U=4
mu=$mu
beta_list=1,2,4
szz_q=all"
    done
  done
done
