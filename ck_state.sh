#!/usr/bin/env bash

set -eu -o pipefail

SCRIPT_NAME="$(basename "$BASH_SOURCE")"

usage() {
  printf "Usage: ./%s MOD_NAME CHECK_STATE
MOD_NAME\\t: name of kernel module
CHECK_STATE\\t: patch state to check. 0 is unpatched and 1 is patched.

eg,
    ./%s no_fscache 0   # check unpatched state for module no_fscache

See livepatch consistency model for details:
    https://www.kernel.org/doc/Documentation/livepatch/livepatch.txt
" "$SCRIPT_NAME" "$SCRIPT_NAME"
}

if [[ "$#" -ne 2 ]]; then
  usage
  exit 1
fi

mod_name="$1"
mod_sysfs_if=/sys/kernel/livepatch/"$mod_name"
if [[ ! -d "$mod_sysfs_if" ]]; then
  printf "[Error] Module %s does not exist in sysfs interface path %s\n\n" "$mod_name" "$(dirname "$mod_sysfs_if")"
  exit 2
fi

check_state="$2"
if [[ "$check_state" -ne 0 && "$check_state" -ne 1 ]]; then
  printf "[Error] CHECK_STATE can only be 0 or 1.\\n\\n"
  usage
  exit 2
fi

if [[ $EUID -ne 0 ]]; then
  printf "[Error] This script must be run as root.\\n\\n"
  usage
  exit 3
fi

MAX_SHOW_NUM_TASKS=5

print_blocking_tasks() {
  blocking_tasks=()
  for ps in /proc/*/task/*/patch_state; do
    tid="$(echo "$ps" | cut -d '/' -f 5)"
    task_path=/proc/"${tid}"

    if [[ -f "$task_path"/patch_state && "$(sudo cat "$task_path"/patch_state)" -eq "$check_state" ]]; then
      blocking_tasks+=("$tid")
    fi
  done

  if [[ "${#blocking_tasks[@]}" -ne 0 ]]; then
    blocking_tids="$(
      IFS=,
      echo "${blocking_tasks[@]}"
    )"
    results="$(ps -eo user,pid,tid,cmd -q "$blocking_tids" || true)"
    lines=$(echo "$results" | wc -l)
    num_results=$((lines - 1))

    if [[ "$num_results" -gt 0 ]]; then
      printf "[INFO] %d tasks are not in %s state:\\n" \
        "$num_results" \
        "$([[ "$check_state" -eq 1 ]] && echo unpatched || echo patched)"

      # show +1 lines of results because of the header line
      echo "$results" | head -$((MAX_SHOW_NUM_TASKS + 1))

      if [[ "$num_results" -gt "$MAX_SHOW_NUM_TASKS" ]]; then
        printf "... %d more\\n" "$((num_results - MAX_SHOW_NUM_TASKS))"
      fi

      echo
    fi
  fi
}

CHECK_FREQ_IN_SECS=2

printf "[INFO] Checking transition state for module %s (update every %ds)...\n\n" \
  "$mod_name" \
  "$CHECK_FREQ_IN_SECS"

while :; do
  transition_state="$(cat "$mod_sysfs_if"/transition)"
  if [[ "$transition_state" -eq 0 ]]; then
    break
  fi

  print_blocking_tasks
  sleep "$CHECK_FREQ_IN_SECS"
done
