#!/bin/sh
# End-to-end tempering run: stdout header, bins.tsv per-slot beta/Ltr, tempering_file rows.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
cat > "$work/input.in" <<'EOF'
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
nwarm=20
nmeas=40
nbin=4
stab=4
nrep=2
seed=4242
replica_bin_file=bins.tsv
EOF
(cd "$work" && "$root/dqmc" input.in > stdout.txt 2> stderr.txt) || { echo "FAIL run"; cat "$work/stderr.txt"; exit 1; }
fail=0
grep -q '^# .* dtau=ladder ' "$work/stdout.txt" || { echo "FAIL stdout dtau=ladder"; fail=1; }
grep -q ' tempering=dtau_ladder tempering_ltr=20 tempering_interval=1' "$work/stdout.txt" || { echo "FAIL stdout tempering fields"; fail=1; }
grep -q '^# tempering=dtau_ladder tempering_ltr=20' "$work/bins.tsv" || { echo "FAIL bins header"; fail=1; }
# 3 slots x 2 ladders x 4 bins data rows, Ltr=20 on every row, beta_effective = beta_requested
n=$(grep -v '^#' "$work/bins.tsv" | wc -l | tr -d ' ')
[ "$n" -eq 24 ] || { echo "FAIL bins rows $n"; fail=1; }
awk -F'\t' '!/^#/ && ($4 != 20 || ($3 - $2) ^ 2 > 1e-24) {bad=1} END {exit bad}' "$work/bins.tsv" || { echo "FAIL bins Ltr/beta"; fail=1; }
# tempering_file: pair rows = ladders*(nbin+1)*(nslot-1) = 2*5*2 = 20
p=$(grep -c '^pair' "$work/pt.tsv")
[ "$p" -eq 20 ] || { echo "FAIL pair rows $p"; fail=1; }
s=$(grep -c '^slot' "$work/pt.tsv")
[ "$s" -eq 24 ] || { echo "FAIL slot rows $s"; fail=1; }
grep -q '^# slot=2 beta=1 dtau=0.050000000000000003' "$work/pt.tsv" || { echo "FAIL slot header"; fail=1; }
# measurement attempts per ladder: 40 rounds
awk -F'\t' '$1=="pair" && $3>=0 {a[$2]+=$5} END {for (l in a) if (a[l]!=40) exit 1}' "$work/pt.tsv" || { echo "FAIL attempts"; fail=1; }
# integers are decimal integers (no exponent, no decimal point) and the seed is exact
awk -F'\t' '$1=="ladder" && ($5 !~ /^[0-9]+$/ || $6 !~ /^[01]$/) {bad=1} END {exit bad}' "$work/pt.tsv" || { echo "FAIL ladder row format"; fail=1; }
tail -1 "$work/stdout.txt" | grep -q '^# tempering solver_elapsed_seconds=' || { echo "FAIL solver_elapsed line"; fail=1; }
# output collisions are rejected before any file is written (identity, input, alias)
for bad in 'tempering_file=bins.tsv' 'tempering_file=input.in' 'tempering_file=./bins.tsv'; do
  mkdir -p "$work/col"; cp "$work/input.in" "$work/col/input.in"
  sed -i.bak '/^tempering_file=/d' "$work/col/input.in"; printf '%s\n' "$bad" >> "$work/col/input.in"
  cp "$work/col/input.in" "$work/col/input.before"
  if (cd "$work/col" && "$root/dqmc" input.in > /dev/null 2> err.txt); then echo "FAIL collision accepted: $bad"; fail=1; fi
  grep -q 'output path collision: tempering_file' "$work/col/err.txt" || { echo "FAIL collision message: $bad"; fail=1; }
  cmp -s "$work/col/input.in" "$work/col/input.before" || { echo "FAIL input overwritten: $bad"; fail=1; }
  rm -rf "$work/col"
done
# an unwritable tempering_file fails with nonzero exit
mkdir -p "$work/nw"; cp "$work/input.in" "$work/nw/input.in"
sed -i.bak 's#^tempering_file=.*#tempering_file=no_such_dir/pt.tsv#' "$work/nw/input.in"
if (cd "$work/nw" && "$root/dqmc" input.in > /dev/null 2> err.txt); then echo "FAIL unwritable accepted"; fail=1; fi
# non-PT run with field_init=uniform (Task 5b)
mkdir -p "$work/fu"
printf 'lattice=chain\nLx=4\npbc=1\nt=-1.0\nU=4\ndtau=0.05\nbeta_list=1\nnwarm=10\nnmeas=20\nnbin=2\nstab=4\nnrep=1\nseed=1\nfield_init=uniform\n' > "$work/fu/input.in"
(cd "$work/fu" && "$root/dqmc" input.in > stdout.txt 2> err.txt) || { echo "FAIL field_init run"; fail=1; }
head -1 "$work/fu/stdout.txt" | grep -q ' field_init=uniform' || { echo "FAIL field_init header"; fail=1; }
# the dtau key is rejected
printf 'dtau=0.05\n' >> "$work/input.in"
if (cd "$work" && "$root/dqmc" input.in > /dev/null 2> err2.txt); then echo "FAIL dtau accepted"; fail=1; fi
[ "$fail" -eq 0 ] && echo OK
exit "$fail"
