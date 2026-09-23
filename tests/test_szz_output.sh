#!/bin/sh
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
run_dir=$(mktemp -d /tmp/afqmc-szz-output.XXXXXX)
trap 'rm -rf "$run_dir"' EXIT HUP INT TERM

disabled_dir="$run_dir/disabled"
enabled_dir="$run_dir/enabled"
selected_dir="$run_dir/selected"
mkdir "$disabled_dir" "$enabled_dir" "$selected_dir"

(cd "$disabled_dir" &&
    "$repo_dir/dqmc" "$repo_dir/input/1d_L4_U0.txt" > stdout.txt)
test ! -e "$disabled_dir/szz.dat"

(cd "$enabled_dir" &&
    "$repo_dir/dqmc" "$repo_dir/input/1d_L4_U0_szz_all.txt" > stdout.txt)
test -s "$enabled_dir/szz.dat"

expected_header='# beta_requested beta T q_index mx my qx_over_pi qy_over_pi qx_folded_over_pi qy_folded_over_pi Szz dSzz'
actual_header=$(grep '^# beta_requested ' "$enabled_dir/szz.dat")
test "$actual_header" = "$(printf '%b' "$expected_header")"

awk '
    BEGIN { rows=0; ok=1 }
    !/^#/ {
        rows++
        if (NF != 12 || $4 != rows-1) ok=0
        if ($2 * $3 < 1-1e-14 || $2 * $3 > 1+1e-14) ok=0
        if ($11 ~ /[Nn][Aa][Nn]|[Ii][Nn][Ff]/ ||
            $12 ~ /[Nn][Aa][Nn]|[Ii][Nn][Ff]/) ok=0
        if ($5 == 3 && ($9 < -0.50000000000001 ||
                        $9 > -0.49999999999999)) ok=0
    }
    END { exit !(ok && rows == 4) }
' "$enabled_dir/szz.dat"

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'Ly=1' 'pbc=1' 't=-1' 'U=0' \
    'dtau=0.1' 'beta_list=0.3' 'nwarm=2' 'nmeas=8' 'nbin=2' \
    'stab=2' 'szz_q=3:0,0:0' 'szz_file=selected.dat' 'seed=9' \
    > "$selected_dir/input.txt"
(cd "$selected_dir" && "$repo_dir/dqmc" input.txt > stdout.txt)
awk '
    BEGIN { rows=0; ok=1 }
    !/^#/ {
        if (rows == 0 && !($4 == 0 && $5 == 3)) ok=0
        if (rows == 1 && !($4 == 1 && $5 == 0)) ok=0
        if ($2 * $3 < 1-1e-14 || $2 * $3 > 1+1e-14) ok=0
        rows++
    }
    END { exit !(ok && rows == 2) }
' "$selected_dir/selected.dat"

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' \
    'szz_q=all' 'szz_file=none' > "$run_dir/bad_file.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_file.txt >/dev/null 2>&1); then
    echo 'szz_file=none unexpectedly succeeded' >&2
    exit 1
fi

printf '%s\n' \
    'lattice=chain' 'Lx=4' 'nmeas=8' 'nbin=2' \
    'szz_q=0:0, 2:0' > "$run_dir/bad_space.txt"
if (cd "$run_dir" && "$repo_dir/dqmc" bad_space.txt >/dev/null 2>&1); then
    echo 'selector with internal whitespace unexpectedly succeeded' >&2
    exit 1
fi

echo OK
