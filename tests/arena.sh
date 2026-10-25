#!/bin/sh
# The arena's rounds, made again from the record as the window shows them,
# are the worlds the workers ran: a ladder of duels on the default box, and
# four-way rounds with newcomers in a pond.
set -e
cd "$(dirname "$0")/.."
dir=$(mktemp -d)
trap 'rm -rf "$dir"' EXIT
SYN3D_ARENA_WORLDS=$dir ./syn-biogenesis-3d --arena --board "$dir/a.board" --quiet --rounds 8 \
    --units 30 --workers 4 --seed 77 >/dev/null
SYN3D_ARENA_WORLDS=$dir ./syn-biogenesis-3d --arena --board "$dir/a.board" --quiet --rounds 12 \
    --units 20 --workers 2 --seed 900 --seats 4 --wild 25 --map pond >/dev/null
./tests/arena_check "$dir" "$dir/a.record"
