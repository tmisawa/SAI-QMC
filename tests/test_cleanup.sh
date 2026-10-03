#!/bin/sh
# Sourced by CLI regressions. Keep their generated files only after a failure.
test_cleanup() {
    test_rc=$?
    if [ "$test_rc" -ne 0 ] && [ -n "${CI_FAILURE_DIR:-}" ]; then
        mkdir -p "$CI_FAILURE_DIR"
        cp -R "$1" "$CI_FAILURE_DIR/$(basename "$0")" || :
    fi
    rm -rf "$1"
    trap - EXIT HUP INT TERM
    exit "$test_rc"
}
