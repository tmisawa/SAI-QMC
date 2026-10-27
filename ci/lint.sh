#!/bin/sh
# CI lint tools are versioned; the downloaded executable is checksum-verified.
set -eu
version=1.7.12
archive="actionlint_${version}_linux_amd64.tar.gz"
mkdir -p build/ci/lint
curl --fail --location --retry 2 --output "build/ci/lint/$archive" \
    "https://github.com/rhysd/actionlint/releases/download/v$version/$archive"
printf '%s  %s\n' '8aca8db96f1b94770f1b0d72b6dddcb1ebb8123cb3712530b08cc387b349a3d8' \
    "build/ci/lint/$archive" | sha256sum --check --strict
tar -xzf "build/ci/lint/$archive" -C build/ci/lint actionlint
build/ci/lint/actionlint
shellcheck ci/*.sh tests/test_cleanup.sh tests/test_szz_parallel.sh \
    tests/test_sperp_parallel.sh tests/test_bc_parallel.sh \
    tests/test_tempering_parallel.sh tests/test_global_parallel.sh \
    tests/test_bc_default_unchanged.sh tests/test_tempering_disabled_unchanged.sh
