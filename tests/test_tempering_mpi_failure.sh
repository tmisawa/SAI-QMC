#!/bin/sh
# Tempering under MPI: a ladder failure on one rank, ranks with no ladder, and an
# unwritable tempering_file must end on every rank without hanging in a collective.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
MPIRUN="${MPIRUN:-mpirun}"
bin="$root/build/test-hooks/dqmc_mpi"
if command -v timeout > /dev/null 2>&1; then
  limit() { timeout 120 "$@"; }
else
  limit() { perl -e 'alarm 120; exec @ARGV' "$@"; }
fi
write_input() { # nrep tempering_file
  cat > "$tmp/input.in" <<EOT
lattice=chain
Lx=4
pbc=1
t=-1.0
U=4
beta_list=0.5,0.75,1.0
tempering=dtau_ladder
tempering_ltr=20
tempering_interval=1
tempering_file=$2
nwarm=20
nmeas=40
nbin=4
stab=4
nrep=$1
seed=4242
parallel=mpi
EOT
}
# Record each rank's exit status so the launcher cannot hide a rank that succeeded.
cat > "$tmp/rank_exit.sh" <<'EOT'
#!/bin/sh
rank="${OMPI_COMM_WORLD_RANK:-${PMI_RANK:-${PMIX_RANK:-}}}"
[ -n "$rank" ] || exit 2
rc=0
"$@" > "rank.$rank.out" 2> "rank.$rank.err" || rc=$?
printf '%s\n' "$rc" > "rank.$rank.exit"
exit 0
EOT
codes() { # nranks -> space-separated exit codes
  n=$1; out=""; r=0
  while [ "$r" -lt "$n" ]; do
    if [ -f "$tmp/rank.$r.exit" ]; then out="$out$(cat "$tmp/rank.$r.exit") "; else out="${out}missing "; fi
    r=$((r + 1))
  done
  printf '%s' "$out"
}
fail=0
# (1) ladder 2 fails; with nrep=3 over 2 ranks it lives on rank 1 only
write_input 3 pt.tsv
rm -f "$tmp"/rank.*
if ! (cd "$tmp" && AFQMC_TEST_TEMPERING_FAIL_LADDER=2 limit "$MPIRUN" -n 2 sh "$tmp/rank_exit.sh" "$bin" input.in > /dev/null 2>&1); then
  echo "FAIL case1 launcher/timeout"; fail=1
fi
[ "$(codes 2)" = "1 1 " ] || { echo "FAIL case1 rank exits: $(codes 2)"; fail=1; }
grep -q 'TEST_TEMPERING_FAIL ladder=2' "$tmp/rank.1.err" || { echo "FAIL case1 hook not on rank 1"; fail=1; }
grep -q 'ladder 2' "$tmp/rank.0.err" || { echo "FAIL case1 root does not name ladder 2"; fail=1; }
awk -F'\t' '$1=="ladder" && $2==2 && $6==1 {ok=1} END {exit !ok}' "$tmp/pt.tsv" || { echo "FAIL case1 failed ladder row"; fail=1; }
awk -F'\t' '$1=="ladder" && $2!=2 && $6!=0 {bad=1} END {exit bad}' "$tmp/pt.tsv" || { echo "FAIL case1 other ladders marked failed"; fail=1; }
# (2) nrep=2 over 4 ranks: ranks 2 and 3 own no ladder and still join every collective
write_input 2 pt.tsv
rm -f "$tmp"/rank.* "$tmp/pt.tsv"
if ! (cd "$tmp" && limit "$MPIRUN" -n 4 sh "$tmp/rank_exit.sh" "$bin" input.in > /dev/null 2>&1); then
  echo "FAIL case2 launcher/timeout"; fail=1
fi
[ "$(codes 4)" = "0 0 0 0 " ] || { echo "FAIL case2 rank exits: $(codes 4)"; fail=1; }
p=$(cat "$tmp/pt.tsv" 2>/dev/null | grep -c '^pair' || true)
[ "$p" -eq 20 ] || { echo "FAIL case2 pair rows $p"; fail=1; }
# (3) unwritable tempering_file: every rank exits nonzero
write_input 2 no_such_dir/pt.tsv
rm -f "$tmp"/rank.*
if ! (cd "$tmp" && limit "$MPIRUN" -n 2 sh "$tmp/rank_exit.sh" "$bin" input.in > /dev/null 2>&1); then
  echo "FAIL case3 launcher/timeout"; fail=1
fi
[ "$(codes 2)" = "1 1 " ] || { echo "FAIL case3 rank exits: $(codes 2)"; fail=1; }
# (4) no tempering_file: all ranks succeed and nothing is written
write_input 3 pt.tsv
sed -i.bak '/^tempering_file=/d' "$tmp/input.in"
rm -f "$tmp"/rank.* "$tmp/pt.tsv"
if ! (cd "$tmp" && limit "$MPIRUN" -n 2 sh "$tmp/rank_exit.sh" "$bin" input.in > /dev/null 2>&1); then
  echo "FAIL case4 launcher/timeout"; fail=1
fi
[ "$(codes 2)" = "0 0 " ] || { echo "FAIL case4 rank exits: $(codes 2)"; fail=1; }
[ ! -e "$tmp/pt.tsv" ] || { echo "FAIL case4 wrote pt.tsv"; fail=1; }
[ "$fail" -eq 0 ] && echo OK
exit "$fail"
