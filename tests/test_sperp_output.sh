#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run_dir=$(mktemp -d /tmp/afqmc-sperp-output.XXXXXX)
trap 'rm -rf "$run_dir"' EXIT HUP INT TERM

mkdir "$run_dir/szz_only" "$run_dir/both" "$run_dir/disabled"

(cd "$run_dir/disabled" &&
    "$repo_dir/dqmc" "$repo_dir/input/1d_L4_U0.txt" > stdout.txt)
test ! -e "$run_dir/disabled/sperp.dat"

for mode in szz_only both; do
    input="$run_dir/$mode/input.txt"
    printf '%s\n' \
        'lattice=chain' 'Lx=4' 'Ly=1' 'pbc=1' 't=-1' 'U=0' \
        'dtau=0.1' 'beta_list=2.0' 'nwarm=2' 'nmeas=8' 'nbin=2' \
        'stab=2' 'szz_q=all' 'szz_file=szz.dat' 'seed=9' \
        > "$input"
done
printf '%s\n' \
    'sperp_q=all' 'sperp_file=sperp.dat' \
    'spin_consistency_file=spin_consistency.dat' \
    'profile=1' 'profile_file=profile.dat' \
    >> "$run_dir/both/input.txt"

(cd "$run_dir/szz_only" && "$repo_dir/dqmc" input.txt > stdout.txt)
(cd "$run_dir/both" && "$repo_dir/dqmc" input.txt > stdout.txt)

cmp "$run_dir/szz_only/szz.dat" "$run_dir/both/szz.dat"
grep -v '^#' "$run_dir/szz_only/stdout.txt" > "$run_dir/szz_only/data.txt"
grep -v '^#' "$run_dir/both/stdout.txt" > "$run_dir/both/data.txt"
cmp "$run_dir/szz_only/data.txt" "$run_dir/both/data.txt"

grep -q ' measurement measure_spin ' "$run_dir/both/profile.dat"
if grep -q ' measurement measure_szz ' "$run_dir/both/profile.dat" ||
   grep -q ' measurement measure_sperp ' "$run_dir/both/profile.dat"; then
    echo 'joint spin measurement was attributed to a component profiler region' >&2
    exit 1
fi

expected_sperp_header='# beta_requested beta T q_index mx my qx_over_pi qy_over_pi qx_folded_over_pi qy_folded_over_pi Sperp dSperp'
actual_sperp_header=$(grep '^# beta_requested ' "$run_dir/both/sperp.dat")
test "$actual_sperp_header" = "$(printf '%b' "$expected_sperp_header")"

grep -v '^#' "$run_dir/both/szz.dat" > "$run_dir/szz_rows.dat"
grep -v '^#' "$run_dir/both/sperp.dat" > "$run_dir/sperp_rows.dat"
paste "$run_dir/szz_rows.dat" "$run_dir/sperp_rows.dat" | awk '
    BEGIN { rows=0; ok=1 }
    {
        rows++
        if (NF != 24 || $4 != $16 || $5 != $17 || $6 != $18) ok=0
        if ($23 ~ /[Nn][Aa][Nn]|[Ii][Nn][Ff]/ ||
            $24 ~ /[Nn][Aa][Nn]|[Ii][Nn][Ff]/) ok=0
        if (($23 - 2*$11) < -1e-12 || ($23 - 2*$11) > 1e-12) ok=0
    }
    END { exit !(ok && rows == 4) }
'

expected_consistency_header='# beta_requested beta T q_index mx my qx_over_pi qy_over_pi qx_folded_over_pi qy_folded_over_pi DeltaSU2 dDeltaSU2'
actual_consistency_header=$(grep '^# beta_requested ' "$run_dir/both/spin_consistency.dat")
test "$actual_consistency_header" = "$(printf '%b' "$expected_consistency_header")"
awk '
    BEGIN { rows=0; ok=1 }
    !/^#/ {
        rows++
        if ($11 < -1e-12 || $11 > 1e-12 || $12 < 0 || $12 > 1e-12) ok=0
    }
    END { exit !(ok && rows == 4) }
' "$run_dir/both/spin_consistency.dat"

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' \
    'sperp_q=all' 'sperp_file=none' > "$run_dir/bad_file.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_file.txt >/dev/null 2>&1); then
    echo 'sperp_file=none unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' \
    'szz_q=all' 'sperp_q=all' 'szz_file=same.dat' \
    'sperp_file=same.dat' > "$run_dir/bad_collision.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_collision.txt >/dev/null 2>&1); then
    echo 'colliding spin output files unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' \
    'szz_q=all' 'spin_consistency_file=check.dat' \
    > "$run_dir/bad_consistency.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_consistency.txt >/dev/null 2>&1); then
    echo 'unpaired spin consistency unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' \
    'szz_q=0:0,1:0' 'sperp_q=1:0,0:0' \
    'spin_consistency_file=check.dat' \
    > "$run_dir/bad_q_order.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_q_order.txt >/dev/null 2>&1); then
    echo 'mismatched q ordering for consistency unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' \
    'szz_q=all' 'sperp_q=all' 'szz_file=szz.dat' \
    'sperp_file=sperp.dat' 'spin_consistency_file=szz.dat' \
    > "$run_dir/bad_consistency_collision.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_consistency_collision.txt \
        >/dev/null 2>&1); then
    echo 'colliding consistency output unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' \
    'profile=1' 'sperp_q=all' 'sperp_file=profile.dat' \
    > "$run_dir/bad_profile_collision.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_profile_collision.txt \
        >/dev/null 2>&1); then
    echo 'colliding default profile output unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' 'nrep=2' \
    'sperp_q=all' 'sperp_file=replicas.dat' \
    > "$run_dir/bad_replica_collision.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_replica_collision.txt \
        >/dev/null 2>&1); then
    echo 'colliding default replica log unexpectedly succeeded' >&2
    exit 1
fi

echo OK
