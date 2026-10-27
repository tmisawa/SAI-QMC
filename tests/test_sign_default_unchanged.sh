#!/bin/sh
# Inputs without mu, tprime or sign_file whose model is particle-hole
# symmetric must stay byte-identical to 4384c37 (the base of the
# sign-reweighting extension) built with the same toolchain: every output
# file, stdout and stderr. Time-dependent values are compared by structure
# only: the profile seconds columns, the "cost" rows of tempering_file and
# the "# tempering solver_elapsed_seconds=" line. With the argument
# "parallel" a subset is also compared for the OpenMP, MPI and hybrid builds;
# both sides must use the same compilers (e.g. OMPI_CC for Open MPI).
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fix="$root/tests/fixtures/global_baseline"
base=4384c37c1b9740016bdeea1bdd8f6b48078b051e
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
if ! git -C "$root" cat-file -e "$base^{commit}" 2>/dev/null; then
  echo "FAIL historical regression requires Git commit $base" >&2
  exit 1
fi
mkdir "$work/base"
git -C "$root" archive "$base" | tar -xf - -C "$work/base"
make -C "$work/base" dqmc >/dev/null 2>&1 || { echo "FAIL baseline build"; exit 1; }
fail=0
nvariant=0
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
      cmp -s "$work/x1" "$work/x2" || { echo "FAIL $3: cost row structure differs"; fail=1; }
      ;;
    stdout.txt|observables.dat)
      grep -v "$elapsed" "$2" > "$work/x1" || true
      grep -v "$elapsed" "$3" > "$work/x2" || true
      cmp -s "$work/x1" "$work/x2" || { echo "FAIL $3 differs"; fail=1; }
      [ "$(grep -c "$elapsed" "$2" || true)" = "$(grep -c "$elapsed" "$3" || true)" ] ||
        { echo "FAIL $3: elapsed line count"; fail=1; }
      ;;
    profile.csv)
      # beta T dtau Ltr phase region calls | seconds (8-12) | nrep parallel
      cut -d' ' -f1-7,13-14 "$2" > "$work/x1"
      cut -d' ' -f1-7,13-14 "$3" > "$work/x2"
      cmp -s "$work/x1" "$work/x2" || { echo "FAIL $3: profile schema/calls differ"; fail=1; }
      ;;
    *)
      cmp -s "$2" "$3" || { echo "FAIL $3 differs"; fail=1; }
      ;;
  esac
}

compare_dirs() { # baseline_dir current_dir
  lb=$(cd "$1" && ls | grep -v '^input.in$' | tr '\n' ' ')
  lc=$(cd "$2" && ls | grep -v '^input.in$' | tr '\n' ' ')
  if [ "$lb" != "$lc" ]; then
    echo "FAIL $2 file sets differ: [$lb] vs [$lc]"; fail=1; return
  fi
  for f in $lb; do
    compare_file "$f" "$1/$f" "$2/$f"
  done
}

variant() { # name fixture sed_script extra_text
  if [ -n "$3" ]; then sed -e "$3" "$fix/$2.in" > "$work/$1.in"; else cp "$fix/$2.in" "$work/$1.in"; fi
  printf '%b' "$4" >> "$work/$1.in"
  run_one "$work/base/dqmc" "$work/$1.in" "$work/b/$1"
  run_one "$root/dqmc" "$work/$1.in" "$work/c/$1"
  compare_dirs "$work/b/$1" "$work/c/$1"
  nvariant=$((nvariant + 1))
}

fwd='s/^sweep_order=alternating$/sweep_order=forward/'
nopbc='/^pbc=1$/d'
bins='replica_bin_file=bins.tsv\n'
global='global_update=site\nglobal_interval=5\nreplica_bin_file=bins.tsv\nglobal_site_diag_file=site_diag.tsv\n'
polar='global_update=site\nglobal_interval=3\nglobal_site_select=polarized\nglobal_site_power=2\nreplica_bin_file=bins.tsv\n'
for name in chain square; do
  variant "$name-omitted" "$name" '' ''
  variant "$name-pbc0" "$name" 's/^pbc=1$/pbc=0/' ''
  variant "$name-bins" "$name" '' "$bins"
  variant "$name-global" "$name" '' "$global"
  variant "$name-polarized" "$name" '' "$polar"
  variant "$name-su2" "$name" '' 'spin_consistency_file=su2.tsv\n'
  variant "$name-profile" "$name" '' 'profile=1\nprofile_file=profile.csv\n'
  variant "$name-uniform" "$name" '' 'field_init=uniform\n'
