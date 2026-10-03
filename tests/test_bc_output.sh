#!/bin/sh
# Directional boundaries (bc_x/bc_y) end to end: identity with the same matrix
# given as lattice=file, spin on/off invariance, headers, the signed matrix,
# rejected inputs, and combination with tempering/conditional/global updates.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
fail=0
bad() { echo "FAIL $*"; fail=1; }

common='t=-1.0
U=4
dtau=0.1
beta_list=1
nwarm=20
nmeas=40
nbin=2
stab=4
seed=20261002'
ap='lattice=square
Lx=4
Ly=2
bc_x=antiperiodic'
tok='bc_x=antiperiodic bc_y=periodic'

run() { # name input_text; sets rc
  mkdir -p "$work/$1"
  printf '%s\n' "$2" > "$work/$1/input.in"
  if (cd "$work/$1" && "$root/dqmc" input.in > stdout.txt 2> stderr.txt); then
    rc=0
  else
    rc=$?
  fi
}
data() { grep -v '^#' "$1" || true; }

# (A) built-in AP/P versus the same matrix as lattice=file, spin off on both
run a_builtin "$ap
$common
replica_bin_file=bins.tsv"
[ "$rc" -eq 0 ] || bad "A built-in run exit $rc"
run a_file "lattice=file
latfile=../a_builtin/hopping_used.txt
$common
replica_bin_file=bins.tsv"
[ "$rc" -eq 0 ] || bad "A file run exit $rc"
data "$work/a_builtin/observables.dat" > "$work/a_scalar"
data "$work/a_file/observables.dat" > "$work/f_scalar"
[ -s "$work/a_scalar" ] || bad "A no scalar data rows"
cmp -s "$work/a_scalar" "$work/f_scalar" || bad "A scalar data rows differ from lattice=file"
data "$work/a_builtin/bins.tsv" > "$work/a_bins"
data "$work/a_file/bins.tsv" > "$work/f_bins"
[ -s "$work/a_bins" ] || bad "A no bin data rows"
cmp -s "$work/a_bins" "$work/f_bins" || bad "A bin data rows differ from lattice=file"

# (B) spin measurement on versus off (built-in only): non-spin data unchanged
run b_on "$ap
$common
replica_bin_file=bins.tsv
szz_q=all
sperp_q=all
spin_consistency_file=consistency.dat"
[ "$rc" -eq 0 ] || bad "B spin-on run exit $rc"
data "$work/b_on/observables.dat" > "$work/b_scalar"
cmp -s "$work/a_scalar" "$work/b_scalar" || bad "B scalar data rows change with spin measurement"
data "$work/b_on/bins.tsv" | cut -f1-17 > "$work/b_bins17"
cut -f1-17 "$work/a_bins" > "$work/a_bins17"
cmp -s "$work/a_bins17" "$work/b_bins17" || bad "B non-spin bin columns change with spin measurement"
[ "$(data "$work/b_on/szz.dat" | wc -l | tr -d ' ')" = 8 ] || bad "B expected 8 Szz rows"

# (C) every header that used to print pbc= now carries the resolved boundary
for f in szz.dat sperp.dat consistency.dat bins.tsv; do
  grep -q "^# lattice=square Lx=4 Ly=2 n=8 $tok " "$work/b_on/$f" || bad "C $f boundary header"
  if grep -q 'pbc=' "$work/b_on/$f"; then bad "C $f still prints pbc="; fi
done
grep -q "^# lattice=square n=8 .* bins=2 $tok" "$work/b_on/observables.dat" || bad "C scalar header token"
grep -q "^# lattice=square n=8 .* bins=2 $tok" "$work/b_on/stdout.txt" || bad "C stdout header token"
run c_diag "$ap
$common
global_update=site
global_interval=5
global_site_diag_file=site_diag.tsv"
[ "$rc" -eq 0 ] || bad "C site-diag run exit $rc"
grep -q "^# lattice=square Lx=4 Ly=2 n=8 $tok " "$work/c_diag/site_diag.tsv" || bad "C site_diag boundary header"
run c_po "lattice=square
Lx=4
Ly=4
bc_x=periodic
bc_y=open
$common
szz_q=af"
[ "$rc" -eq 0 ] || bad "C P/O run exit $rc"
grep -q '^# lattice=square Lx=4 Ly=4 n=16 bc_x=periodic bc_y=open ' "$work/c_po/szz.dat" || bad "C P/O header"
run c_yap "lattice=square
Lx=4
Ly=4
bc_y=antiperiodic
$common
szz_q=af"
[ "$rc" -eq 0 ] || bad "C y-AP run exit $rc"
grep -q '^# lattice=square Lx=4 Ly=4 n=16 bc_x=periodic bc_y=antiperiodic ' "$work/c_yap/szz.dat" || bad "C y-AP header"

