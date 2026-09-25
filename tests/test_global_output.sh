#!/bin/sh
# spec 4 / 5: stdout columns and the replica-bin file of a global_update=site run.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
mode="${AFQMC_TEST_MODE:-serial}"
bin="$root/dqmc"
hook_bin="$root/build/test-hooks/dqmc"
if [ "$mode" = omp ]; then
  bin="$root/dqmc_omp"
  hook_bin="$root/build/test-hooks/dqmc_omp"
fi
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
cat > "$tmp/input.in" <<'EOF'
lattice=chain
Lx=4
pbc=1
t=-1.0
U=4
dtau=0.1
beta_list=1,2
nwarm=7
nmeas=20
nbin=4
stab=4
nrep=3
seed=5
sweep_order=alternating
green_rebuild=combine
szz_q=2:0,0:0
szz_file=szz.tsv
sperp_q=0:0,2:0
sperp_file=sperp.tsv
global_update=site
global_interval=3
replica_bin_file=bins.tsv
EOF
printf 'parallel=%s\n' "$mode" >> "$tmp/input.in"
cp "$tmp/input.in" "$tmp/base.in"  # immutable valid input for independent test cases
(cd "$tmp" && "$bin" input.in > stdout.txt)
fail=0
grep -q '^# lattice=.* global_update=site global_interval=3' "$tmp/stdout.txt" || { echo "FAIL header"; fail=1; }
grep -q 'acceptance dAcceptance  global_acceptance global_attempts$' "$tmp/stdout.txt" || { echo "FAIL column names"; fail=1; }
# data rows have 16 fields; attempts per beta = 3 replicas * 4 sites * 7 measurement passes = 84
awk '!/^#/ { if (NF != 16) bad=1; if ($16 != 84) bad=1; if ($15 < 0 || $15 > 1) bad=1 } END { exit bad }' "$tmp/stdout.txt" || { echo "FAIL data row"; fail=1; }
rows=$(grep -vc '^#' "$tmp/bins.tsv")
[ "$rows" -eq 24 ] || { echo "FAIL bin rows $rows"; fail=1; }
[ "$(grep -c '^# columns:' "$tmp/bins.tsv")" -eq 1 ] || { echo "FAIL header count"; fail=1; }
grep -q '^# szz_Q_index=0 szz_0_index=1 sperp_Q_index=1$' "$tmp/bins.tsv" || { echo "FAIL q indices"; fail=1; }
# ordering: beta_index, replica_id, bin_id ascending
awk -F'\t' '!/^#/ { k=$1*10000+$5*100+$7; if (seen && k <= prev) bad=1; prev=k; seen=1 } END { exit bad }' "$tmp/bins.tsv" || { echo "FAIL order"; fail=1; }
# per-beta sum of global_attempts equals the stdout column
awk -F'\t' '!/^#/ { s[$1]+=$17 } END { exit !(s[0]==84 && s[1]==84) }' "$tmp/bins.tsv" || { echo "FAIL attempts sum"; fail=1; }
# interval longer than the run -> nan acceptance, 0 attempts
sed -i.bak 's/^global_interval=3$/global_interval=1000/' "$tmp/input.in"
(cd "$tmp" && "$bin" input.in > stdout2.txt)
awk '!/^#/ { if ($15 != "nan" || $16 != 0) bad=1 } END { exit bad }' "$tmp/stdout2.txt" || { echo "FAIL nan acceptance"; fail=1; }
# unwritable bin file is a failure
sed 's/^global_update=site$/global_update=none/' "$tmp/base.in" > "$tmp/binonly.in"
(cd "$tmp" && "$bin" binonly.in > binonly.stdout)
awk '!/^#/ { if (NF != 14) bad=1; n++ } END { exit (bad || n != 2) }' "$tmp/binonly.stdout" || { echo "FAIL bin-only stdout"; fail=1; }
awk -F'\t' '!/^#/ { if ($16 != 0 || $17 != 0) bad=1; n++ } END { exit (bad || n != 24) }' "$tmp/bins.tsv" || { echo "FAIL bin-only rows/counters"; fail=1; }
sed -i.bak 's#^replica_bin_file=.*#replica_bin_file=/nonexistent_dir/bins.tsv#' "$tmp/input.in"
if (cd "$tmp" && "$bin" input.in > /dev/null 2>&1); then echo "FAIL open error ignored"; fail=1; fi
# collisions must be rejected before diagnostic initialization or hopping dump (I1/B1)
collide() { # description extra_lines protected_file
  sed -e '/^replica_bin_file=/d' -e '/^global_interval=/d' -e '/^green_rebuild=/d' "$tmp/base.in" > "$tmp/c.in"
  printf '%b' "$2" >> "$tmp/c.in"
  printf 'SENTINEL\n' > "$tmp/$3"
  printf 'SENTINEL\n' > "$tmp/hopping_used.txt"
  if (cd "$tmp" && "$bin" c.in > /dev/null 2> c.err); then echo "FAIL collision accepted: $1"; fail=1; fi
  grep -q '^ERROR: output path collision:' "$tmp/c.err" || { echo "FAIL collision diagnostic: $1"; fail=1; }
  [ "$(cat "$tmp/$3")" = SENTINEL ] || { echo "FAIL $3 clobbered: $1"; fail=1; }
  [ "$(cat "$tmp/hopping_used.txt")" = SENTINEL ] || { echo "FAIL hopping dump preceded collision check: $1"; fail=1; }
}
collide "default replica log" 'global_interval=3\nreplica_bin_file=replicas.dat\n' replicas.dat
collide "default profile file" 'global_interval=3\nprofile=1\nreplica_bin_file=profile.dat\n' profile.dat
collide "explicit szz file" 'global_interval=3\nreplica_bin_file=szz.tsv\n' szz.tsv
collide "stabilization drift" 'global_interval=3\nstab_drift_file=shared.tsv\nreplica_bin_file=shared.tsv\n' shared.tsv
collide "UDV scale" 'global_interval=3\nudv_scale_file=shared.tsv\nreplica_bin_file=shared.tsv\n' shared.tsv
collide "centered UDV" 'global_interval=3\ngreen_rebuild=centered\nudv_centered_file=shared.tsv\nreplica_bin_file=shared.tsv\n' shared.tsv
collide "fixed hopping output" 'global_interval=3\nreplica_bin_file=hopping_used.txt\n' hopping_used.txt
collide "szz file via ./ alias" 'global_interval=3\nreplica_bin_file=./szz.tsv\n' szz.tsv
# the run-time input itself must be protected (review P1): reject and keep the input intact
sed -e '/^replica_bin_file=/d' -e '/^global_interval=/d' "$tmp/base.in" > "$tmp/self.in"
printf 'global_interval=3\nreplica_bin_file=self.in\n' >> "$tmp/self.in"
cp "$tmp/self.in" "$tmp/self.expected"
if (cd "$tmp" && "$bin" self.in > /dev/null 2> self.err); then echo "FAIL input-file collision accepted"; fail=1; fi
grep -q '^ERROR: output path collision: replica_bin_file and input' "$tmp/self.err" || { echo "FAIL input-file collision diagnostic"; fail=1; }
cmp -s "$tmp/self.in" "$tmp/self.expected" || { echo "FAIL input file clobbered"; fail=1; }
# a hard link of the szz file is the same file (identity, not string, comparison)
sed -e '/^replica_bin_file=/d' -e '/^global_interval=/d' "$tmp/base.in" > "$tmp/link.in"
printf 'global_interval=3\nreplica_bin_file=szz_link.tsv\n' >> "$tmp/link.in"
printf 'SENTINEL\n' > "$tmp/szz.tsv"; rm -f "$tmp/szz_link.tsv"; ln "$tmp/szz.tsv" "$tmp/szz_link.tsv"
if (cd "$tmp" && "$bin" link.in > /dev/null 2> link.err); then echo "FAIL hard-link collision accepted"; fail=1; fi
[ "$(cat "$tmp/szz.tsv")" = SENTINEL ] || { echo "FAIL hard-linked szz file clobbered"; fail=1; }
rm -f "$tmp/szz_link.tsv" "$tmp/szz.tsv"
# Dangling symlinks still name the same future output, including relative
# targets in another directory and links on the spin-output side.
for kind in relative absolute chain reverse; do
  dir="$tmp/dangling_$kind"
  mkdir -p "$dir/links"
  sed '/^replica_bin_file=/d' "$tmp/base.in" > "$dir/input.in"
  printf 'replica_bin_file=bins.tsv\n' >> "$dir/input.in"
  cp "$dir/input.in" "$dir/input.expected"
  printf 'SENTINEL\n' > "$dir/hopping_used.txt"
  case "$kind" in
    relative) ln -s szz.tsv "$dir/bins.tsv" ;;
    absolute) ln -s "$dir/szz.tsv" "$dir/bins.tsv" ;;
    chain) ln -s links/next "$dir/bins.tsv"; ln -s ../szz.tsv "$dir/links/next" ;;
    reverse) ln -s bins.tsv "$dir/szz.tsv" ;;
  esac
  if (cd "$dir" && "$bin" input.in > stdout.txt 2> error.txt); then
    echo "FAIL dangling-$kind collision accepted"; fail=1
  fi
  grep -q '^ERROR: output path collision: replica_bin_file and szz_file' "$dir/error.txt" || { echo "FAIL dangling-$kind diagnostic"; fail=1; }
  cmp -s "$dir/input.in" "$dir/input.expected" || { echo "FAIL dangling-$kind input clobbered"; fail=1; }
  [ "$(cat "$dir/hopping_used.txt")" = SENTINEL ] || { echo "FAIL dangling-$kind hopping clobbered"; fail=1; }
  [ ! -e "$dir/szz.tsv" ] && [ ! -e "$dir/bins.tsv" ] || { echo "FAIL dangling-$kind output created"; fail=1; }