done
variant chain-forward chain "$fwd" "$bins"
variant chain-two-sided chain "$fwd;s/^green_rebuild=combine$/green_rebuild=two_sided/" ''
variant chain-centered chain "s/^green_rebuild=combine$/green_rebuild=centered/" ''
variant chain-ap chain "$nopbc" 'bc_x=antiperiodic\nreplica_bin_file=bins.tsv\n'
variant square-ap square "$nopbc" 'bc_x=antiperiodic\nreplica_bin_file=bins.tsv\n'
variant square-op square "$nopbc" 'bc_x=antiperiodic\nbc_y=open\n'
pt='tempering=dtau_ladder\ntempering_ltr=20\ntempering_file=pt.tsv\nreplica_bin_file=bins.tsv\n'
variant chain-pt chain '/^dtau=/d' "$pt"
variant chain-pt-cond chain '/^dtau=/d' "${pt}conditional_measure=1\n"
variant chain-cond chain "$fwd" 'conditional_measure=1\nreplica_bin_file=bins.tsv\n'
variant square-cond square '' 'conditional_measure=1\nreplica_bin_file=bins.tsv\n'
if [ "${1:-}" = parallel ]; then
  # the same comparison for the OpenMP, MPI and hybrid builds (MPIRUN, 2 ranks)
  make -C "$work/base" dqmc_omp dqmc_mpi dqmc_hybrid >/dev/null 2>&1 ||
    { echo "FAIL baseline parallel build"; exit 1; }
  for mode in omp mpi hybrid; do
    for v in chain-bins square-global chain-pt square-ap; do
      sed -e "s/^parallel=.*//" "$work/$v.in" > "$work/$v.$mode.in"
      printf 'parallel=%s\n' "$mode" >> "$work/$v.$mode.in"
      for side in b c; do
        bin="$work/base/dqmc_$mode"
        [ "$side" = c ] && bin="$root/dqmc_$mode"
        mkdir -p "$work/$side/$v.$mode"
        cp "$work/$v.$mode.in" "$work/$side/$v.$mode/input.in"
        if [ "$mode" = omp ]; then
          (cd "$work/$side/$v.$mode" && OMP_NUM_THREADS=2 "$bin" input.in > stdout.txt 2> stderr.txt) ||
            { echo "FAIL nonzero exit: $side $v $mode"; fail=1; }
        else
          (cd "$work/$side/$v.$mode" && OMP_NUM_THREADS=2 "${MPIRUN:-mpirun}" -n 2 "$bin" input.in > stdout.txt 2> stderr.txt) ||
            { echo "FAIL nonzero exit: $side $v $mode"; fail=1; }
        fi
      done
      compare_dirs "$work/b/$v.$mode" "$work/c/$v.$mode"
      nvariant=$((nvariant + 1))
    done
  done
fi
# lattice=file with the square fixture's own matrix (no q selectors)
run_one "$work/base/dqmc" "$fix/square.in" "$work/matrix"
nofile='/^lattice=/d;/^Lx=/d;/^Ly=/d;/^pbc=/d;/^szz_/d;/^sperp_/d'
variant square-file square "$nofile" "lattice=file\nlatfile=$work/matrix/hopping_used.txt\nreplica_bin_file=bins.tsv\n"
# Legacy scope of the hopping-file tolerance: elements below 1e-12 are still
# ignored by the bipartite test without mu/sign_file (byte-identical output).
printf '3\n0 -1e-13 -1e-13\n-1e-13 0 -1e-13\n-1e-13 -1e-13 0\n' > "$work/tiny.txt"
printf 'lattice=file\nlatfile=%s\nU=4e-13\ndtau=5e12\nbeta_list=1e13\nnwarm=50\nnmeas=200\nnbin=10\nnrep=2\nseed=7\nreplica_bin_file=bins.tsv\n' \
  "$work/tiny.txt" > "$work/tiny-file.in"
run_one "$work/base/dqmc" "$work/tiny-file.in" "$work/b/tiny-file"
run_one "$root/dqmc" "$work/tiny-file.in" "$work/c/tiny-file"
compare_dirs "$work/b/tiny-file" "$work/c/tiny-file"
nvariant=$((nvariant + 1))
[ "$fail" -eq 0 ] && echo "OK ($nvariant legacy inputs byte-identical to ${base%"${base#???????}"})"
exit "$fail"
