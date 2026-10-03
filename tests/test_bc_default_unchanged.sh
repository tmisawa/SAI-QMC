#!/bin/sh
# Inputs without bc_x/bc_y must stay byte-identical to a614a8f built with the
# same toolchain, and bc_x/bc_y spelling a legacy pbc value must match it.
# The only exceptions are the tempering timing values, compared by structure:
# "cost" rows of tempering_file and the "# tempering solver_elapsed_seconds=" line.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fix="$root/tests/fixtures/global_baseline"
base=a614a8f68062f5543c1a58476b4b31a33e7f606a
work="$(mktemp -d)"
# shellcheck source=tests/test_cleanup.sh
. "$root/tests/test_cleanup.sh"
trap 'test_cleanup "$work"' EXIT
if ! git -C "$root" merge-base --is-ancestor "$base" HEAD 2>/dev/null; then
  echo "FAIL historical regression requires ancestor $base; fetch full history" >&2
  exit 1
fi
mkdir "$work/base"
git -C "$root" archive "$base" | tar -xf - -C "$work/base"
make -C "$work/base" dqmc >"$work/baseline-build.log" 2>&1 || { cat "$work/baseline-build.log"; echo "FAIL baseline build"; exit 1; }
cat "$work/baseline-build.log"
fail=0
elapsed='^# tempering solver_elapsed_seconds='

run_one() { # binary input_file outdir; success cases must exit 0
  mkdir -p "$3"
  cp "$2" "$3/input.in"
  if ! (cd "$3" && "$1" input.in > stdout.txt 2> stderr.txt); then
    echo "FAIL nonzero exit: $3"; sed -n 1,5p "$3/stderr.txt"; fail=1
  fi
}

compare_file() { # name baseline_file current_file
  case "$1" in
    pt.tsv)
      grep -v '^cost' "$2" > "$work/x1" || true
      grep -v '^cost' "$3" > "$work/x2" || true
      cmp -s "$work/x1" "$work/x2" || { echo "FAIL $3: non-cost rows differ"; fail=1; }
      grep '^cost' "$2" | cut -f1-4,6-7 > "$work/x1" || true
      grep '^cost' "$3" | cut -f1-4,6-7 > "$work/x2" || true
      [ -s "$work/x2" ] || { echo "FAIL $3: no cost rows"; fail=1; }
      cmp -s "$work/x1" "$work/x2" || { echo "FAIL $3: cost row structure differs"; fail=1; }
      awk -F'\t' '/^cost/ && $5 !~ /^[0-9]+(\.[0-9]+)?$/ { bad = 1 } END { exit bad }' "$3" ||
        { echo "FAIL $3: cost seconds are not finite and non-negative"; fail=1; }
      ;;
    stdout.txt|observables.dat)
      grep -v "$elapsed" "$2" > "$work/x1" || true
      grep -v "$elapsed" "$3" > "$work/x2" || true
      cmp -s "$work/x1" "$work/x2" || { echo "FAIL $3 differs"; fail=1; }
      nb=$(grep -c "$elapsed" "$2" || true)
      nc=$(grep -c "$elapsed" "$3" || true)
      [ "$nb" = "$nc" ] || { echo "FAIL $3: elapsed line count $nb vs $nc"; fail=1; }
      if [ "$nc" -gt 0 ]; then
        tail -1 "$3" | grep -Eq "${elapsed}[0-9]+\\.[0-9]{3} nranks=[0-9]+\$" ||
          { echo "FAIL $3: elapsed line is not last or not finite and non-negative"; fail=1; }
        [ "$(tail -1 "$2" | sed 's/.* nranks=//')" = "$(tail -1 "$3" | sed 's/.* nranks=//')" ] ||
          { echo "FAIL $3: elapsed nranks differs"; fail=1; }
      fi
      ;;
    *)
      cmp -s "$2" "$3" || { echo "FAIL $3 differs"; fail=1; }
      ;;
  esac
}

compare_dirs() { # baseline_dir current_dir
  lb=$(cd "$1" && printf '%s\n' * | grep -v '^input.in$' | tr '\n' ' ')
  lc=$(cd "$2" && printf '%s\n' * | grep -v '^input.in$' | tr '\n' ' ')
  if [ "$lb" != "$lc" ]; then
    echo "FAIL $2 file sets differ: [$lb] vs [$lc]"; fail=1; return
  fi
  for f in $lb; do
    compare_file "$f" "$1/$f" "$2/$f"
  done
}

edit() { # sed_script extra_text fixture_file out_file; empty script = no edit
  if [ -n "$1" ]; then sed -e "$1" "$3" > "$4"; else cp "$3" "$4"; fi
  printf '%b' "$2" >> "$4"
}

variant() { # name fixture base_sed base_extra current_sed current_extra
  edit "$3" "$4" "$fix/$2.in" "$work/$1.base.in"
  edit "$5" "$6" "$fix/$2.in" "$work/$1.cur.in"
  run_one "$work/base/dqmc" "$work/$1.base.in" "$work/b/$1"
  run_one "$root/dqmc" "$work/$1.cur.in" "$work/c/$1"
  compare_dirs "$work/b/$1" "$work/c/$1"
}

pbc0='s/^pbc=1$/pbc=0/'
nopbc='/^pbc=1$/d'
global='global_update=site\nglobal_interval=5\nreplica_bin_file=bins.tsv\nglobal_site_diag_file=site_diag.tsv\n'
for name in chain square; do
  variant "$name-omitted" "$name" '' '' '' ''
  variant "$name-pbc0" "$name" "$pbc0" '' "$pbc0" ''
  variant "$name-bcopen" "$name" 's/^pbc=1$/bc=open/' '' 's/^pbc=1$/bc=open/' ''
  variant "$name-nopbc" "$name" "$nopbc" '' "$nopbc" ''
  variant "$name-bins" "$name" '' 'replica_bin_file=bins.tsv\n' '' 'replica_bin_file=bins.tsv\n'
  variant "$name-global" "$name" '' "$global" '' "$global"
done
variant square-explicit-p square '' '' "$nopbc" 'bc_x=periodic\nbc_y=periodic\n'
variant square-explicit-o square "$pbc0" '' "$nopbc" 'bc_x=open\nbc_y=open\n'
variant chain-explicit-p chain '' '' "$nopbc" 'bc_x=periodic\n'
variant chain-explicit-o chain "$pbc0" '' "$nopbc" 'bc_x=open\n'
pt='tempering=dtau_ladder\ntempering_ltr=20\ntempering_file=pt.tsv\nreplica_bin_file=bins.tsv\n'
variant chain-pt chain '/^dtau=/d' "$pt" '/^dtau=/d' "$pt"
# lattice=file with the baseline's own square matrix (selectors are not allowed)
nofile='/^lattice=/d;/^Lx=/d;/^Ly=/d;/^pbc=/d;/^szz_/d;/^sperp_/d'
file_extra="lattice=file\nlatfile=$work/b/square-omitted/hopping_used.txt\nreplica_bin_file=bins.tsv\n"
variant square-file square "$nofile" "$file_extra" "$nofile" "$file_extra"
[ "$fail" -eq 0 ] && echo OK
exit "$fail"
