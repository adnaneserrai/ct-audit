#!/bin/sh
# Runs the S-box AES follow-up experiments with gcc and clang (-O2), three seeds each.
# Usage: scripts/aes_probe.sh CORE N
CORE=${1:-3}; N=${2:-1000000}
PQ=third_party/PQClean
mkdir -p build results
for cc in gcc clang; do
  $cc -O2 -Wall -Wno-unused-function -Isrc -I$PQ/common -o build/aes_probe_$cc tools/aes_probe.c $PQ/common/aes.c -lm || exit 1
  for s in 1 2 3; do
    echo "== $cc seed $s"
    taskset -c "$CORE" build/aes_probe_$cc "$N" - "$s" | awk '{printf "%-20s max|t| = %s\n", $2, $NF}'
  done
done