done
# A dangling link to a distinct new file is a valid output destination.
dir="$tmp/dangling_distinct"
mkdir "$dir"
cp "$tmp/base.in" "$dir/input.in"
ln -s actual-bins.tsv "$dir/bins.tsv"
(cd "$dir" && "$bin" input.in > stdout.txt) || { echo "FAIL distinct dangling destination"; fail=1; }
grep -q '^# replica-bin sums;' "$dir/actual-bins.tsv" || { echo "FAIL distinct dangling bin output"; fail=1; }
grep -q '^# definition=Szz' "$dir/szz.tsv" || { echo "FAIL distinct dangling spin output"; fail=1; }
# an inactive default name is not reserved: profile=0 leaves profile.dat free
sed -e '/^replica_bin_file=/d' "$tmp/base.in" > "$tmp/d.in"; printf 'replica_bin_file=profile.dat\n' >> "$tmp/d.in"
(cd "$tmp" && "$bin" d.in > /dev/null) || { echo "FAIL inactive default name rejected"; fail=1; }
# injected write / close failures end the run with a nonzero status (I6)
sed -e '/^global_interval=/d' -e '/^replica_bin_file=/d' "$tmp/base.in" > "$tmp/e.in"
printf 'global_interval=3\nreplica_bin_file=bins2.tsv\n' >> "$tmp/e.in"
for hook in AFQMC_TEST_BIN_WRITE_FAIL AFQMC_TEST_BIN_CLOSE_FAIL; do
  if (cd "$tmp" && env "$hook=1" "$hook_bin" e.in > /dev/null 2>&1); then echo "FAIL $hook ignored"; fail=1; fi
