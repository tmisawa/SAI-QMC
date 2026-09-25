#!/bin/sh
# Default runs must stay byte-identical to 463dc75 built with the same toolchain (spec 7.9).
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
fix="$root/tests/fixtures/global_baseline"
variants="${*:-omitted none binonly profile}"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# This explicit historical regression requires the old source; normal make test does not.
if ! git -C "$root" cat-file -e '463dc75^{commit}' 2>/dev/null; then
  echo "FAIL historical regression requires Git commit 463dc75; run make test for history-free tests" >&2
  exit 1
fi
# clean baseline build in isolation: no object files are shared with the working tree
mkdir "$work/base"
git -C "$root" archive 463dc75 > "$work/base.tar"
tar -xf "$work/base.tar" -C "$work/base"
make -C "$work/base" dqmc >/dev/null 2>&1 || { echo "FAIL baseline build"; exit 1; }
{
  echo "baseline_commit=463dc75"
  echo "cc=$(cc --version | head -1)"
  echo "cflags=$(make -C "$root" -pn dqmc 2>/dev/null | sed -n 's/^CFLAGS *= *//p' | head -1)"
  echo "ldlibs=$(make -C "$root" -pn dqmc 2>/dev/null | sed -n 's/^LDLIBS *= *//p' | head -1)"
  echo "baseline_sha256=$(shasum -a 256 "$work/base/dqmc" | cut -d' ' -f1)"
  echo "current_sha256=$(shasum -a 256 "$root/dqmc" | cut -d' ' -f1)"
} > "$work/provenance.txt"
fail=0
run_one() { # binary input extra outdir
  mkdir -p "$4"
  cp "$2" "$4/input.in"
  printf '%b' "$3" >> "$4/input.in"
  (cd "$4" && "$1" input.in > stdout.txt 2> stderr.txt)
}
for name in chain square; do
  for variant in $variants; do
    case "$variant" in
      omitted) extra='' ;;
      none)    extra='global_update=none\nglobal_interval=7\n' ;;
      binonly) extra='replica_bin_file=bins.tsv\n' ;;
      profile) extra='profile=1\nprofile_file=profile.csv\n' ;;
      *) echo "unknown variant $variant"; exit 2 ;;
    esac
    base_extra=''
    [ "$variant" = profile ] && base_extra="$extra"
    run_one "$work/base/dqmc" "$fix/$name.in" "$base_extra" "$work/b.$name.$variant" || { echo "FAIL baseline run $name/$variant"; fail=1; continue; }
    run_one "$root/dqmc" "$fix/$name.in" "$extra" "$work/c.$name.$variant" || { echo "FAIL run $name/$variant"; fail=1; continue; }
    for f in stdout.txt szz.tsv sperp.tsv replicas.csv; do
      cmp -s "$work/b.$name.$variant/$f" "$work/c.$name.$variant/$f" || { echo "FAIL $f $name/$variant"; fail=1; }
    done
    if [ "$variant" = profile ]; then
      # compare schema and call counts, not measured seconds
      for d in b c; do cut -d, -f1-7,13-15 "$work/$d.$name.$variant/profile.csv" > "$work/$d.$name.prof"; done
      cmp -s "$work/b.$name.prof" "$work/c.$name.prof" || { echo "FAIL profile schema/calls $name"; fail=1; }
    fi
  done
done
[ "$fail" -eq 0 ] && { cat "$work/provenance.txt"; echo OK; } || exit 1
