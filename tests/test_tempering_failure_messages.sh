#!/bin/sh
# Tempering failure messages: a slot whose own sweep or global pass failed is
# reported as that slot's numerical breakdown, not as a tempering exchange
# failure, whether or not the failed slot takes part in the next exchange
# round; an exchange that fails while every slot is intact is still reported
# as an exchange failure. Production binaries ignore the injection hooks.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
bin="$root/dqmc"
hook_bin="$root/build/test-hooks/dqmc"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
# 3 slots, exchange round after every ladder sweep s = 1, 2, ... (round s-1;
# even rounds try pair 0, odd rounds pair 1); sweeps 1-4 are warmup and
# 5-12 measurement (2 bins of 4); a global pass after every sweep.
cat > "$tmp/input.in" <<'EOF'
lattice=chain
Lx=4
pbc=1
t=-1.0
U=4
beta_list=0.5,0.75,1.0
tempering=dtau_ladder
tempering_ltr=20
tempering_interval=1
tempering_file=pt.tsv
nwarm=4
nmeas=8
nbin=2
stab=4
nrep=1
seed=4242
global_update=site
global_interval=1
replica_bin_file=bins.tsv
EOF
fail=0
has() { # case pattern
  grep -q "$2" "$tmp/$1.err" || { echo "FAIL $1: missing: $2"; fail=1; }
}
lacks() { # case pattern
  if grep -q "$2" "$tmp/$1.err"; then echo "FAIL $1: unexpected: $2"; fail=1; fi
}
run_case() { # case env-assignments...
  name=$1
  shift
  rm -f "$tmp/pt.tsv"
  if (cd "$tmp" && env "$@" "$hook_bin" input.in > "$name.out" 2> "$name.err"); then
    echo "FAIL $name: run succeeded"; fail=1
  fi
  has "$name" '^ERROR: tempering ladder 0 failed$'
  # a failed ladder fails the run: header-only scalar output, empty bin file,
  # and only tempering_file written, with failed=1 in the ladder row
  if grep -qv '^#' "$tmp/$name.out"; then echo "FAIL $name: scalar rows written"; fail=1; fi
  [ ! -s "$tmp/bins.tsv" ] || { echo "FAIL $name: bins.tsv not empty"; fail=1; }
  awk -F'\t' '$1=="ladder" && $2==0 && $6==1 {ok=1} END {exit !ok}' "$tmp/pt.tsv" ||
    { echo "FAIL $name: ladder row not failed=1"; fail=1; }
}
# (1) measurement: slot 1's own global pass fails at sweep 6; round 5 tries
#     pair 1 = (1,2), which contains the failed slot
run_case meas AFQMC_TEST_GLOBAL_FAIL_AT=6 AFQMC_TEST_GLOBAL_FAIL_BETA=1
has meas '^TEST_GLOBAL_FAIL beta_index=1 replica_id=0 sweep=6 pass_rc=1 status=1$'
has meas '^ERROR: dqmc measurement numerical breakdown (slot=1 ladder=0 beta_index=1 .* bin=0 meas=1 status=1 '
lacks meas 'tempering exchange failed'
# (2) warmup: slot 0 fails after sweep 2; round 1 tries only pair 1 = (1,2)
run_case warm AFQMC_TEST_TEMPERING_SLOT_FAIL_AT=2 AFQMC_TEST_TEMPERING_SLOT_FAIL_SLOT=0
has warm '^TEST_TEMPERING_SLOT_FAIL ladder=0 slot=0 sweep=2$'
has warm '^ERROR: dqmc warmup numerical breakdown (slot=0 ladder=0 beta_index=0 .* status=1 '
lacks warm 'tempering exchange failed'
# (3) every slot intact, the weight evaluation of round 2 (pair 0) fails
run_case exch AFQMC_TEST_TEMPERING_EXCHANGE_FAIL_AT=3
has exch '^TEST_TEMPERING_EXCHANGE_FAIL ladder=0 sweep=3$'
has exch '^ERROR: tempering exchange failed (ladder=0 pair=0 round=2 sweep=3 slot_status=0,0)$'
lacks exch 'numerical breakdown'
# production binaries ignore the hooks: same bins and exchange statistics
(cd "$tmp" && "$bin" input.in > ref.out 2> ref.err) || { echo "FAIL production run"; fail=1; }
cp "$tmp/bins.tsv" "$tmp/ref.bins"
grep -v '^cost' "$tmp/pt.tsv" > "$tmp/ref.pt"
(cd "$tmp" && env AFQMC_TEST_GLOBAL_FAIL_AT=6 AFQMC_TEST_GLOBAL_FAIL_BETA=1 \
  AFQMC_TEST_TEMPERING_SLOT_FAIL_AT=2 AFQMC_TEST_TEMPERING_SLOT_FAIL_SLOT=0 \
  AFQMC_TEST_TEMPERING_EXCHANGE_FAIL_AT=3 AFQMC_TEST_TEMPERING_FAIL_LADDER=0 \
  "$bin" input.in > hooked.out 2> hooked.err) || { echo "FAIL production run with hooks set"; fail=1; }
cmp -s "$tmp/ref.bins" "$tmp/bins.tsv" || { echo "FAIL production bins changed by hooks"; fail=1; }
grep -v '^cost' "$tmp/pt.tsv" | cmp -s "$tmp/ref.pt" - || { echo "FAIL production pt.tsv changed by hooks"; fail=1; }
grep -v '^# tempering solver_elapsed_seconds=' "$tmp/ref.out" > "$tmp/ref.stdout"
grep -v '^# tempering solver_elapsed_seconds=' "$tmp/hooked.out" | cmp -s "$tmp/ref.stdout" - ||
  { echo "FAIL production stdout changed by hooks"; fail=1; }
cmp -s "$tmp/ref.err" "$tmp/hooked.err" || { echo "FAIL production stderr changed by hooks"; fail=1; }
[ "$fail" -eq 0 ] && echo OK
exit "$fail"
