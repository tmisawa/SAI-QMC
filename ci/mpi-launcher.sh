#!/bin/sh
# One executable path is the MPIRUN contract for both shell and Python tests.
set -eu
launcher=${CI_MPIEXEC:-mpirun}
version=$("$launcher" --version)
case "$version" in
    *"Open MPI"*|*"OpenRTE"*) ;;
    *) echo 'Expected an Open MPI launcher' >&2; exit 2 ;;
esac
# Existing three-rank/two-thread fixtures intentionally exceed four CPUs.
# No binding also leaves OpenMP workers free to use the allocated runner CPUs.
exec "$launcher" --oversubscribe --bind-to none "$@"
