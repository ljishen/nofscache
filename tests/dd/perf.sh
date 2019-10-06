#!/usr/bin/env bash

set -eu -o pipefail

SCRIPT_NAME="$(basename "${BASH_SOURCE[0]}")"

usage() {
  printf "Usage: ./%s [DD_OPTIONS]
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

if [[ -z "${FLAMEGRAPH_SRC:-}" ]]; then
  printf >&2 "[Error] Please set the FLAMEGRAPH_SRC variable before running this script.\\n\\n"
  usage
  exit 2
fi

tmpfile="$(mktemp --dry-run /tmp/perf_dd.data.XXXXXXXXXX)"

die() {
  rm -f "$tmpfile"
}
trap die EXIT

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

OUTPUT_FILE_PREFIX=dd

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
"$FLAMEGRAPH_SRC"/flamegraph.pl "$OUTPUT_FILE_PREFIX".folded > "$OUTPUT_FILE_PREFIX".svg

echo "[INFO] Generated $OUTPUT_FILE_PREFIX.svg in the current dir."