done
# a numerical failure in a global pass of beta index 1 leaves no rows of that beta (I6)
rm -f "$tmp/bins2.tsv"
if (cd "$tmp" && AFQMC_TEST_GLOBAL_FAIL_AT=9 AFQMC_TEST_GLOBAL_FAIL_BETA=1 "$hook_bin" e.in > numerical.stdout 2> numerical.err); then echo "FAIL numerical failure ignored"; fail=1; fi
for replica in 0 1 2; do
  grep -q "^TEST_GLOBAL_FAIL beta_index=1 replica_id=$replica sweep=9 pass_rc=1 status=1$" "$tmp/numerical.err" || { echo "FAIL global-pass failure not exercised for replica $replica"; fail=1; }
done
if [ -f "$tmp/bins2.tsv" ]; then
  awk -F'\t' '!/^#/ { if ($1 == 0) good++; else bad=1 } END { exit (bad || good != 12) }' "$tmp/bins2.tsv" || { echo "FAIL expected 12 rows of beta 0 and none of failed beta 1"; fail=1; }
else
  echo "FAIL successful beta 0 has no bin file"; fail=1
fi
# a square lattice with one direction of length 1 keeps the measured af momentum in the bin file (review P2)
mkdir "$tmp/l1"
cat > "$tmp/l1/input.in" <<'EOF'
lattice=square
Lx=4
Ly=1
pbc=1
t=-1.0
U=4
dtau=0.1
beta_list=1
nwarm=3
nmeas=8
nbin=2
stab=4
nrep=1
seed=9
szz_q=af
szz_file=szz.tsv
sperp_q=af
sperp_file=sperp.tsv
global_update=site
global_interval=2
replica_bin_file=bins.tsv
EOF
(cd "$tmp/l1" && "$bin" input.in > stdout.txt) || { echo "FAIL 4x1 run"; fail=1; }
grep -q '^# szz_Q_index=0 szz_0_index=-1 sperp_Q_index=0$' "$tmp/l1/bins.tsv" || { echo "FAIL 4x1 q indices"; fail=1; }
awk -F'\t' '!/^#/ { if ($18 == "nan" || $19 == "nan") bad=1; n++ } END { exit (bad || n != 2) }' "$tmp/l1/bins.tsv" || { echo "FAIL 4x1 Q columns are nan"; fail=1; }

