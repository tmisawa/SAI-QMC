#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run_dir=$(mktemp -d /tmp/sai-qmc-scalar-output.XXXXXX)
trap 'rm -rf "$run_dir"' EXIT HUP INT TERM
mode=${1:-serial}

run_qmc() {
    case "$mode" in
        serial) "$repo_dir/dqmc" input.txt ;;
        omp) OMP_NUM_THREADS=2 "$repo_dir/dqmc_omp" input.txt ;;
        mpi) "${MPIRUN:-mpirun}" -np 2 "$repo_dir/dqmc_mpi" input.txt ;;
        hybrid) OMP_NUM_THREADS=2 "${MPIRUN:-mpirun}" -np 2 \
            "$repo_dir/dqmc_hybrid" input.txt ;;
        *) echo "unknown mode: $mode" >&2; exit 1 ;;
    esac
}

cat > "$run_dir/base.txt" <<EOF
lattice=chain
Lx=4
U=4
dtau=0.1
beta_list=0.4,0.8
nwarm=2
nmeas=8
nbin=2
nrep=3
replica_log=none
parallel=$mode
seed=17
EOF

case_dir() {
    mkdir "$run_dir/$1"
    cp "$run_dir/base.txt" "$run_dir/$1/input.txt"
}

case_dir default
(cd "$run_dir/default" && run_qmc > stdout.txt)
cmp "$run_dir/default/observables.dat" "$run_dir/default/stdout.txt"
awk '!/^#/ {if (NF != 14) exit 1; n++} END {if (n != 2) exit 1}' \
    "$run_dir/default/observables.dat"

case_dir custom
printf '%s\n' 'output_file=results.dat' >> "$run_dir/custom/input.txt"
(cd "$run_dir/custom" && run_qmc > stdout.txt)
cmp "$run_dir/default/observables.dat" "$run_dir/custom/results.dat"
test ! -e "$run_dir/custom/observables.dat"

case_dir disabled
printf '%s\n' 'output_file=none' >> "$run_dir/disabled/input.txt"
(cd "$run_dir/disabled" && run_qmc > stdout.txt)
cmp "$run_dir/default/stdout.txt" "$run_dir/disabled/stdout.txt"
test ! -e "$run_dir/disabled/observables.dat"

# Direct redirection to the automatic destination must not duplicate rows.
# MPI launchers relay stdout through pipes, so use stdout-only mode there.
case "$mode" in
    serial|omp)
        case_dir redirected
        (cd "$run_dir/redirected" && run_qmc > observables.dat)
        cmp "$run_dir/default/stdout.txt" "$run_dir/redirected/observables.dat"
        ;;
esac

case_dir input_collision
printf '%s\n' 'output_file=./input.txt' >> "$run_dir/input_collision/input.txt"
cp "$run_dir/input_collision/input.txt" "$run_dir/input_collision/before.txt"
if (cd "$run_dir/input_collision" && run_qmc > stdout.txt 2> error.txt); then
    echo 'input/output collision unexpectedly succeeded' >&2; exit 1
fi
cmp "$run_dir/input_collision/before.txt" "$run_dir/input_collision/input.txt"
grep -q 'output_file.*collides with input' "$run_dir/input_collision/error.txt"
test ! -e "$run_dir/input_collision/hopping_used.txt"

case_dir output_collision
printf '%s\n' 'output_file=./szz.dat' 'szz_q=all' \
    >> "$run_dir/output_collision/input.txt"
if (cd "$run_dir/output_collision" && run_qmc > stdout.txt 2> error.txt); then
    echo 'spin/scalar collision unexpectedly succeeded' >&2; exit 1
fi
grep -q 'output_file.*collides with szz_file' "$run_dir/output_collision/error.txt"
test ! -e "$run_dir/output_collision/szz.dat"

case_dir hardlink
printf '%s\n' 'output_file=alias.dat' >> "$run_dir/hardlink/input.txt"
ln "$run_dir/hardlink/input.txt" "$run_dir/hardlink/alias.dat"
cp "$run_dir/hardlink/input.txt" "$run_dir/hardlink/before.txt"
if (cd "$run_dir/hardlink" && run_qmc > stdout.txt 2> error.txt); then
    echo 'hard-linked input/output collision unexpectedly succeeded' >&2; exit 1
fi
cmp "$run_dir/hardlink/before.txt" "$run_dir/hardlink/input.txt"

case_dir open_failure
printf '%s\n' 'output_file=missing/results.dat' >> "$run_dir/open_failure/input.txt"
if (cd "$run_dir/open_failure" && run_qmc > stdout.txt 2> error.txt); then
    echo 'unwritable scalar destination unexpectedly succeeded' >&2; exit 1
fi
grep -q 'failed to open output_file' "$run_dir/open_failure/error.txt"

# Linux provides a sink that opens successfully but fails when flushed.
if test -c /dev/full; then
    case_dir write_failure
    printf '%s\n' 'output_file=/dev/full' >> "$run_dir/write_failure/input.txt"
    if (cd "$run_dir/write_failure" && run_qmc > stdout.txt 2> error.txt); then
        echo 'scalar write failure unexpectedly succeeded' >&2; exit 1
    fi
    grep -q 'failed to write scalar output' "$run_dir/write_failure/error.txt"
fi

echo "OK ($mode)"
