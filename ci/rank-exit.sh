#!/bin/sh
# Wait for every rank and retain its status before the launcher sees success.
set -u
rank=${OMPI_COMM_WORLD_RANK:?Open MPI rank is missing}
rc=0
"$@" > "rank.$rank.stdout" 2> "rank.$rank.stderr" || rc=$?
printf '%s\n' "$rc" > "rank.$rank.exit"
exit 0