# (D) the signed matrix, written in full by hand; chain AP with af = (pi, 0)
printf '%s\n' 8 \
  '0 -1 0 1 -2 0 0 0' \
  '-1 0 -1 0 0 -2 0 0' \
  '0 -1 0 -1 0 0 -2 0' \
  '1 0 -1 0 0 0 0 -2' \
  '-2 0 0 0 0 -1 0 1' \
  '0 -2 0 0 -1 0 -1 0' \
  '0 0 -2 0 0 -1 0 -1' \
  '0 0 0 -2 1 0 -1 0' > "$work/expected_hopping.txt"
cmp -s "$work/expected_hopping.txt" "$work/a_builtin/hopping_used.txt" || bad "D AP/P hopping matrix"
run d_chain "lattice=chain
Lx=4
bc_x=antiperiodic
$common
szz_q=af"
[ "$rc" -eq 0 ] || bad "D chain AP exit $rc"
grep -q '^# lattice=chain Lx=4 Ly=1 n=4 bc_x=antiperiodic t=' "$work/d_chain/szz.dat" || bad "D chain header"
data "$work/d_chain/szz.dat" | awk 'NR == 1 { ok = ($5 == 2 && $6 == 0) } END { exit !(NR == 1 && ok) }' ||
  bad "D chain af momentum is not (pi, 0)"

# (E) rejected inputs and their messages
expect_reject() { # name expected_message input_text
  run "$1" "$3"
  [ "$rc" -ne 0 ] || bad "E $1 unexpectedly succeeded"
  grep -qF "$2" "$work/$1/stderr.txt" || bad "E $1 message: $(head -1 "$work/$1/stderr.txt")"
}
expect_reject e_pbc 'ERROR: pbc/bc cannot be combined with bc_x/bc_y' "lattice=square
Lx=4
Ly=4
pbc=1
bc_x=antiperiodic
$common"
expect_reject e_file 'ERROR: bc_x/bc_y require lattice=chain or lattice=square' "lattice=file
latfile=../a_builtin/hopping_used.txt
bc_x=periodic
$common"
expect_reject e_chain_y 'ERROR: bc_y requires lattice=square' "lattice=chain
Lx=4
bc_y=open
$common"
expect_reject e_len2 'ERROR: bc_x=antiperiodic requires an even Lx >= 4 (got Lx=2)' "lattice=square
Lx=2
Ly=4
bc_x=antiperiodic
$common"
expect_reject e_value 'ERROR: bc_x must be periodic, antiperiodic, or open (got apbc)' "lattice=square
Lx=4
Ly=4
bc_x=apbc
$common"
expect_reject e_odd 'ERROR: non-bipartite lattice is outside v1 scope' "lattice=square
Lx=4
Ly=3
bc_x=antiperiodic
$common"

# (F) tempering + conditional measurement + global update with AP/P (no timing compared)
run f_pt "$ap
t=-1.0
U=4
beta_list=1,2
tempering=dtau_ladder
tempering_ltr=20
conditional_measure=1
global_update=site
global_interval=5
replica_bin_file=bins.tsv
nwarm=10
nmeas=20
nbin=2
stab=4
seed=20261003"
[ "$rc" -eq 0 ] || bad "F tempering run exit $rc: $(head -1 "$work/f_pt/stderr.txt")"
grep -q "^# lattice=square Lx=4 Ly=2 n=8 $tok " "$work/f_pt/bins.tsv" || bad "F tempering bins header"

[ "$fail" -eq 0 ] && echo OK
exit "$fail"