# --- Stage A site diagnostic (spec 3.3 / 5) ---
sed -e '/^replica_bin_file=/d' "$tmp/base.in" > "$tmp/diag.in"
printf 'replica_bin_file=bins.tsv\nglobal_site_diag_file=site_diag.tsv\n' >> "$tmp/diag.in"
mkdir "$tmp/diag"; cp "$tmp/diag.in" "$tmp/diag/input.in"
(cd "$tmp/diag" && "$bin" input.in > stdout.txt) || { echo "FAIL diag run"; fail=1; }
rows=$(grep -vc '^#' "$tmp/diag/site_diag.tsv")
[ "$rows" -eq 600 ] || { echo "FAIL diag rows $rows"; fail=1; }
grep -q '^# columns: beta_index	beta_requested	Ltr	replica_id	seed	indicator	bin	bin_lower	bin_upper	attempts	accepted$' "$tmp/diag/site_diag.tsv" || { echo "FAIL diag header"; fail=1; }
grep -q 'global_site_select=fixed' "$tmp/diag/site_diag.tsv" || { echo "FAIL diag select header"; fail=1; }
# Per-beta attempts for each indicator equal stdout global_attempts (84 per beta),
# and accepted sums agree with the replica-bin file.
awk -F'\t' '!/^#/ && $6=="p" { a[$1]+=$10 } END { exit !(a[0]==84 && a[1]==84) }' "$tmp/diag/site_diag.tsv" || { echo "FAIL diag attempts sum (p)"; fail=1; }
awk -F'\t' '!/^#/ && $6=="d" { a[$1]+=$10 } END { exit !(a[0]==84 && a[1]==84) }' "$tmp/diag/site_diag.tsv" || { echo "FAIL diag attempts sum (d)"; fail=1; }
acc_bins=$(awk -F'\t' '!/^#/ { s+=$16 } END { print s+0 }' "$tmp/diag/bins.tsv")
acc_diag=$(awk -F'\t' '!/^#/ && $6=="p" { s+=$11 } END { print s+0 }' "$tmp/diag/site_diag.tsv")
[ "$acc_bins" = "$acc_diag" ] || { echo "FAIL diag accepted sum $acc_bins vs $acc_diag"; fail=1; }
# Ordering is beta, replica, indicator (p then d), bin ascending.
awk -F'\t' '!/^#/ { ind=($6=="p")?0:1; k=$1*100000+$4*1000+ind*100+$7; if (seen && k<=prev) bad=1; prev=k; seen=1 } END { exit bad }' "$tmp/diag/site_diag.tsv" || { echo "FAIL diag order"; fail=1; }
# Enabling the diagnostic must not change any other output, including the replica log.
mkdir "$tmp/nodiag"; sed -e '/^global_site_diag_file=/d' "$tmp/diag.in" > "$tmp/nodiag/input.in"
(cd "$tmp/nodiag" && "$bin" input.in > stdout.txt)
for f in stdout.txt bins.tsv szz.tsv sperp.tsv replicas.dat; do
  cmp -s "$tmp/diag/$f" "$tmp/nodiag/$f" || { echo "FAIL diag changed $f"; fail=1; }
