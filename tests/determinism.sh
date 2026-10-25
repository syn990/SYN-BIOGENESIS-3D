#!/bin/sh
# A seed names one world: the optimised and unoptimised builds, and a clang
# build when clang is installed, must reach the same state in every kind
# of world.
set -e
cd "$(dirname "$0")/.."
bins="./syn-biogenesis-3d ./tests/syn-biogenesis-3d-O0"
if command -v clang >/dev/null 2>&1; then
    clang -std=c11 -D_DEFAULT_SOURCE -DSYN3D_NO_WINDOW -O2 -ffp-contract=off -Isrc \
        -o tests/syn-biogenesis-3d-clang src/main.c src/headless.c src/options.c src/sim/*.c \
        src/io/*.c src/syn/*.c src/guide/palette.c -lm
    bins="$bins ./tests/syn-biogenesis-3d-clang"
fi
fail=0
for args in "--size 1000 1000 8 --pitch 0" "--size 600 600 300" "--size 400 400 400 --pitch 100"; do
    ref=""
    for bin in $bins; do
        h=$($bin --headless --seed 5 --frames 6000 $args --hash 2>&1 >/dev/null | grep hash)
        if [ -z "$ref" ]; then
            ref=$h
        elif [ "$h" != "$ref" ]; then
            echo "FAIL $bin differs ($args): $h, not $ref"
            fail=1
        fi
    done
    echo "  $(echo $bins | wc -w) builds agree ($args): $ref"
done
exit $fail
