#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run_dir=$(mktemp -d /tmp/sai-qmc-dat-output.XXXXXX)
trap 'rm -rf "$run_dir"' EXIT HUP INT TERM
mode=${1:-serial}

cat > "$run_dir/input.txt" <<EOF
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=0.4,0.8
nwarm=2
nmeas=8
nbin=2
nrep=2
seed=17
parallel=$mode
green_rebuild=centered
stab=2
szz_q=all
sperp_q=all
spin_consistency_file=spin_consistency.dat
profile=1
EOF

if test "$mode" = serial; then
    printf '%s\n' 'stab_drift_file=drift.dat' 'udv_scale_file=scales.dat' \
        'udv_centered_file=centered.dat' >> "$run_dir/input.txt"
fi

replica_columns=8
profile_columns=14
case "$mode" in
    serial) (cd "$run_dir" && "$repo_dir/dqmc" input.txt > stdout.txt) ;;
    omp) (cd "$run_dir" && OMP_NUM_THREADS=2 \
        "$repo_dir/dqmc_omp" input.txt > stdout.txt) ;;
    mpi|hybrid)
        replica_columns=9
        profile_columns=15
        (cd "$run_dir" && OMP_NUM_THREADS=2 "${MPIRUN:-mpirun}" -np 2 \
            "$repo_dir/dqmc_$mode" input.txt > stdout.txt) ;;
    *) echo "unknown mode: $mode" >&2; exit 1 ;;
esac

# All tables have one commented column header and space-separated data rows.
check_table() {
    awk -v columns="$2" -v first="$3" -v expected="$4" '
        BEGIN { headers=0; rows=0; ok=1 }
        /^#/ {
            if ($2 == first) {
                headers++
                if (NF != columns+1) ok=0
            }
            next
        }
        NF {
            rows++
            if (NF != columns || index($0, "\t") || index($0, ",")) ok=0
        }
        END { exit !(ok && headers == 1 && rows > 0 &&
                     (expected < 0 || rows == expected)) }
    ' "$run_dir/$1"
}

cmp "$run_dir/observables.dat" "$run_dir/stdout.txt"
check_table observables.dat 14 T 2
for observable in szz sperp spin_consistency; do
    check_table "$observable.dat" 12 beta_requested 8
done
check_table replicas.dat "$replica_columns" beta 4
check_table profile.dat "$profile_columns" beta -1
if test "$mode" = serial; then
    check_table drift.dat 14 beta_index 4
    check_table scales.dat 25 beta_index -1
    check_table centered.dat 18 beta_index -1
fi
for old_output in "$run_dir"/*.csv "$run_dir"/*.tsv; do
    test ! -e "$old_output"
done
test -s "$run_dir/hopping_used.txt"
echo "OK: .dat tables ($mode)"
