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
[ "$fail" -eq 0 ] && echo OK || exit 1
