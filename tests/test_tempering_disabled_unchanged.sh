#!/bin/sh
# tempering=none runs must stay byte-identical to 3215eee built with the same toolchain.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fix="$root/tests/fixtures/global_baseline"
base=3215eee700b9b6359242e228e515cf83a5a53732
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
run_one() { # binary input extra outdir; success cases must exit 0
  mkdir -p "$4"
  cp "$2" "$4/input.in"
  printf '%b' "$3" >> "$4/input.in"
  if ! (cd "$4" && "$1" input.in > stdout.txt 2> stderr.txt); then
    echo "FAIL nonzero exit: $4"; sed -n 1,5p "$4/stderr.txt"; fail=1
  fi
}
for name in chain square; do
  # field_random is re-added by Task 5b together with the field_init key.
  for variant in omitted none global select bins alternating diag drift udvscale centered profile; do
    case "$variant" in
      omitted)      extra='' ;;
      none)         extra='tempering=none\ntempering_interval=3\n' ;;
      global)       extra='global_update=site\nglobal_interval=5\nreplica_bin_file=bins.tsv\n' ;;
      select)       extra='global_update=site\nglobal_interval=5\nglobal_site_select=polarized\nreplica_bin_file=bins.tsv\n' ;;
      bins)         extra='replica_bin_file=bins.tsv\n' ;;
      alternating)  extra='sweep_order=alternating\nglobal_update=site\nglobal_interval=3\n' ;;
      diag)         extra='global_update=site\nglobal_interval=5\nglobal_site_diag_file=site_diag.tsv\n' ;;
      drift)        extra='stab_drift_file=drift.txt\n' ;;
      udvscale)     extra='udv_scale_file=udv_scale.txt\n' ;;
      centered)     extra='green_rebuild=centered\nudv_centered_file=udv_centered.txt\n' ;;
      profile)      extra='profile=1\nprofile_file=profile.csv\n' ;;
    esac
    # the baseline does not know the new keys: run it without them
    base_extra="$extra"
    case "$variant" in none) base_extra='' ;; esac
    run_one "$work/base/dqmc" "$fix/$name.in" "$base_extra" "$work/b/$name-$variant"
    run_one "$root/dqmc" "$fix/$name.in" "$extra" "$work/c/$name-$variant"
    # same set of files
    lb=$(cd "$work/b/$name-$variant" && ls | grep -v '^input.in$' | tr '\n' ' ')
    lc=$(cd "$work/c/$name-$variant" && ls | grep -v '^input.in$' | tr '\n' ' ')
    [ "$lb" = "$lc" ] || { echo "FAIL $name-$variant file sets differ: [$lb] vs [$lc]"; fail=1; }
    for f in $lb; do
      if [ "$f" = profile.csv ]; then
        # timings differ; compare schema and call counts (profile.csv is space-separated)
        cut -d' ' -f1-7,13-15 "$work/b/$name-$variant/$f" > "$work/pb"
        cut -d' ' -f1-7,13-15 "$work/c/$name-$variant/$f" > "$work/pc"
        cmp -s "$work/pb" "$work/pc" || { echo "FAIL $name-$variant profile schema/calls"; fail=1; }
        continue
      fi
      cmp -s "$work/b/$name-$variant/$f" "$work/c/$name-$variant/$f" || { echo "FAIL $name-$variant $f differs"; fail=1; }
    done
  done
done
[ "$fail" -eq 0 ] && echo OK
exit "$fail"
