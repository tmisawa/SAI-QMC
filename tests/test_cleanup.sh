#!/bin/sh
# Sourced by CLI regressions. Keep their generated files only after a failure.
test_cleanup() {
    test_rc=$?
    if [ "$test_rc" -ne 0 ] && [ -n "${CI_FAILURE_DIR:-}" ]; then
        if mkdir -p "$CI_FAILURE_DIR" &&
            test_saved=$(mktemp -d "$CI_FAILURE_DIR/$(basename "$0").XXXXXX") &&
            cp -R "$1/." "$test_saved/"; then
            rm -rf "$1"
        else
            printf 'Failed to archive test outputs; retained in %s\n' "$1" >&2
        fi
    else
        rm -rf "$1"
    fi
    trap - EXIT HUP INT TERM
    exit "$test_rc"
}
