#!/usr/bin/env bash

set -eu -o pipefail

if [[ $EUID -ne 0 ]]; then
  echo "[Error] This script must be run as root."
  exit 1
fi

if [[ "$#" -ne 1 ]]; then
  printf "Usage: %s CHECK_STATE
CHECK_STATE: patch state to check. 0 is unpatched and 1 is patched.

See livepatch consistency model for details:
    https://www.kernel.org/doc/Documentation/livepatch/livepatch.txt
" "$(basename "$0")"
  exit 1
fi

check_state="$1"

blocking_tasks=()
for ps in /proc/*/task/*/patch_state; do
  tid=$(echo "$ps" | awk -F'/' '{ print $5 }')
  task_path=/proc/"${tid}"

  if [[ -f "$task_path"/patch_state && "$(sudo cat "$task_path"/patch_state)" -eq "$check_state" ]]; then
    blocking_tasks+=("$tid")
  fi
done

num_blocking_tasks="${#blocking_tasks[@]}"

SHOW_NUM_TASKS=5

if [[ "$num_blocking_tasks" -ne 0 ]]; then
  printf "[INFO] %d tasks are not in %s state:\\n" \
    "$num_blocking_tasks" \
    "$([[ "$check_state" -eq 1 ]] && echo unpatched || echo patched)"
  ps -eo user,pid,tid,cmd -q "$(
    IFS=,
    echo "${blocking_tasks[@]:0:$SHOW_NUM_TASKS}"
  )"

  if [[ "$num_blocking_tasks" -gt "$SHOW_NUM_TASKS" ]]; then
    printf "... %d more\\n" "$((num_blocking_tasks - 5))"
  fi

  echo
fi