done
# The site diagnostic is valid without a replica-bin file.
mkdir "$tmp/diagonly"; sed -e '/^replica_bin_file=/d' "$tmp/diag.in" > "$tmp/diagonly/input.in"
(cd "$tmp/diagonly" && "$bin" input.in > stdout.txt) || { echo "FAIL diag-only run"; fail=1; }
[ "$(grep -vc '^#' "$tmp/diagonly/site_diag.tsv")" -eq 600 ] || { echo "FAIL diag-only rows"; fail=1; }
cmp -s "$tmp/diagonly/stdout.txt" "$tmp/nodiag/stdout.txt" || { echo "FAIL diag-only changed stdout"; fail=1; }
# The key requires the site update.
sed -e 's/^global_update=site$/global_update=none/' "$tmp/diag.in" > "$tmp/diag_none.in"
if (cd "$tmp" && "$bin" diag_none.in > /dev/null 2>&1); then echo "FAIL diag accepted without site update"; fail=1; fi
# Collisions are rejected before any output is written. Scalar validation must
# precede the opt-in collision checks.
diag_collide() { # description extra_lines protected_file [checker: optin|scalar]
  sed -e '/^replica_bin_file=/d' -e '/^global_interval=/d' -e '/^green_rebuild=/d' "$tmp/base.in" > "$tmp/dc.in"
  printf '%b' "$2" >> "$tmp/dc.in"
  printf 'SENTINEL\n' > "$tmp/$3"
  printf 'SENTINEL\n' > "$tmp/hopping_used.txt"
  if (cd "$tmp" && "$bin" dc.in > /dev/null 2> dc.err); then echo "FAIL diag collision accepted: $1"; fail=1; fi
  case "${4:-optin}" in
    scalar) grep -Fxq "ERROR: output_file $3 collides with global_site_diag_file $3" "$tmp/dc.err" || { echo "FAIL scalar/diag collision diagnostic: $1"; fail=1; } ;;
    optin) grep -Eq '^ERROR: output path collision: (global_site_diag_file and [A-Za-z_.]+|[A-Za-z_.]+ and global_site_diag_file) both use ' "$tmp/dc.err" || { echo "FAIL diag collision diagnostic: $1"; fail=1; } ;;
    *) echo "FAIL unknown collision checker: $4"; fail=1 ;;
  esac
  [ "$(cat "$tmp/$3")" = SENTINEL ] || { echo "FAIL $3 clobbered: $1"; fail=1; }
  [ "$(cat "$tmp/hopping_used.txt")" = SENTINEL ] || { echo "FAIL hopping dump preceded diag collision check: $1"; fail=1; }
}
diag_collide "bin file" 'global_interval=3\nreplica_bin_file=shared.tsv\nglobal_site_diag_file=shared.tsv\n' shared.tsv
diag_collide "szz alias" 'global_interval=3\nglobal_site_diag_file=./szz.tsv\n' szz.tsv
diag_collide "replica log" 'global_interval=3\nglobal_site_diag_file=replicas.dat\n' replicas.dat
diag_collide "scalar output" 'global_interval=3\nglobal_site_diag_file=observables.dat\n' observables.dat scalar
sed -e '/^replica_bin_file=/d' -e '/^global_interval=/d' "$tmp/base.in" > "$tmp/dself.in"
printf 'global_interval=3\nglobal_site_diag_file=dself.in\n' >> "$tmp/dself.in"; cp "$tmp/dself.in" "$tmp/dself.expected"
if (cd "$tmp" && "$bin" dself.in > /dev/null 2> dself.err); then echo "FAIL diag input collision accepted"; fail=1; fi
cmp -s "$tmp/dself.in" "$tmp/dself.expected" || { echo "FAIL diag clobbered its input"; fail=1; }
# Dangling symlink collisions use fresh directories with no pre-existing target.
for kind in relative absolute chain reverse; do
  dir="$tmp/diag_dangling_$kind"
  mkdir -p "$dir/links"
  sed '/^replica_bin_file=/d' "$tmp/base.in" > "$dir/input.in"
  printf 'global_site_diag_file=site_diag.tsv\n' >> "$dir/input.in"
  cp "$dir/input.in" "$dir/input.expected"
  printf 'SENTINEL\n' > "$dir/hopping_used.txt"
  case "$kind" in
    relative) ln -s szz.tsv "$dir/site_diag.tsv" ;;
    absolute) ln -s "$dir/szz.tsv" "$dir/site_diag.tsv" ;;
    chain) ln -s links/next "$dir/site_diag.tsv"; ln -s ../szz.tsv "$dir/links/next" ;;
    reverse) ln -s site_diag.tsv "$dir/szz.tsv" ;;
  esac
  if (cd "$dir" && "$bin" input.in > stdout.txt 2> error.txt); then echo "FAIL diag dangling-$kind collision accepted"; fail=1; fi
  grep -Eq '^ERROR: output path collision: (global_site_diag_file and szz_file|szz_file and global_site_diag_file)' "$dir/error.txt" || { echo "FAIL diag dangling-$kind diagnostic"; fail=1; }
  cmp -s "$dir/input.in" "$dir/input.expected" || { echo "FAIL diag dangling-$kind input clobbered"; fail=1; }
  [ "$(cat "$dir/hopping_used.txt")" = SENTINEL ] || { echo "FAIL diag dangling-$kind hopping clobbered"; fail=1; }
  [ ! -e "$dir/szz.tsv" ] && [ ! -e "$dir/site_diag.tsv" ] || { echo "FAIL diag dangling-$kind output created"; fail=1; }
