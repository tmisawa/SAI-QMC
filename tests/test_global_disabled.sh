#!/bin/sh
# History-free disabled-mode equivalence. The old-source regression is a separate target.
set -eu
root="$(cd "$(dirname "$0")/.." && pwd)"
tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT
for name in chain square; do
  for variant in omitted none binonly profile selectfixed power; do
    dir="$tmp/$name.$variant"
    mkdir "$dir"
    cp "$root/tests/fixtures/global_baseline/$name.in" "$dir/input.in"
    case "$variant" in
      none) printf 'global_update=none\nglobal_interval=7\n' >> "$dir/input.in" ;;
      binonly) printf 'replica_bin_file=bins.tsv\n' >> "$dir/input.in" ;;
      profile) printf 'profile=1\nprofile_file=profile.csv\n' >> "$dir/input.in" ;;
      selectfixed) printf 'global_site_select=fixed\n' >> "$dir/input.in" ;;
      power) printf 'global_site_select=fixed\nglobal_site_power=3.25\n' >> "$dir/input.in" ;;
    esac
    (cd "$dir" && "$root/dqmc" input.in > stdout.txt)
    for file in stdout.txt szz.tsv sperp.tsv replicas.csv; do
      cmp "$tmp/$name.omitted/$file" "$dir/$file"
    done
  done
  for variant in siteonly sitefixed; do
    dir="$tmp/$name.$variant"
    mkdir "$dir"
    cp "$root/tests/fixtures/global_baseline/$name.in" "$dir/input.in"
    printf 'global_update=site\nglobal_interval=7\n' >> "$dir/input.in"
    if [ "$variant" = sitefixed ]; then
      printf 'global_site_select=fixed\nglobal_site_power=0\n' >> "$dir/input.in"
    fi
    (cd "$dir" && "$root/dqmc" input.in > stdout.txt)
  done
  for file in stdout.txt szz.tsv sperp.tsv replicas.csv; do
    cmp "$tmp/$name.siteonly/$file" "$tmp/$name.sitefixed/$file"
  done
done
echo 'OK disabled modes (no historical comparison)'
