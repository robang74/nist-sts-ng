#!/bin/sh
set -eu
here=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
bin="${TMPDIR:-/tmp}/verify-phase6-$$"
trap 'rm -f "$bin"' EXIT HUP INT TERM
${CC:-cc} -O2 -Wall -Wextra -Werror -std=c99 \
    "$here/phase6-kernels-standalone.c" \
    "$here/verify_phase6_helpers.c" -o "$bin"
"$bin"