done
# Injected diagnostic and bin write/close failures must end the run nonzero.
for hook in AFQMC_TEST_DIAG_WRITE_FAIL AFQMC_TEST_DIAG_CLOSE_FAIL AFQMC_TEST_BIN_WRITE_FAIL AFQMC_TEST_BIN_CLOSE_FAIL; do
  if (cd "$tmp/diag" && env "$hook=1" "$hook_bin" input.in > /dev/null 2>&1); then echo "FAIL $hook ignored with the diagnostic enabled"; fail=1; fi
done
# A numerical failure in beta index 1 retains beta 0's 300 diagnostic rows only.
mkdir "$tmp/diagfail"; cp "$tmp/diag.in" "$tmp/diagfail/input.in"
if (cd "$tmp/diagfail" && AFQMC_TEST_GLOBAL_FAIL_AT=9 AFQMC_TEST_GLOBAL_FAIL_BETA=1 "$hook_bin" input.in > stdout.txt 2> err.txt); then echo "FAIL diag numerical failure ignored"; fail=1; fi
if [ -f "$tmp/diagfail/site_diag.tsv" ]; then
  awk -F'\t' '!/^#/ { if ($1 == 0) good++; else bad=1 } END { exit (bad || good != 300) }' "$tmp/diagfail/site_diag.tsv" || { echo "FAIL expected 300 diag rows of beta 0 and none of beta 1"; fail=1; }
else
  echo "FAIL successful beta 0 has no diag file"; fail=1
fi
[ "$fail" -eq 0 ] && echo OK || exit 1
