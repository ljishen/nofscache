#!/usr/bin/env bash

set -eu -o pipefail

SCRIPT_NAME="$(basename "${BASH_SOURCE[0]}")"

usage() {
  printf "Usage: ./%s <r|w> [DD_OPTIONS]
<r|w>\\t\\t: Test dd read or dd write.
DD_OPTIONS\\t: See DD(1) for all DD options.

This script requires a FLAMEGRAPH_SRC variable to point to the repository of FlameGraph.
See https://github.com/brendangregg/FlameGraph
" "$SCRIPT_NAME"
}

if [[ $EUID -ne 0 ]]; then
  printf >&2 "[Error] This script must be run as root.\\n\\n"
  usage
  exit 1
fi

if [[ "$#" -lt 1 ]]; then
  usage
  exit 1
fi

# string to lower case
ops="${1,,}"

if [[ "$ops" != "r" && "$ops" != "w" ]]; then
  printf >&2 "[Error] Operation can only be 'r' or 'w'.\\n\\n"
  usage
  exit 1
fi

# remove the 1st parameter
set -- "${@:2}"

if [[ -z "${FLAMEGRAPH_SRC:-}" ]]; then
  printf >&2 "[Error] Please set the FLAMEGRAPH_SRC variable before running this script.\\n\\n"
  usage
  exit 2
fi

tmpfile="$(mktemp --dry-run "$PWD"/perf_dd.data.XXXXXXXXXX)"

die() {
  rm -f "$tmpfile"
}
trap die EXIT

if [[ "$ops" == "r" ]]; then
  TEST_FILE_SIZE_KiB="$((30 * 1024 * 1024))"

  echo "[INFO] Generating data file $tmpfile ($TEST_FILE_SIZE_KiB KiB)"
  fallocate --length "$TEST_FILE_SIZE_KiB"KiB "$tmpfile"

  dd_comm=(
    dd
    "if=$tmpfile"
    "of=/dev/null"
    "count=$((TEST_FILE_SIZE_KiB / 4))"
    "bs=4K"
    "$@"
  )
else
  TEST_FILE_SIZE_KiB="$((10 * 1024 * 1024))"

  dd_comm=(
    dd
    "if=/dev/zero"
    "of=$tmpfile"
    "count=$((TEST_FILE_SIZE_KiB / 4))"
    "bs=4K"
    "$@"
  )
fi

OUTPUT_FILE_PREFIX=dd_"$ops"

rm -f "$OUTPUT_FILE_PREFIX".data.old

echo "[INFO] Running perf record..."
echo "[INFO] dd command: ${dd_comm[*]}"

perf record \
  -F 99 \
  --proc-map-timeout "$((TEST_FILE_SIZE_KiB * 100 / 1024))" \
  -o "$OUTPUT_FILE_PREFIX".data \
  -a -g -- \
  "${dd_comm[@]}"

echo "[INFO] Running perf script..."
perf script -f \
  -i "$OUTPUT_FILE_PREFIX".data |
  "$FLAMEGRAPH_SRC"/stackcollapse-perf.pl > "$OUTPUT_FILE_PREFIX".folded

echo "[INFO] Generating flamegraph..."
grep dd "$OUTPUT_FILE_PREFIX".folded | "$FLAMEGRAPH_SRC"/flamegraph.pl > "$OUTPUT_FILE_PREFIX".svg

echo "[INFO] Generated $OUTPUT_FILE_PREFIX.svg in the current dir."
